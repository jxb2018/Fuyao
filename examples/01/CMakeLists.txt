cmake_minimum_required(VERSION 3.16)
project(exp01)
set(CMAKE_CXX_STANDARD 17)

include_directories(utils/)

# precondition : build Lumine-host
set(FUNC_WORKER_CPP_LIBRARIES_NAME worker_cpp)

set(EXP01_FN_A_NAME exp01_fn_a)
set(EXP01_FN_B_NAME exp01_fn_b)
set(EXP01_FN_C_NAME exp01_fn_c)
set(EXP01_FN_D_NAME exp01_fn_d)
set(EXP01_FN_E_NAME exp01_fn_e)
set(EXP01_FN_F_NAME exp01_fn_f)

set(EXP01_FN_A_SOURCE_FILES src/fn_a/fn_a.cpp utils/http_parser.c)
set(EXP01_FN_B_SOURCE_FILES src/fn_b/fn_b.cpp)
set(EXP01_FN_C_SOURCE_FILES src/fn_c/fn_c.cpp)
set(EXP01_FN_D_SOURCE_FILES src/fn_d/fn_d.cpp)
set(EXP01_FN_E_SOURCE_FILES src/fn_e/fn_e.cpp)
set(EXP01_FN_F_SOURCE_FILES src/fn_f/fn_f.cpp)

add_executable(${EXP01_FN_A_NAME} ${EXP01_FN_A_SOURCE_FILES})
add_executable(${EXP01_FN_B_NAME} ${EXP01_FN_B_SOURCE_FILES})
add_executable(${EXP01_FN_C_NAME} ${EXP01_FN_C_SOURCE_FILES})
add_executable(${EXP01_FN_D_NAME} ${EXP01_FN_D_SOURCE_FILES})
add_executable(${EXP01_FN_E_NAME} ${EXP01_FN_E_SOURCE_FILES})
add_executable(${EXP01_FN_F_NAME} ${EXP01_FN_F_SOURCE_FILES})

target_link_libraries(${EXP01_FN_A_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
target_link_libraries(${EXP01_FN_B_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
target_link_libraries(${EXP01_FN_C_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
target_link_libraries(${EXP01_FN_D_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
target_link_libraries(${EXP01_FN_E_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
target_link_libraries(${EXP01_FN_F_NAME} ${FUNC_WORKER_CPP_LIBRARIES_NAME})
