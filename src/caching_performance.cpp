#include<stdio.h>
#include <chrono>
#include<fcntl.h> 
#include<errno.h> 
#include <unistd.h>
#include <cstdlib>

void helper(const char* filename) {
	//first access
	auto start = std::chrono::high_resolution_clock::now();
	int fd = open(filename, O_RDWR | O_CREAT); 
	auto stop = std::chrono::high_resolution_clock::now();
	close(fd);
        double first_access = (std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count())/1000.0;
        double total = 0;
	//subsequent accesses
	for(int i = 0; i < 10; i++) {
		start = std::chrono::high_resolution_clock::now();
		fd = open(filename, O_RDWR | O_CREAT);
		stop = std::chrono::high_resolution_clock::now();
		close(fd);
        	total  += std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
             
        }
	printf(" File :  %s  First Access : %lf micro_s Subsequent Access Average: %lf micro_s \n",filename,first_access,total/10000.0);
}

void helper_rw(const char* filename) {
/** How does write performance differ from reading performance?
1) Read 4MB data from a file and close it
2) Write 4MB data to a file and close it
**/
int fd;
char *buf = (char*)malloc(10);

auto start = std::chrono::high_resolution_clock::now();
fd = open(filename, O_RDONLY | O_CREAT);
int bytes = pread(fd,buf,10,0);
close(fd);
auto stop = std::chrono::high_resolution_clock::now();
double duration = (std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count())/1000.0;
printf("Read file: %s bytes: %d time: %lf micro_s \n",filename,bytes,duration);

start = std::chrono::high_resolution_clock::now();
fd = open(filename, O_WRONLY | O_CREAT);
bytes = pwrite(fd,buf,10,0);
close(fd);
stop = std::chrono::high_resolution_clock::now();
duration = (std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count())/1000.0;
printf("Write file: %s bytes: %d time: %lf micro_s \n",filename,bytes,duration);

}
int main(int argc, char **argv) {
//dummy
int fd = open("./tmp/afs/data/hello.txt", O_RDWR | O_CREAT);

/** First open() and subsequent open() by increasing file sizes **/
helper("./tmp/afs/data/1K.txt");
helper("./tmp/afs/data/5K.txt");
helper("./tmp/afs/data/10K.txt");
helper("./tmp/afs/data/1M.txt");
helper("./tmp/afs/data/2M.txt");
helper("./tmp/afs/data/4M.txt");
helper("./tmp/afs/data/6M.txt");
helper("./tmp/afs/data/8M.txt");
helper("./tmp/afs/data/10M.txt");
helper("./tmp/afs/data/1G.txt");


helper_rw("./tmp/afs/rwdata/1K.txt");
helper_rw("./tmp/afs/rwdata/2K.txt");
helper_rw("./tmp/afs/rwdata/4K.txt");
helper_rw("./tmp/afs/rwdata/8K.txt");
helper_rw("./tmp/afs/rwdata/16K.txt");
helper_rw("./tmp/afs/rwdata/32K.txt");
helper_rw("./tmp/afs/rwdata/64K.txt");
helper_rw("./tmp/afs/rwdata/128K.txt");
helper_rw("./tmp/afs/rwdata/256K.txt");
helper_rw("./tmp/afs/rwdata/512K.txt");
helper_rw("./tmp/afs/rwdata/1M.txt");
helper_rw("./tmp/afs/rwdata/2M.txt");
helper_rw("./tmp/afs/rwdata/4M.txt");
return 0;
}
