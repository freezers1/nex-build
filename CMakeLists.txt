cmake_minimum_required(VERSION 3.18)
project(Native)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Отключаем сборку ассемблерных файлов Dobby для ARM64 (чтобы GitHub не падал)
set(DOBBY_BUILD_EXAMPLES OFF CACHE BOOL "")
set(DOBBY_BUILD_TESTS OFF CACHE BOOL "")
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "")

include(FetchContent)
FetchContent_Declare(
  dobby
  GIT_REPOSITORY https://github.com/jmpews/Dobby.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(dobby)

add_library(Native SHARED main.cpp)

target_link_libraries(Native 
    dobby
    android 
    log 
    dl
    GLESv3 
    EGL
)
