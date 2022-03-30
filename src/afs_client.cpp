#define FUSE_USE_VERSION 31

#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <fuse_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <unordered_map>

#include "client_crash.h"
#include "afs_client_rpc.h"
#include "util.h"

using namespace std;

// rpc caller object                                                                                                                                                        
static AfsClient* afs_client = NULL;  

static FILE* log_file;

struct attr {
  int dirty;
  struct timespec server_mtime;
};

//read atribute file
int read_attr(const char* path, struct attr *attr) {
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  
  read(fd, attr, sizeof(*attr));

  close(fd);
  return 0;
}

//update attribute file and persist to disk
int write_attr(const char* path, struct attr *attr) {
  int fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU);
  if (fd == -1) {
    return -1;
  }
  
  write(fd, attr, sizeof(*attr));
  fsync(fd);

  close(fd);
  return 0;
}

constexpr int CHECK_CRASH =  1;

std::string crash_filename;

//check if the num matches the crash code in the file
int should_crash(int num) {
  FILE *file = fopen(crash_filename.c_str(), "r");
  if (file == NULL) {
    perror("open");
    return 0;
  }
  
  int result_num;

  if (fscanf(file, "%i", &result_num) != 1) {
    fclose(file);
    return 0;
  }

  if (num == result_num) {
    fclose(file);
    return 1;
  }

  fclose(file);
  return 0;
}

//check if I should crash and then do it
inline void check_crash(int num) {
  if (CHECK_CRASH && should_crash(num)) {
    printf("here\n");
    kill(getpid(), SIGKILL);  
  }
}

void print_stat(struct stat *sb){
printf("\n\nStats\n\n");
printf("File type:                ");

   switch (sb->st_mode & S_IFMT) {
    case S_IFBLK:  printf("block device\n");            break;
    case S_IFCHR:  printf("character device\n");        break;
    case S_IFDIR:  printf("directory\n");               break;
    case S_IFIFO:  printf("FIFO/pipe\n");               break;
    case S_IFLNK:  printf("symlink\n");                 break;
    case S_IFREG:  printf("regular file\n");            break;
    case S_IFSOCK: printf("socket\n");                  break;
    default:       printf("unknown?\n");                break;
    }

   printf("I-node number:            %ld\n", (long) sb->st_ino);

   printf("Mode:                     %lo (octal)\n",
            (unsigned long) sb->st_mode);

   printf("Link count:               %ld\n", (long) sb->st_nlink);
    printf("Ownership:                UID=%ld   GID=%ld\n",
            (long) sb->st_uid, (long) sb->st_gid);

   printf("Preferred I/O block size: %ld bytes\n",
            (long) sb->st_blksize);
    printf("File size:                %lld bytes\n",
            (long long) sb->st_size);
    printf("Blocks allocated:         %lld\n",
            (long long) sb->st_blocks);

   printf("Last status change:       %s", ctime(&sb->st_ctime));
    printf("Last file access:         %s", ctime(&sb->st_atime));
    printf("Last file modification:   %s", ctime(&sb->st_mtime));

}
static int afs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
  /**
   * Always return the stat from the server
   **/
  fprintf(log_file, "afs_getattr\n");
  string buf;
  int res = afs_client->GetAttr(path, buf);
  fprintf(log_file, " res: %d\n", res);
  if(res < 0)
    return res;
  
  memcpy(stbuf, &buf[0], buf.size());

  struct attr attr;
  read_attr(get_attr_path(path).c_str(), &attr);
  if (attr.server_mtime.tv_sec == stbuf->st_mtim.tv_sec && attr.server_mtime.tv_nsec == stbuf->st_mtim.tv_nsec && attr.dirty) {
    struct stat statbuf;
    lstat(get_cache_path(path).c_str(), &statbuf);
    stbuf->st_size = statbuf.st_size;
  }
  
  //print_stat(stbuf); 
  return 0;
}


int mkpath(char* file_path, mode_t mode) {
    assert(file_path && *file_path);
    for (char* p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(file_path, mode) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }
    return 0;
}

static void *afs_init(struct fuse_conn_info *conn, struct fuse_config *cfg){
cfg->direct_io=1;
return NULL;

}
static int afs_open(const char *path, struct fuse_file_info *fi) {
  fprintf(log_file, "afs_open\n");
  printf("\nafs_open %s \n", path); 
  /**
   * 1) Get attr from server
   * 3) Get attr file of the local file and compare with the server attr. set |modified| flag
   * 3) if |modified| 
   *         Fetch from the server, copy into the local (fsync)
   * 4) Copy local contents to the temp (fsync not required) 
   * 5) Return temp handle to the end user
   **/
  
  // Get attr from server
  string sbuf;
  struct stat sstat, lstat;
  int res = afs_client->GetAttr(path, sbuf);
  memcpy(&sstat, &sbuf[0], sbuf.size());
  
  // Get local attr if file exists
  int fetch = 1;
  struct attr st;
  if(exists(get_cache_path(path))) {
    //read the local attr file and compare with server attribute
    struct attr st;
    read_attr(get_attr_path(path).c_str(),&st);
    if((st.server_mtime.tv_sec == sstat.st_mtim.tv_sec) && (st.server_mtime.tv_nsec == sstat.st_mtim.tv_nsec )) {
	    printf("**********using from the cache************\n");
	    fetch = 0; 
    }
  }
  //path does not exist so create the directory structure
  else {
	string temp = get_cache_path(path);
	char * cstr = new char [temp.length()+1];
        std::strcpy (cstr, temp.c_str());
	mkpath(cstr, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  }

  // if modified fetch from the server
  if(fetch) {
    	printf("*********Fetching from the server***********\n");
    	string data;
	struct timespec mtime;
    	res = afs_client->Fetch(path, data , &mtime);
	if(res < 0)
      		return res;
	check_crash(CRASH_OPEN);
    	save_file(get_cache_path(path),data);
     //reset the attributes
  	st.dirty = 0;
  	st.server_mtime.tv_sec = mtime.tv_sec;
	st.server_mtime.tv_nsec = mtime.tv_nsec;
  	write_attr(get_attr_path(path).c_str(),&st);
  }
  
  fi->fh = open(get_cache_path(path).c_str(), fi->flags);
  fprintf(log_file, " success\n");
  
  return 0;
}

static int afs_flush(const char *path, struct fuse_file_info *fi) {

//  	1) Check the write flag for the file path.
//  	2) If set, store the changes to the server and update mtime

  fprintf(log_file, "afs_release\n");

  check_crash(CRASH_RELEASE_START);

  struct attr attr;
  if (read_attr(get_attr_path(path).c_str(), &attr) == -1) {
    return -EIO;
  }

  if (attr.dirty) {
    if (fi == NULL || fi->fh == 0) {
      return -EBADF;
    }

    if (fsync(fi->fh) == -1) {
      if (errno & EBADF) {
        return -errno;
      }
      return -EIO;
    }

    //file might not have been opened in read mode so re-open it
    int fd = open(get_cache_path(path).c_str(), O_RDONLY);
    if (fd == -1){
      return -EIO;
    }

    struct timespec mtime;
    if(afs_client->Store(path, fd, &mtime) != 0) {
      return -EIO;
    }

    if (close(fd) == -1) {
      return -errno;
    }

    check_crash(CRASH_RELEASE_AFTER_STORE);

    attr.dirty = 0;
    attr.server_mtime.tv_sec = mtime.tv_sec;
    attr.server_mtime.tv_nsec = mtime.tv_nsec;

    if (write_attr(get_attr_path(path).c_str(), &attr) == -1) {
      return -EIO;
    }

  }
  return 0;
}

static int afs_release(const char *path, struct fuse_file_info *fi) {
  if (fi==NULL) {
    return -EBADF;
  }
  if (close(fi->fh)) {
    return -errno;
  }
  return 0;
}
static int afs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
  if (fi == NULL) {
    return -EBADF;
  }

  //just pass it down to the underlying cached file.
  if (fsync(fi->fh) == -1) {
    if (errno & EBADF) {
      return -errno;
    }
    return -EIO;
  }

  return 0;
}
static int afs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
  fprintf(log_file, "afs_truncate\n");
  
  struct attr attr;
  if (read_attr(get_attr_path(path).c_str(), &attr) == -1) {
    return -EIO;
  }
  if (attr.dirty == 0) {
    attr.dirty = 1;
    if (write_attr(get_attr_path(path).c_str(), &attr) == -1) {
      return -EIO;
    }
  }

  check_crash(CRASH_TRUNCATE);

  //just pass it down to the underlying cached file.
  int res;
  if (fi == NULL) {
    res = truncate(get_cache_path(path).c_str(), size);
  }
  else {
    res = ftruncate(fi->fh, size);
  }
  if (res == -1) {
    return -errno;
  }

  return 0;
}

static int afs_write(const char *path, 
                     const char *buf, 
                     size_t size,
                     off_t offset, 
                     struct fuse_file_info *fi) {

  fprintf(log_file, "afs_write\n");

  struct attr attr;
  if (read_attr(get_attr_path(path).c_str(), &attr) == -1) {
    return -EIO;
  }

  if (!fi) {
    return -EBADF;
  }

  //sett dirty flag before actually doing the write in case of crash
  if (attr.dirty == 0) {
    attr.dirty = 1;
    if (write_attr(get_attr_path(path).c_str(), &attr) == -1) {
      return -EIO;
    }
  }

  check_crash(CRASH_WRITE);
  
  //just pass it down to the underlying cached file.
  int res = pwrite(fi->fh, buf, size, offset);
  if (res == -1) {
    return -errno;
  }

  return res;
}



static int afs_rmdir(const char *path) {
     printf("\n afs_rmdir %s \n", path);
 // delete on the server and local if empty, else return error to the end user 
    int res = afs_client->RmDir(path);
    check_crash(CRASH_RMDIR);
    if (res == 0) {                                                                                                                                                        
        res = rmdir(path);                                                                                                                      
    }
    return res;
}

static int afs_mkdir(const char *path, mode_t mode) {
     printf("\n afs_mkdir %s \n", path); 
//mkdir on server. If error, return it to the end user
    int res = afs_client->MkDir(path, mode);
    return res; 
}


static int afs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct timespec times;
	int file_existence = 0;
  	fprintf(log_file, "afs_create\n");

  	// creating directory structure
	string temp = get_cache_path(path);
	char * cstr = new char [temp.length()+1];
    std::strcpy (cstr, temp.c_str());
    mkpath(cstr, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	// send Create RPC to server
	int ret = afs_client->Create(path, mode, fi->flags, &times, file_existence);
  	if (ret < 0)
    		return ret;

  	// check from server reply if the file already existed on the server before
  	// If yes, call Fetch to fetch it from the server
  	if (file_existence == 1) {
  	 	printf("Fetching from the server\n");
  	    string data;
  	    struct attr st;
  	    int res = afs_client->Fetch(path, data , &times);
  		if(res < 0)
  	      	return res;
  	    save_file(path, data); //using fetched data from the server, create a file and fill the data
  	}
  	check_crash(CRASH_CREATE_AFTER_REMOTE);

  	// creating/opening file locally on client
  	string loc_path = get_cache_path(path);
	ret = open(loc_path.c_str(), fi->flags, mode);
	if (ret == -1)
		return -errno;

	//modifying attr file
	struct attr file_attr;
	file_attr.dirty = 0;
	file_attr.server_mtime = times;
	string attr_path = get_attr_path(path);
	int res = write_attr(attr_path.c_str(), &file_attr);
	if (res == -1) {
		return -EIO;
		close(ret);
	}

	fi->fh = ret;
	return 0;
}

static int afs_unlink(const char *path)
{
	// send Unlink RPC to server
	int ret = afs_client->Unlink(path);
	if (ret == -1)
		return -errno;

	check_crash(CRASH_UNLINK);

	// deleting file locally
	string loc_path = get_cache_path(path);
	ret = unlink(loc_path.c_str());
	if (ret == -1)
		return -errno;

	// deleting associated attr file
	string attr_path = get_attr_path(path);
	ret = unlink(attr_path.c_str());
	if (ret == -1)
		return -errno;

	return 0;
}

static int afs_read(const char *path,
                    char *buf,
                    size_t size,
                    off_t offset,
		            struct fuse_file_info *fi)
{
  if (!fi) {
	return -EBADF;
	}

	int res = pread(fi->fh, buf, size, offset);
	if (res == -1) {
	return -errno;
	}

	return res;
}

static off_t afs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi) {
  if (!fi) {
    return -EBADF;
  }

  int res = lseek(fi->fh, off, whence);
  if (res == -1) {
    return -errno;
  }

  return res;
}
static const struct fuse_operations operations = {
  .getattr  = afs_getattr,
  .mkdir    = afs_mkdir,
  .unlink   = afs_unlink,
  .rmdir    = afs_rmdir,
  .truncate = afs_truncate,
  .open     = afs_open,
  .read     = afs_read,
  .write    = afs_write,
  .flush    = afs_flush,
  .release  = afs_release,
  .fsync    = afs_fsync,
  .init     = afs_init,
  .create   = afs_create,
  .lseek    = afs_lseek,
};

void InitRPC(const char* serverhost) {
    if (afs_client == NULL) {
        afs_client = new AfsClient(grpc::CreateChannel(serverhost, grpc::InsecureChannelCredentials()));
    }
}

int main(int argc, char **argv) {
  if (argc != 5 && argc != 4) {
    printf("Usage: %s <cache_dir> <mount_dir> -d [crash_file]\n", argv[0]);
    return 0;
  }

  if (argc == 5 && !CHECK_CRASH) {
    printf("====WARNING==== crash_file given but CHECK_CRASH not set.\n  Will not crash.\n");
  }
  if (argc == 4 && CHECK_CRASH) {
    printf("====ERROR==== CHECK_CRASH set, but no crash_file given\n");
    return 1;
  }
  if (argc == 5) {
    crash_filename = string(getenv("PWD")) + "/" + argv[4];
  }

  string filepath;
  string pwd(getenv("PWD"));
  string cache_path(argv[1]);

  cache_dir = pwd+"/"+cache_path;
  set_cache_path(cache_dir);

  log_file = fopen((pwd+"/afs_log").c_str(), "w+");
  if (!log_file) {
    perror("fopen: failed");
    exit(1);
  }
  fprintf(log_file, "start\n");
  const char* server = "localhost:50051";      
  InitRPC(server);
  char* new_argv[] = {argv[0], argv[2],argv[3]};
  afs_client->SayHello("world");
  return fuse_main(3, new_argv, &operations, NULL);
}
