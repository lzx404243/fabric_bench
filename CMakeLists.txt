cmake_minimum_required(VERSION 3.13)
project(fabric_bench)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

include(CheckCSourceCompiles)
check_c_source_compiles(
    "
        int f(void *restrict x);
        int main(void) {return 0;}
    "
    HAVE_RESTRICT
)

set(FB_SERVER IBV CACHE STRING "Server: OFI MPI IBV UCX")

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

find_package(OpenMP REQUIRED)
if("OFI" IN_LIST FB_SERVER)
    find_package(Fabric REQUIRED)
    set(FB_USE_OFI ON)
endif()

if("IBV" IN_LIST FB_SERVER)
    find_package(Fabric REQUIRED)
    set(FB_USE_IBV ON)
endif()

if("UCX" IN_LIST FB_SERVER)
    find_package(
            UCX
            REQUIRED COMPONENTS ucp uct ucs
            HINTS ENV UCX_ROOT
    )
    set(FB_USE_UCX ON)
endif()

configure_file(include/config.hpp.in include/config.hpp @ONLY)
add_subdirectory(modules)

add_library(fb_lib INTERFACE)
target_include_directories(fb_lib INTERFACE
        ${CMAKE_BINARY_DIR}/include
        include
)
target_link_libraries(fb_lib INTERFACE
        mlog-obj
        pmi-obj
        OpenMP::OpenMP_CXX
)

if(("IBV" IN_LIST FB_SERVER) OR ("OFI" IN_LIST FB_SERVER))
    target_link_libraries(fb_lib INTERFACE Fabric::${FB_SERVER})
endif()

if("UCX" IN_LIST FB_SERVER)
    target_link_libraries(fb_lib INTERFACE ucx::uct ucx::ucs)
endif()

option(USE_GPROF "use gprof" OFF)
if (USE_GPROF)
    # export GMON_OUT_PREFIX="gmon.out"
    target_compile_options(fb_lib INTERFACE -pg -fno-inline)
    target_link_options(fb_lib INTERFACE -pg)
endif()

add_subdirectory(benchmark)