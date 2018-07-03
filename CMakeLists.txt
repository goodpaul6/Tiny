project(tiny)

cmake_minimum_required(VERSION 3.1)

set(SOURCES
    src/tiny.c
    src/array.c
    src/dict.c
    src/tinystd.c
    src/tiny_lexer.c
    src/tiny_util.c
    src/t_mem.c)

add_library(tiny STATIC ${SOURCES})

add_definitions(-DTINY_MEM_CHECK)

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DTINY_MEM_STACK_TRACE_COUNT=10)
    target_link_libraries(tiny PRIVATE dbghelp.lib)
endif()

target_include_directories(tiny PUBLIC include)

add_subdirectory(examples)
add_subdirectory(test)

