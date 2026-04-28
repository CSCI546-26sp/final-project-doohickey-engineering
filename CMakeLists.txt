cmake_minimum_required(VERSION 3.15)
project(distributed_log LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Catch2 REQUIRED)
find_package(GTest REQUIRED)

pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF_PC REQUIRED IMPORTED_TARGET protobuf)

# Tools
set(Protobuf_PROTOC_EXECUTABLE /usr/bin/protoc)
set(GRPC_CPP_PLUGIN_EXECUTABLE /usr/bin/grpc_cpp_plugin)

# Proto generation
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT
        ${GEN_DIR}/locality_messaging.pb.cc
        ${GEN_DIR}/locality_messaging.grpc.pb.cc
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --grpc_out=${GEN_DIR}
        --cpp_out=${GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
        -I ${PROTO_DIR}
        ${PROTO_DIR}/locality_messaging.proto
    DEPENDS ${PROTO_DIR}/locality_messaging.proto
)

add_library(proto_generated
    ${GEN_DIR}/locality_messaging.pb.cc
    ${GEN_DIR}/locality_messaging.grpc.pb.cc
)

target_include_directories(proto_generated PUBLIC ${GEN_DIR})
target_link_libraries(proto_generated PUBLIC PkgConfig::GRPCPP PkgConfig::PROTOBUF_PC)

# Core library
add_library(loglib
    src/lamport_clock.cpp
    src/log_store.cpp
    src/data_plane_service.cpp
)

target_include_directories(loglib PUBLIC include ${GEN_DIR})
target_link_libraries(loglib PUBLIC proto_generated ZLIB::ZLIB Threads::Threads)

# Servers/clients
add_executable(log_server src/log_server.cpp)
target_link_libraries(log_server PRIVATE loglib)

add_executable(fake_auth_server src/fake_auth_server.cpp)
target_link_libraries(fake_auth_server PRIVATE loglib)

add_executable(log_client src/log_client.cpp)
target_link_libraries(log_client PRIVATE loglib)

# Raft
add_library(raftlib src/raft_node.cpp)
target_include_directories(raftlib PUBLIC include ${GEN_DIR})
target_link_libraries(raftlib PUBLIC proto_generated Threads::Threads)

target_include_directories(raftlib PUBLIC include ${GEN_DIR})

target_link_libraries(raftlib
    PUBLIC
    loglib              
    proto_generated
    Threads::Threads
)

add_executable(raft_server src/raft_server.cpp)
target_link_libraries(raft_server PRIVATE raftlib)

add_executable(partition_sim src/partition_sim.cpp)
target_include_directories(partition_sim PRIVATE include)
target_link_libraries(partition_sim PRIVATE loglib)

add_executable(latency_bench src/latency_bench.cpp)
target_include_directories(latency_bench PRIVATE include)
target_link_libraries(latency_bench PRIVATE loglib)

add_executable(crash_sim src/crash_sim.cpp)
target_include_directories(crash_sim PRIVATE include)
target_link_libraries(crash_sim PRIVATE loglib)

add_executable(correctness_eval src/correctness_eval.cpp)
target_include_directories(correctness_eval PRIVATE include)
target_link_libraries(correctness_eval PRIVATE loglib)

# ----------------------------
# TESTS (AUTO)
# ----------------------------
#
#add_executable(all_tests
#    tests/test_main.cpp
#    tests/test_log_store.cpp
#    tests/tests_lamport_clock.cpp
#    tests/test_grpc_integration.cpp
#)

#target_include_directories(all_tests PRIVATE include ${GEN_DIR})

#target_link_libraries(all_tests
#    PRIVATE
#    loglib
#    proto_generated
#    Threads::Threads
#    Catch2::Catch2
#)

#add_test(NAME all_tests COMMAND all_tests)

file(GLOB TEST_SOURCES tests/*.cpp)

set(CATCH_TEST_SOURCES "")

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    # skip standalone programs (they have their own main)
    if(test_name STREQUAL "test_raft_rpcs" OR
       test_name STREQUAL "test_raft_crash" OR
       test_name STREQUAL "test_full_system")
        continue()
    endif()

    # skip the main file (we add it explicitly)
    if(test_name STREQUAL "test_main")
        continue()
    endif()

    list(APPEND CATCH_TEST_SOURCES ${test_src})
endforeach()

# Build unified test binary
add_executable(all_tests
    tests/test_main.cpp      # 🔥 explicitly include main
    ${CATCH_TEST_SOURCES}
)

target_include_directories(all_tests PRIVATE include ${GEN_DIR})

target_link_libraries(all_tests
    PRIVATE
    loglib
    proto_generated
    Threads::Threads
    Catch2::Catch2
)

add_executable(test_raft_rpcs tests/test_raft_rpcs.cpp)
target_link_libraries(test_raft_rpcs PRIVATE loglib)

add_executable(test_raft_crash tests/test_raft_crash.cpp)
target_link_libraries(test_raft_crash PRIVATE loglib)

add_executable(full_system_test tests/test_main.cpp tests/test_full_system.cpp)

target_link_libraries(full_system_test
    PRIVATE
    raftlib
    loglib
    proto_generated
    Threads::Threads
    Catch2::Catch2
)

add_test(NAME all_tests COMMAND all_tests)
add_test(NAME raft_rpcs
         COMMAND test_raft_rpcs localhost:50053 localhost:50054 localhost:50055)
add_test(NAME raft_crash
         COMMAND test_raft_crash)
add_test(NAME full_system COMMAND full_system_test)