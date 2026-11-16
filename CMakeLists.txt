cmake_minimum_required(VERSION 3.18)
project(Native)
add_library(Native SHARED main.cpp)
target_link_libraries(Native android log dl)