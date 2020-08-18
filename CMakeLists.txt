cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(dwmbricks C)

find_package(X11 REQUIRED)

add_library(util util.c)
add_library(cli cli.c)

add_executable(dwmbricks dwmbricks.c)
target_link_libraries(dwmbricks X11 util cli)
