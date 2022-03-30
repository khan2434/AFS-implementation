#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#include "test.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientWriter;
using grpc::Status;
using test::GetAttrReq;
using test::GetAttrReply;
using test::FetchReq;
using test::FetchReply;
using test::StoreReq;
using test::StoreReply;
using test::MkDirReq;
using test::MkDirReply;
using test::RmDirReq;
using test::RmDirReply;
using test::UnlinkReq;
using test::UnlinkReply;
using test::CreateReq;
using test::CreateReply;
using test::HelloRequest;
using test::HelloReply;

class AfsClient {
	public:
    AfsClient(std::shared_ptr<Channel> channel)
        : stub_(test::Afs::NewStub(channel)),
          channel_(channel){}

    // Just a simple message to check the communication channel
    std::string SayHello(const std::string& user) {
    	HelloRequest request;
    	request.set_name(user);
    	HelloReply reply;
    	ClientContext context;
    	Status status = stub_->SayHello(&context, request, &reply);
    	if (status.ok()) {
      		return reply.message();
    	} else {
      		return "RPC failed";
    	}
    }

    // get fstat from the server
    int GetAttr(std::string path, std::string& buf) {

        while (1) {
          GetAttrReq request;
        request.set_path(path);

        GetAttrReply reply;
        ClientContext context;
          auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
          context.set_deadline(deadline);
          Status status = stub_->GetAttr(&context, request, &reply);
          if (status.ok()) {
              buf = reply.buf();
              return reply.err();
          }
          else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            int connected = 0;
            while (!connected) {
              auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
              connected = channel_->WaitForConnected(deadline);
            }
          }
          else {
              printf("%d %d\n", channel_->GetState(true), grpc_connectivity_state::GRPC_CHANNEL_TRANSIENT_FAILURE);
              return EIO;
          }
        }
    }

    // Unlink a file
    int Unlink(const std::string& path) {
        while (1) {
	    UnlinkReq request;
            request.set_path(path);

            UnlinkReply reply;
            ClientContext context;
            auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
            context.set_deadline(deadline);
            Status status = stub_->Unlink(&context, request, &reply);
        
	    if (status.ok()) {
              return reply.err();
            }

            else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
                int connected = 0;
               while (!connected) {
              auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
              connected = channel_->WaitForConnected(deadline);
            }
           }
            else {
              printf("%d %d\n", channel_->GetState(true), grpc_connectivity_state::GRPC_CHANNEL_TRANSIENT_FAILURE);
              return EIO;
            }
        }
    } 
    
    // Create a file
    int Create(const std::string& path, int mode, int flags, struct timespec *st_mtim, int file_existence) {
    	while (1) {
	    CreateReq request;
		request.set_path(path);
		request.set_mode(mode);
		request.set_flags(flags);

		CreateReply reply;
		ClientContext context;
		auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
		context.set_deadline(deadline);
		Status status = stub_->Create(&context, request, &reply);

		st_mtim->tv_sec = reply.m_sec();
		st_mtim->tv_nsec = reply.m_nsec();

		// existence checks if the file already exists on the server or not
		file_existence = reply.existence();

	    if (status.ok()) {
              return reply.err();
            }

            else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
                int connected = 0;
                while (!connected) {
                  auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
                  connected = channel_->WaitForConnected(deadline);
                }
            }
            else {
              printf("%d %d\n", channel_->GetState(true), grpc_connectivity_state::GRPC_CHANNEL_TRANSIENT_FAILURE);
              return EIO;
            }

	}
    }

    // Fetch a file 
    int Fetch(const std::string path, std::string& buf,struct timespec *st_mtim) {
      while(1) {
    	FetchReq request;
    	request.set_path(path);

    	FetchReply reply;
    	ClientContext context;
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(10000);
        context.set_deadline(deadline);

    	std::unique_ptr<ClientReader<FetchReply> > reader(stub_->Fetch(&context, request));
        buf = std::string();
        //buf.reserve(size);

    	while (reader->Read(&reply)) {
        check_crash(CRASH_FETCH);
            buf += reply.buf();
	    if (reply.num_bytes() < 0) {
                break;
            }
        }
        Status status = reader->Finish();
        if (status.ok()) {
           printf("Client received fetch data: %s\n",buf.c_str());
	   st_mtim->tv_sec = reply.m_sec();
           st_mtim->tv_nsec = reply.m_nsec();
           return reply.err();
        }
        else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
          int connected = 0;
          while (!connected) {
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
            connected = channel_->WaitForConnected(deadline);
          }
        }
        else {
          return EIO;
        }

      }
  }

    // Store a file
    int Store(const std::string& path, int fd, struct timespec *st_mtim) {
      while (1) {
        StoreReq request;
        StoreReply reply;
        ClientContext context;
        //should deadline be updated every iteration of loop?
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(1000);
        context.set_deadline(deadline);
        std::unique_ptr<ClientWriter<StoreReq> > writer(stub_->Store(&context, &reply));

        int remain = lseek64(fd, 0, SEEK_END);
        int stump = 1024*1000; //1 Mb

        std::string buf;
        buf.resize(stump);
        lseek64(fd, 0, SEEK_SET);

        while (remain > 0) {
          request.set_path(path); 
          int size = read(fd, &(buf[0]), stump);
          if (size == -1) {
            perror("read");
            return -1;
          }
          if (size == stump) {
            request.set_buf(buf);
          }
          else {
            buf.resize(size);
            request.set_buf(buf);
          }
          remain -= stump;
          if(!writer->Write(request)) {
            //Broken stream
            break;
          }
          check_crash(CRASH_STORE);
        }

        writer->WritesDone();
        
        Status status = writer->Finish();
        if (status.ok()) {
          st_mtim->tv_sec = reply.m_sec();
          st_mtim->tv_nsec = reply.m_nsec();
          return reply.err();
        }
        else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
          int connected = 0;
          while (!connected) {
            auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
            connected = channel_->WaitForConnected(deadline);
          }
        }
        else {
          return EIO;
        }
      }
    }

    // Make directory with the given mode
    int MkDir(const std::string& path, int mode) {
       while(1){
        MkDirReq request;
        request.set_path(path);
        request.set_mode(mode);

        MkDirReply reply;
        ClientContext context;
	auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
        context.set_deadline(deadline);
        Status status = stub_->MkDir(&context, request, &reply);
	if (status.ok()) {
              return reply.err();
        }

	else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            int connected = 0;
            while (!connected) {
              auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
              connected = channel_->WaitForConnected(deadline);
            }
          }
        else {
              printf("%d %d\n", channel_->GetState(true), grpc_connectivity_state::GRPC_CHANNEL_TRANSIENT_FAILURE);
              return EIO;
        }
      }
    }

    // Remove directory
    int RmDir(const std::string& path) {
      while(1) {
        RmDirReq request;
        request.set_path(path);

        RmDirReply reply;
        ClientContext context;
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
        context.set_deadline(deadline);
        Status status = stub_->RmDir(&context, request, &reply);
        if (status.ok()) {
              return reply.err();
        }
        else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            int connected = 0;
            while (!connected) {
              auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
              connected = channel_->WaitForConnected(deadline);
            }
        }
        else {
              printf("%d %d\n", channel_->GetState(true), grpc_connectivity_state::GRPC_CHANNEL_TRANSIENT_FAILURE);
              return EIO;
        }
      }
    }

    private:
    std::unique_ptr<test::Afs::Stub> stub_;
    std::shared_ptr<Channel> channel_;

};
