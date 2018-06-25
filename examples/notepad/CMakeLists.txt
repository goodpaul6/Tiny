set(SOURCES
    src/notepad.c
    src/editor.c
    src/buffer.c
    src/display.c
    src/tigr.c)

add_executable(tiny_notepad ${SOURCES})

if(WIN32)
    target_link_libraries(tiny_notepad "D3D9.lib")
endif()

target_include_directories(tiny_notepad PRIVATE include)
target_link_libraries(tiny_notepad tiny)
