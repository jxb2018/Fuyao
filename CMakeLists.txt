cmake_minimum_required(VERSION 3.16)
project(Fuyao)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-fPIC)
add_compile_options(-fexceptions)

# add_compile_options(-D__DEBUG)
add_compile_options(-D__ENABLE_RDMA)

set(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(FUNC_WORKER_CPP_LIBRARIES_NAME worker_cpp)

# header
include_directories(${BASE_DIR}/lib)
include_directories(${BASE_DIR}/src)

# deps
add_subdirectory(${BASE_DIR}/deps/abseil-cpp abseil)
add_subdirectory(${BASE_DIR}/deps/fmt fmt)
add_subdirectory(${BASE_DIR}/deps/GSL gsl)
add_subdirectory(${BASE_DIR}/deps/json json)
add_subdirectory(${BASE_DIR}/deps/libuv libuv)
add_subdirectory(${BASE_DIR}/deps/nghttp2 nghttp2)
add_subdirectory(${BASE_DIR}/deps/pybind11 pybind11)

# rdma verbs
set(ENV{PKG_CONFIG_PATH} /usr/lib/x86_64-linux-gnu/pkgconfig)
set(ENV{PKG_CONFIG_EXECUTABLE} /usr/bin/pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIBVERBS REQUIRED IMPORTED_TARGET libibverbs)

# dpu submodule
add_subdirectory(${BASE_DIR}/src/dpu/gateway gateway)
add_subdirectory(${BASE_DIR}/src/dpu/agent agent)

# host submodule
add_subdirectory(${BASE_DIR}/src/host/engine engine)
add_subdirectory(${BASE_DIR}/src/host/launcher launcher)

# cpp runtime library
add_subdirectory(${BASE_DIR}/src/host/worker/cpp/ worker_cpp)

# python runtime library
# Replace it based on the actual situation
set(PYTHON_INCLUDE_DIR /usr/include/python3.8)
set(LIBRARY_OUTPUT_PATH ${BASE_DIR}/src/host/worker/python/faas)
set(FUNC_WORKER_PYTHON_OUTPUT_NAME _faas_native)

add_subdirectory(${BASE_DIR}/src/host/worker/python/ worker_python)

# examples
add_subdirectory(${BASE_DIR}/examples examples)