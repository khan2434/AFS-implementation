**Background**

This is an AFS1-like implementation of distributed file system. This filesystem matches the POSIX semantics and implements open(), close(), creat(), unlink(), mkdir(), rmdir(), read(), write(), pread(), pwrite(), stat(), fsync(), and truncate().

The basic structure is a single server process communicating with multiple client FUSE processes. 
Each client FUSE caches entire files on open() and only flushes on close(). Writes to the server follows last-writer-wins policy, so the server copy of the file will entirely contain one client copy, not a mix.

On every call to open(), GetAttr() is called to fetch the last modified time of the file. If it's not modified, it means client cached copy can be used for reading and writing. If it is modified, then a new updated copy is fetched from the server.
Each file in the client has a corresponding attribute file which keeps track of the modifications on both client and server side. If the file is modified on the client, it is flushed to the server on close(). Otherwise it is not.
Everytime close() is called, file is flushed to the server and the server sends modified time as an acknowledgement which client writes to Attr file.

**Compile and Run**            

fuse installation - https://github.com/libfuse/libfuse        
GRPC installation - https://grpc.io/docs/languages/cpp/quickstart/       


Go the src/ folder,

**Build steps**
$ cmake -S . -B output
$ cd output/
$ make

**To start the client**
./afs_client <cache_dir> <mount_dir> -d <crash_file>

**To start the server**
./afs_server <server_dir> <crash_file>
