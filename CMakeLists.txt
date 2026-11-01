cmake_minimum_required(VERSION 3.20)
project(CudaMedianFilter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)

set(COMMON_SOURCES
    src/common.cpp
    src/image_io.cpp
    src/cuda_backend.cpp)

add_executable(median_bench
    src/main_cli.cpp
    src/benchmark.cpp
    ${COMMON_SOURCES})
target_include_directories(median_bench PRIVATE
    ${CUDAToolkit_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(median_bench PRIVATE
    user32 gdi32 comctl32 comdlg32 shell32 ole32 uuid)

add_executable(median_gui WIN32
    src/main_gui.cpp
    src/gui.cpp
    ${COMMON_SOURCES})
target_include_directories(median_gui PRIVATE
    ${CUDAToolkit_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(median_gui PRIVATE
    user32 gdi32 comctl32 comdlg32 shell32 ole32 uuid)

# Keep the runtime-compiled kernel next to the executables.
add_custom_command(TARGET median_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels.cu
        $<TARGET_FILE_DIR:median_gui>/kernels.cu)
add_custom_command(TARGET median_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/src/kernels.cu
        $<TARGET_FILE_DIR:median_bench>/kernels.cu)
