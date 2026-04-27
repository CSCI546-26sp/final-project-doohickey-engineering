cmake_minimum_required(VERSION 3.15)
project(distributed_log LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Some environments inject ~/.local/include via transitive package dependencies.
# Force system headers to be searched first so protobuf/grpc headers stay consistent.
add_compile_options(-I/usr/include)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF_PC REQUIRED IMPORTED_TARGET protobuf)

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

target_include_directories(proto_generated SYSTEM BEFORE PUBLIC /usr/include)
target_include_directories(proto_generated PUBLIC ${GEN_DIR})
target_link_libraries(proto_generated PUBLIC PkgConfig::GRPCPP PkgConfig::PROTOBUF_PC)

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

add_library(raftlib
    src/raft_node.cpp
)
target_include_directories(raftlib PUBLIC include ${GEN_DIR})
target_link_libraries(raftlib PUBLIC proto_generated Threads::Threads)

add_executable(raft_server src/raft_server.cpp)
target_include_directories(raft_server PRIVATE include ${GEN_DIR})
target_link_libraries(raft_server PRIVATE raftlib)

add_executable(test_raft_rpcs tests/test_raft_rpcs.cpp)
target_include_directories(test_raft_rpcs PRIVATE include ${GEN_DIR})
target_link_libraries(test_raft_rpcs PRIVATE proto_generated Threads::Threads)