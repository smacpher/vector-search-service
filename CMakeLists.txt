cmake_minimum_required(VERSION 3.26)

project(vector_search)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# protobuf
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")
# include_directories(${Protobuf_INCLUDE_DIR})

set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

# grpc
find_package(absl CONFIG REQUIRED)
message(STATUS "Using absl ${absl_VERSION}")

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using grpc ${gRPC_VERSION}")
set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
set(_GRPC_PY_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_python_plugin>)

# openmp
find_package(OpenMP REQUIRED)

# faiss
find_package(faiss CONFIG REQUIRED)
message(STATUS "Using faiss ${faiss_VERSION}")

# googletest
include(FetchContent)
FetchContent_Declare(
     googletest
     URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# protos
get_filename_component(proto_include_path "src/proto" ABSOLUTE)
get_filename_component(index_service_proto "src/proto/index_service.proto" ABSOLUTE)

set(_PROTOS_DIR "${CMAKE_CURRENT_BINARY_DIR}/src/proto")
file(MAKE_DIRECTORY "${_PROTOS_CPP_DIR}")

# generated proto sources
set(index_service_proto_srcs "${_PROTOS_DIR}/index_service.pb.cc")
set(index_service_proto_hdrs "${_PROTOS_DIR}/index_service.pb.h")
set(index_service_grpc_srcs "${_PROTOS_DIR}/index_service.grpc.pb.cc")
set(index_service_grpc_hdrs "${_PROTOS_DIR}/index_service.grpc.pb.h")
add_custom_command(
      OUTPUT
        "${index_service_proto_srcs}" "${index_service_proto_hdrs}"
        "${index_service_grpc_srcs}" "${index_service_grpc_srcs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${_PROTOS_DIR}"
        --cpp_out "${_PROTOS_DIR}"
        -I "${proto_include_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        --plugin=protoc-gen-grpc_python="${_GRPC_PY_PLUGIN_EXECUTABLE}"
        "${index_service_proto}"
      DEPENDS "${index_service_proto}")

# include generated proto sources
# TODO: replace this with `target_include_directories`
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# cpp
set(_CPP_DIR "src/cpp")

add_executable(
        faiss_index_service
        "${_CPP_DIR}/faiss_index_service_main.cc"
        "${_CPP_DIR}/faiss_index_service.cc"
        ${index_service_proto_srcs} ${index_service_grpc_srcs}
)
target_link_libraries(faiss_index_service ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF} faiss OpenMP::OpenMP_CXX absl::flags absl::log absl::strings)

add_executable(
        sharded_index_service 
        "${_CPP_DIR}/sharded_index_service_main.cc"
        "${_CPP_DIR}/sharded_index_service.cc"
        ${index_service_proto_srcs} ${index_service_grpc_srcs}
)
target_link_libraries(sharded_index_service ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF} absl::flags absl::log absl::strings)

add_executable(
        playground
        "playground.cpp"
        ${index_service_proto_srcs} ${index_service_grpc_srcs}
)
target_link_libraries(playground ${_REFLECTION} ${_GRPC_GRPCPP} ${_PROTOBUF_LIBPROTOBUF})

# unittests
enable_testing()
add_executable(algo_test "${_CPP_DIR}/algo_test.cc")
target_link_libraries(algo_test GTest::gtest_main GTest::gmock_main)

include(GoogleTest)
gtest_discover_tests(algo_test)

