cmake_minimum_required(VERSION 3.25)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(messenger)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(CMAKE_EXPORT_COMPILE_COMMANDS AND UNIX)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

option(USE_SANITIZERS "Enable Address and UndefinedBehavior sanitizers" OFF)
if(USE_SANITIZERS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

option(BUILD_TESTS "Build all unit tests" OFF)
option(BUILD_TESTS_SANITIZED "Build all unit tests with sanitizers" OFF)

if(CMAKE_BUILD_TYPE MATCHES "Release")
    cmake_policy(SET CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

set(PROTO_SRC "${CMAKE_SOURCE_DIR}/proto/messenger.proto")
set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

add_custom_command(
    OUTPUT
    ${PROTO_GEN_DIR}/messenger.pb.cc
    ${PROTO_GEN_DIR}/messenger.pb.h
    ${PROTO_GEN_DIR}/messenger.grpc.pb.cc
    ${PROTO_GEN_DIR}/messenger.grpc.pb.h
    COMMAND protoc
    ARGS
    --cpp_out=${PROTO_GEN_DIR}
    --grpc_out=${PROTO_GEN_DIR}
    --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin
    -I${CMAKE_SOURCE_DIR}/proto
    ${PROTO_SRC}
    DEPENDS ${PROTO_SRC}
)

add_library(messenger_proto STATIC
    ${PROTO_GEN_DIR}/messenger.pb.cc
    ${PROTO_GEN_DIR}/messenger.grpc.pb.cc
)
target_include_directories(messenger_proto PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(messenger_proto PUBLIC protobuf grpc++ grpc gpr absl_synchronization)

add_executable(messenger_server server/main.cpp)
target_link_libraries(messenger_server PRIVATE messenger_proto)

add_executable(messenger_client client/main.cpp)
target_link_libraries(messenger_client PRIVATE messenger_proto Threads::Threads nlohmann_json::nlohmann_json)

if(BUILD_TESTS OR BUILD_TESTS_SANITIZED)
    enable_testing()
    add_subdirectory(tests)
endif()

find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES "server/*.cpp" "client/*.cpp" "tests/*.cpp" "server/*.hpp" "client/*.hpp" "tests/*.hpp")
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format on all C++ source files..."
    )
endif()
