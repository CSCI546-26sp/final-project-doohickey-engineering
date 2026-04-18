cmake_minimum_required(VERSION 3.15)
project(distributed_log LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf REQUIRED)

# Use system gRPC
set(gRPC_DIR "/usr/lib/aarch64-linux-gnu/cmake/grpc")
find_package(gRPC CONFIG REQUIRED)

# Force system tools
set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)
set(GRPC_CPP_PLUGIN_EXECUTABLE /usr/bin/grpc_cpp_plugin)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT
        ${GEN_DIR}/locality_messaging.pb.cc
        ${GEN_DIR}/locality_messaging.pb.h
        ${GEN_DIR}/locality_messaging.grpc.pb.cc
        ${GEN_DIR}/locality_messaging.grpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --grpc_out=${GEN_DIR}
        --cpp_out=${GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
        -I ${PROTO_DIR}
        ${PROTO_DIR}/locality_messaging.proto
    DEPENDS ${PROTO_DIR}/locality_messaging.proto
    VERBATIM
)

add_library(proto_generated
    ${GEN_DIR}/locality_messaging.pb.cc
    ${GEN_DIR}/locality_messaging.grpc.pb.cc
)

target_include_directories(proto_generated PUBLIC ${GEN_DIR})
target_link_libraries(proto_generated PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_library(loglib
    src/lamport_clock.cpp
    src/log_store.cpp
)

target_include_directories(loglib PUBLIC include ${GEN_DIR})
target_link_libraries(loglib PUBLIC proto_generated ZLIB::ZLIB Threads::Threads)

add_executable(log_server src/log_server.cpp)
target_include_directories(log_server PRIVATE include ${GEN_DIR})
target_link_libraries(log_server PRIVATE loglib)

add_executable(fake_auth_server src/fake_auth_server.cpp)
target_include_directories(fake_auth_server PRIVATE include ${GEN_DIR})
target_link_libraries(fake_auth_server PRIVATE loglib)

add_executable(log_client src/log_client.cpp)
target_include_directories(log_client PRIVATE include ${GEN_DIR})
target_link_libraries(log_client PRIVATE loglib)