cmake_minimum_required(VERSION 2.8) # Проверка версии CMake.

project(final)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(SRC_LIST final.cpp)

add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} pthread)
