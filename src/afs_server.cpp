#include <fcntl.h>
#include <iostream>
#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <grpc++/grpc++.h>

#include "test.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;
using grpc::ServerWriter;
using grpc::ServerReader;

using test::HelloRequest;
using test::HelloReply;
using test::GetAttrReq;
using test::GetAttrReply;
using test::FetchReq;
using test::FetchReply;
using test::Afs;
using test::StoreReq;
using test::StoreReply;
using test::MkDirReq;
using test::MkDirReply;
using test::RmDirReq;
using test::RmDirReply;
using test::CreateReq;
using test::CreateReply;
using test::UnlinkReq;
using test::UnlinkReply;

constexpr int CHECK_CRASH =  1;

std::string crash_filename;

int should_crash(int num) {
  FILE *file = fopen(crash_filename.c_str(), "r");
  if (file == NULL) {
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

enum crash_num {
  CRASH_GETATTR = 1,
  CRASH_START_FETCH = 2,
  CRASH_END_FETCH = 3,
  CRASH_START_STORE = 4,
  CRASH_END_STORE = 5,
  CRASH_START_MKDIR = 6,
  CRASH_END_MKDIR = 7,
  CRASH_START_RMDIR = 8,
  CRASH_END_RMDIR = 9,
  CRASH_START_CREATE = 10,
  CRASH_END_CREATE = 11,
  CRASH_START_UNLINK = 12,
  CRASH_END_UNLINK = 13,
  CRASH_FETCH_STREAMING = 14,
  CRASH_STORE_STREAMING = 15,
};

inline void check_crash(int num) {
  if (CHECK_CRASH && should_crash(num)) {
    kill(getpid(), SIGINT);
  }
}



// Logic and data behind the server's behavior.
class AfsServiceImpl final : public Afs::Service {
	public:
	const std::string path_prefix; 
        AfsServiceImpl(std::string server_dir): path_prefix(server_dir) {
 	 }

      Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
      std::string prefix("Hello ");
      reply->set_message(prefix + request->name());
      printf("Hello world\n");
      return Status::OK;
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

       /**
         * GetAttr
         */
        Status GetAttr(ServerContext* context, const GetAttrReq* request,
                GetAttrReply* reply) override {
            // default errno = 0
            
            check_crash(CRASH_GETATTR);

            reply->set_err(0);
            int res;
            struct stat stbuf;
            std::string path = path_prefix + request->path();
            printf("\n====GetAttr:==== %s \n", path.c_str());
            res = lstat(path.c_str(), &stbuf);
	    print_stat(&stbuf);
            if (res < 0) {
                reply->set_err(-errno);
                return Status::OK;
            }
            std::string buf;

            int stat_size = sizeof(struct stat);
            buf.resize(stat_size);

            assert(buf.size() == sizeof(struct stat));
            memcpy(&buf[0], &stbuf, buf.size());
            reply->set_buf(buf);
            reply->set_err(res);
            return Status::OK;
       }

       Status Fetch(ServerContext* context, const FetchReq* request,
                ServerWriter<FetchReply>* writer) override {
       
            check_crash(CRASH_START_FETCH);
            
            FetchReply* reply = new FetchReply();
            reply->set_num_bytes(0);
            int res;
            std::string path = path_prefix + request->path();
             printf("\n====Fetch:==== %s \n", path.c_str());

            int fd = open(path.c_str(), O_RDONLY|O_CREAT);
	    perror("File Open ");
            if (fd == -1) {
                reply->set_num_bytes(-1);
                return Status::OK;
            }
            
	    struct stat statbuf;
	    lstat(path.c_str(), &statbuf);
	    reply->set_m_sec(statbuf.st_mtim.tv_sec);
            reply->set_m_nsec(statbuf.st_mtim.tv_nsec);
            
	    std::string buf;
            buf.resize(statbuf.st_size);
            int b = pread(fd, &buf[0], statbuf.st_size, 0);
	    perror("Pread ");
            if (b == -1) {
                reply->set_num_bytes(-errno);
            }

            close(fd);

            int remain = b;
            int stump = 1024*1000; // 1Mb
            int curr = 0;
            
            while (remain > 0) {
                reply->set_buf(buf.substr(curr, std::min(stump, remain)));
                reply->set_num_bytes(std::min(stump, remain));
//		printf("Sending %s   %d\n",path.c_str(),std::min(stump, remain));
                curr += stump;
                remain -= stump;
                writer->Write(*reply);
                check_crash(CRASH_FETCH_STREAMING);
            }

            check_crash(CRASH_END_FETCH);

            return Status::OK;
        }

    Status Store(ServerContext* context, ServerReader<StoreReq> *reader, StoreReply* reply) {

      check_crash(CRASH_START_STORE);      

      StoreReq request;
      int num_written = 0;
      int fd = -1;
      std::string filename = "";
      std::string tmp_filename = "";
      
      reply->set_err(0);

      while(reader->Read(&request)) {
        if (num_written == 0) {
          filename = path_prefix + request.path();
          //add peer uri so that the filename is unique for each client
          tmp_filename = filename + ".temp" + context->peer();
          fd = open(tmp_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
          
          printf("\n====Store:==== %s \n", filename.c_str());

          if (fd == -1) {
            perror("open");
            reply->set_err(errno);
            return Status::OK;
          }
        }
        int res = write(fd, request.buf().c_str(), request.buf().length());
        if (res == -1) {
          perror("write");
          close(fd);
          reply->set_err(errno);
          return Status::OK;
        }
        num_written += res;
        check_crash(CRASH_STORE_STREAMING);
      }

      if (fsync(fd) == -1) {
        perror("fsync");
        reply->set_err(errno);
        close(fd);
        return Status::OK;
      }
      if (close(fd) == -1) {
        perror("close");
        reply->set_err(errno);
        return Status::OK;
      }
      
      //get mtime before renaming in case another client is Storing at the same time.
      //rename doesn't change mtime
      struct stat statbuf;
      if (lstat(tmp_filename.c_str(), &statbuf) == -1) {
        perror("lstat");
        reply->set_err(errno);
        return Status::OK;
      }
      if (rename(tmp_filename.c_str(), filename.c_str()) == -1) {
        perror("rename");
        reply->set_err(errno);
        return Status::OK;
      }

      reply->set_m_sec(statbuf.st_mtim.tv_sec);
      reply->set_m_nsec(statbuf.st_mtim.tv_nsec);

      check_crash(CRASH_END_STORE);

      return Status::OK;
    }

        /**
         * MkDir
         */
        Status MkDir(ServerContext* context, const MkDirReq* request,
                MkDirReply* reply) override {

          check_crash(CRASH_START_MKDIR);

            reply->set_err(0);
            std::string path = path_prefix + request->path();
            printf("\n====Mkdir==== %s \n", path.c_str());
	    int res;

            res = mkdir(path.c_str(), request->mode());
	    printf("Mkdir res : %d\n",res);
            if (res == -1) {
                reply->set_err(-errno);
                return Status::OK;
            }
            reply->set_err(res);

          check_crash(CRASH_END_MKDIR);

            return Status::OK;
        }

        /**
         * RmDir
         */
        Status RmDir(ServerContext* context, const RmDirReq* request,
                RmDirReply* reply) override {

          check_crash(CRASH_START_RMDIR);

            reply->set_err(0);
            std::string path = path_prefix + request->path();
            printf("\n====Rmdir==== %s \n", path.c_str());
            int res;

            res = rmdir(path.c_str());
	     printf("Rmdir res : %d\n",res);
            if (res == -1) {
                reply->set_err(-errno);
                return Status::OK;
            }

            reply->set_err(res);

          check_crash(CRASH_END_RMDIR);

            return Status::OK;
        }

      Status Create(ServerContext* context, const CreateReq* request,
			   CreateReply* reply) override {

    	  check_crash(CRASH_START_CREATE);

		   // default errno = 0
		   reply->set_err(-errno);
		   std::string path = path_prefix + request->path();
		   printf("Create: %s \n", path.c_str());
		   int mode = request->mode();
		   int flags = request->flags();
		   int res;

		   if(access(path.c_str(), F_OK) == 0) {
		       // file exists, set existence to 1 and do not create a new file
			   reply->set_existence(1);
		   } else {
		       // file doesn't exist. Create a new file
			   res = open(path.c_str(), flags, mode);
			   if (res == -1) {
				   reply->set_err(-errno);
				   return Status::OK;
			   }
			   reply->set_err(res);
			   close(res);
		   }

		 struct stat statbuf;
		 if (lstat(path.c_str(), &statbuf) == -1) {
		   perror("lstat");
		   reply->set_err(errno);
		   return Status::OK;
		 }
		 //set mtime to send to client
		 reply->set_m_sec(statbuf.st_mtim.tv_sec);
		 reply->set_m_nsec(statbuf.st_mtim.tv_nsec);

		 check_crash(CRASH_END_CREATE);

		 return Status::OK;
	   }

       Status Unlink(ServerContext* context, const UnlinkReq* request,
			   UnlinkReply* reply) override {
		   // default errno = 0

        check_crash(CRASH_START_UNLINK);

		   reply->set_err(0);
		   std::string path = path_prefix + request->path();
		   printf("Unlink: %s \n", path.c_str());
		   int res;
		   res = unlink(path.c_str());
		   if (res == -1) {
			   reply->set_err(-errno);
			   return Status::OK;
		   }
		   reply->set_err(res);

        check_crash(CRASH_END_UNLINK);

		   return Status::OK;
	   }
};

void RunServer(std::string server_dir) {
  std::string server_address("localhost:50051");
  AfsServiceImpl service(server_dir);

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  if (argc != 3 && argc != 2) {
    printf("Usage: %s <server_dir> [crash_file]\n", argv[0]);
    return 0;
  }

  if (argc == 3 && !CHECK_CRASH) {
    printf("====WARNING==== crash_file given but CHECK_CRASH not set.\n  Will not crash.\n");
  }
  if (argc == 2 && CHECK_CRASH) {
    printf("====ERROR==== CHECK_CRASH set, but no crash_file given\n");
    return 1;
  }

  crash_filename = argv[2];

  RunServer(argv[1]);
  
  return 0;
}
