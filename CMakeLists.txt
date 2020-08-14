cmake_minimum_required(VERSION 3.10)
project(dwmbricks C)

find_package(X11 REQUIRED)

add_executable(dwmbricks dwmbricks.c)
target_link_libraries(dwmbricks X11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
