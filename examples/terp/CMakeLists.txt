set(SOURCES
    src/main.c)

add_executable(tiny_terp ${SOURCES})

tiny_with_custom_defs(tiny_for_terp 
    TINY_THREAD_STACK_SIZE=100000
    TINY_THREAD_MAX_CALL_DEPTH=50000)

target_link_libraries(tiny_terp tiny_for_terp)
