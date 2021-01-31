cmake_minimum_required(VERSION 3.13)
project(fabricBench)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

find_package(OpenMP REQUIRED)
find_package(Fabric REQUIRED)

configure_file(include/config.hpp.in include/config.hpp @ONLY)
include_directories(${CMAKE_BINARY_DIR}/include)

add_subdirectory(pmi)
add_subdirectory(benchmark)