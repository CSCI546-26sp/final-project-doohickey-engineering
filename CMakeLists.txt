cmake_minimum_required(VERSION 3.15)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(distributed_log)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.3)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(gRPC GIT_REPOSITORY https://github.com/grpc/grpc.git GIT_TAG v1.62.0 GIT_SUBMODULES_RECURSE ON)
set(FETCHCONTENT_QUIET OFF)
set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(gRPC)

set(_PROTOC $<TARGET_FILE:protoc>)
set(_GRPC_CPP $<TARGET_FILE:grpc_cpp_plugin>)
set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT
        ${PROTO_OUT}/locality_messaging.pb.cc
        ${PROTO_OUT}/locality_messaging.pb.h
        ${PROTO_OUT}/locality_messaging.grpc.pb.cc
        ${PROTO_OUT}/locality_messaging.grpc.pb.h
    COMMAND ${_PROTOC}
        --grpc_out=${PROTO_OUT}
        --cpp_out=${PROTO_OUT}
        --plugin=protoc-gen-grpc=${_GRPC_CPP}
        -I${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${CMAKE_CURRENT_SOURCE_DIR}/proto/locality_messaging.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/locality_messaging.proto
)

add_library(loglib
    src/lamport_clock.cpp
    src/log_store.cpp
    src/raft_node.cpp
    ${PROTO_OUT}/locality_messaging.pb.cc
    ${PROTO_OUT}/locality_messaging.grpc.pb.cc
)
target_include_directories(loglib PUBLIC include ${PROTO_OUT})
target_link_libraries(loglib PUBLIC grpc++ libprotobuf)

add_executable(log_server src/log_server.cpp)
target_include_directories(log_server PRIVATE include ${PROTO_OUT})
target_link_libraries(log_server PRIVATE loglib)

add_executable(fake_auth_server src/fake_auth_server.cpp)
target_include_directories(fake_auth_server PRIVATE include ${PROTO_OUT})
target_link_libraries(fake_auth_server PRIVATE loglib)

add_executable(log_client src/log_client.cpp)
target_include_directories(log_client PRIVATE include ${PROTO_OUT})
target_link_libraries(log_client PRIVATE loglib)

add_executable(raft_server src/raft_server.cpp)
target_include_directories(raft_server PRIVATE include ${PROTO_OUT})
target_link_libraries(raft_server PRIVATE loglib)

add_executable(raft_test_client src/raft_test_client.cpp)
target_include_directories(raft_test_client PRIVATE include ${PROTO_OUT})
target_link_libraries(raft_test_client PRIVATE loglib)
