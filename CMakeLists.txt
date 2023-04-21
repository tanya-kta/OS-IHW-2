cmake_minimum_required(VERSION 3.16)
project(OS_HW2 C)

set(CMAKE_C_STANDARD 11)

add_executable(OS_HW2 main.c)
target_link_libraries(OS_HW2 rt pthread)
