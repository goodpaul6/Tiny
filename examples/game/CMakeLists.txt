set(SOURCES
    src/game.c
    src/tigr.c)

add_executable(tiny_game ${SOURCES})

if(WIN32)
    target_link_libraries(tiny_game "D3D9.lib")
endif()

target_include_directories(tiny_game PRIVATE include)
target_link_libraries(tiny_game tiny)

