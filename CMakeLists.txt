project(tiny)

cmake_minimum_required(VERSION 3.1)

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(SOURCES
    src/tiny.c
    src/array.c
    src/dict.c
    src/iniparser.c
    src/tinystd.c)

add_library(tiny STATIC ${SOURCES})

target_include_directories(tiny PUBLIC include)

add_subdirectory(examples)
add_subdirectory(test)

