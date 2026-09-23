cmake_minimum_required(VERSION 4.3)
project(Project3_jkrajeski_main C)

set(CMAKE_C_STANDARD 11)

include_directories(Project3-jkrajeski-main)

add_executable(Project3_jkrajeski_main
        Project3-jkrajeski-main/parser.c
        Project3-jkrajeski-main/parser.h
        Project3-jkrajeski-main/test_parser.c
        Project3-jkrajeski-main/tush.c)
