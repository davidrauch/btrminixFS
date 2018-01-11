cmake_minimum_required(VERSION 3.6)
project(btrminix)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

set(SOURCE_FILES utils.cpp errors.cpp snapshots.cpp btrminix.cpp)
add_executable(btrminix ${SOURCE_FILES})

target_link_libraries(btrminix stdc++fs)