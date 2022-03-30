# Inspired by https://github.com/grpc/grpc/blob/master/examples/cpp/helloworld/CMakeLists.txt

cmake_minimum_required(VERSION 3.5.1)
project(CS739-AFS-FUSE C CXX)
set(CMAKE_CXX_STANDARD 11)

include(../common/common.cmake)


get_filename_component(hw_proto "../protos/test.proto" ABSOLUTE)
get_filename_component(hw_proto_path "${hw_proto}" PATH)

set(hw_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/test.pb.cc")
set(hw_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/test.pb.h")
set(hw_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/test.grpc.pb.cc")
set(hw_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/test.grpc.pb.h")

add_custom_command(
        OUTPUT "${hw_proto_srcs}" "${hw_proto_hdrs}" "${hw_grpc_srcs}" "${hw_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${hw_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${hw_proto}"
        DEPENDS "${hw_proto}")


include_directories("${CMAKE_CURRENT_BINARY_DIR}" "/users/Preeti/fuse-3.10.5/include")
add_library(hw_grpc_proto
        ${hw_grpc_srcs}
        ${hw_grpc_hdrs}
        ${hw_proto_srcs}
        ${hw_proto_hdrs})
target_link_libraries(hw_grpc_proto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})


foreach(_target afs_client afs_server)
    add_executable(${_target} "${_target}.cpp")
    target_link_libraries(${_target}
            "/users/Preeti/fuse-3.10.5/build/lib/libfuse3.so"
	    hw_grpc_proto
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF})
endforeach()

