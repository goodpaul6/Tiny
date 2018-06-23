set(SOURCES
    src/test.c)

add_executable(tiny_test ${SOURCES})

target_include_directories(tiny_test PRIVATE include)

target_link_libraries(tiny_test tiny)
