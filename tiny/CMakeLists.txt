set(SOURCES
    src/array.c
    src/dict.c
    src/lexer.c
    src/std.c
    src/tiny.c
    src/util.c
    src/pos.c
    src/arena.c)

add_library(tiny STATIC ${SOURCES})

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DTINY_MEM_STACK_TRACE_COUNT=10)
endif()

if(UNIX)
    target_link_libraries(tiny PRIVATE m)
endif()

target_compile_options(tiny PRIVATE -Wno-gnu-alignof-expression)

target_include_directories(tiny PUBLIC include)
