#include<stdio.h>
#include <chrono>
#include<fcntl.h>
#include<errno.h>
#include <unistd.h>
#include <cstdlib>

int main(int argc, char **argv) {

/** Open a large file which is not cached,read a byte, write a byte to it and close from multiple clients
 **/

int fd = open("./tmp/afs/data/4G.txt", O_RDWR | O_CREAT);

auto start = std::chrono::high_resolution_clock::now();
char *buf = (char*)malloc(1);
pread(fd,buf,1,0);
pwrite(fd,buf,1,1);
close(fd);
auto stop = std::chrono::high_resolution_clock::now();
double duration = (std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count())/1000.0;
printf("time: %lf micro_s \n",duration);
return 0;
}
