cmake_minimum_required(VERSION 3.16)
project(experiments)

include_directories(${BASE_DIR}/src/host/worker/cpp)
include_directories(${BASE_DIR}/lib)
include_directories(${BASE_DIR}/deps/libmc)

# add some experiments
add_subdirectory(00 exp00)
add_subdirectory(01 exp01)