if(WIN32)
    set(SOURCES
        src/serv_win32.c
        src/request.c
        src/response.c)
else()
    set(SOURCES
        src/serv_dummy.c)
endif()

add_executable(tiny_server ${SOURCES})

if(WIN32)
    target_link_libraries(tiny_server "Ws2_32.lib")
endif()

target_include_directories(tiny_server PRIVATE include)
target_link_libraries(tiny_server tiny)
