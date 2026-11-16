cmake_minimum_required(VERSION 3.18)
project(Native)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Подключаем Dobby (хуки)
add_subdirectory(third_party/dobby)
include_directories(third_party/dobby/include)

# Собираем нашу библиотеку
add_library(Native SHARED main.cpp)

# Линкуем Dobby и системные библиотеки Android
target_link_libraries(Native 
    dobby
    android 
    log 
    dl
    GLESv3 
    EGL
)
