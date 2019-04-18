file(GLOB SOURCES src/*.c)

add_library(tiny STATIC ${SOURCES})

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DTINY_MEM_STACK_TRACE_COUNT=10)
endif()

if(UNIX)
    target_link_libraries(tiny PRIVATE m)
endif()

target_include_directories(tiny PUBLIC include)
