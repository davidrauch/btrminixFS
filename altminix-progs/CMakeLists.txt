cmake_minimum_required(VERSION 3.6)
project(altminix)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

set(SOURCE_FILES utils.cpp errors.cpp snapshots.cpp altminix.cpp)
add_executable(altminix ${SOURCE_FILES})

target_link_libraries(altminix stdc++fs)