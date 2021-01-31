include_directories(${CMAKE_SOURCE_DIR}/include)
link_libraries(
        pmi-obj
        Fabric::OFI
        OpenMP::OpenMP_CXX
)

add_executable(hello_world hello_world.cpp)
add_executable(pingpong_sym pingpong_sym.cpp)
add_executable(pingpong_asym pingpong_asym.cpp)
add_executable(pingpong_asym-self pingpong_asym-self.cpp)