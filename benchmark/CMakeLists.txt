add_executable(pingpong_sym pingpong_sym.cpp)
target_include_directories(pingpong_sym PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(pingpong_sym PRIVATE
        pmi-obj
        Fabric::OFI
        OpenMP::OpenMP_CXX
        )
