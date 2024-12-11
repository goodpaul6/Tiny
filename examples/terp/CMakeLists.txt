set(SOURCES
    src/main.c)

add_executable(tiny_terp ${SOURCES})

tiny_with_custom_defs(tiny_for_terp 
    TINY_THREAD_STACK_SIZE=2048 
    TINY_THREAD_MAX_CALL_DEPTH=1024)

target_link_libraries(tiny_terp tiny_for_terp)
