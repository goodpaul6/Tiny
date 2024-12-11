function(tiny_with_custom_defs target_name)
    set(SOURCES
        ${CMAKE_SOURCE_DIR}/tiny/src/array.c
        ${CMAKE_SOURCE_DIR}/tiny/src/dict.c
        ${CMAKE_SOURCE_DIR}/tiny/src/lexer.c
        ${CMAKE_SOURCE_DIR}/tiny/src/std.c
        ${CMAKE_SOURCE_DIR}/tiny/src/tiny.c
        ${CMAKE_SOURCE_DIR}/tiny/src/util.c
        ${CMAKE_SOURCE_DIR}/tiny/src/pos.c
        ${CMAKE_SOURCE_DIR}/tiny/src/arena.c)

    add_library(${target_name} STATIC ${SOURCES})

    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/tiny/include)

    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE -D_CRT_SECURE_NO_WARNINGS -DTINY_MEM_STACK_TRACE_COUNT=10)
    endif()

    if(UNIX)
        target_link_libraries(${target_name} PRIVATE m)
        target_compile_options(${target_name} PRIVATE -Wno-gnu-alignof-expression)
    endif()

    if(ARGN)
        target_compile_definitions(${target_name} PUBLIC ${ARGN})
    endif()
endfunction()

# Default build with no custom defs
tiny_with_custom_defs(tiny)
