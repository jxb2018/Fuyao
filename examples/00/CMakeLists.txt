CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
project(exp00)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-D__WORKER_PYTHON_BUILD)

include_directories(utils/)

set(EXP00_RDMA_SERVER_NAME exp00_rdma_server)
set(EXP00_RDMA_CLIENT_NAME exp00_rdma_client)

set(EXP00_RDMA_SOURCE_FILES
        ${BASE_DIR}/lib/base/logging.cpp
        ${BASE_DIR}/lib/base/thread.cpp
        ${BASE_DIR}/lib/rdma/bit_map.cpp
        ${BASE_DIR}/lib/rdma/infinity.cpp
        ${BASE_DIR}/lib/rdma/queue_pair.cpp
        ${BASE_DIR}/lib/rdma/shared_memory.cpp
        rdma_rc.cpp)
set(EXP00_LIBRARIES fmt GSL absl::flags uv_a)

add_executable(${EXP00_RDMA_SERVER_NAME} ${EXP00_RDMA_SOURCE_FILES})
add_executable(${EXP00_RDMA_CLIENT_NAME} ${EXP00_RDMA_SOURCE_FILES})

# verbs
set(ENV{PKG_CONFIG_PATH} /usr/lib/x86_64-linux-gnu/pkgconfig)
set(ENV{PKG_CONFIG_EXECUTABLE} /usr/bin/pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIBVERBS REQUIRED IMPORTED_TARGET libibverbs)

target_link_libraries(${EXP00_RDMA_SERVER_NAME} ${EXP00_LIBRARIES} PkgConfig::LIBIBVERBS)
target_link_libraries(${EXP00_RDMA_CLIENT_NAME} ${EXP00_LIBRARIES} PkgConfig::LIBIBVERBS)