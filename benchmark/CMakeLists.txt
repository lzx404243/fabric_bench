option(USE_GPROF "use gprof" OFF)
if (USE_GPROF)
    # export GMON_OUT_PREFIX="gmon.out"
    add_compile_options(-pg -fno-inline)
    add_link_options(-pg)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
link_libraries(
        mlog-obj
        pmi-obj
        Fabric::OFI
        OpenMP::OpenMP_CXX
)

add_executable(hello_world hello_world.cpp)
add_executable(pingpong_sym pingpong_sym.cpp)
add_executable(pingpong_prg pingpong_prg.cpp)