set(SOURCES
    src/main.c)

add_executable(tiny_terp ${SOURCES})

target_link_libraries(tiny_terp tiny)
