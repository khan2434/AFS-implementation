#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
using namespace std;

string cache_dir;
string attr_sufix = ".attr";
string file_sufix = ".local";

string get_cache_path(const char* path) {
	return cache_dir + path + file_sufix;
}
string get_attr_path (const char* path) {
        return cache_dir + path + attr_sufix;
}

void set_cache_path(string& path) {
	cache_dir = path;
}

int exists(string path) {
   return access(path.c_str(), F_OK ) != -1;
}

void get_local_attr(string path, struct stat* st) {
  lstat(path.c_str(), st);	
}

void save_file(string path, string& data) {
	printf("Saving file : %s\n",path.c_str());
	printf("Data : %s \n",data.c_str());
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
	if ((fd == -1) && (EEXIST == errno)) {
            /* open the existing file with truncate flag */
            fd = open(path.c_str(), O_TRUNC | O_WRONLY);
	}
	perror("Saving file error :  ");
	int res = pwrite(fd, &data[0], data.size(), 0);
	fsync(fd);
	close(fd);
}
