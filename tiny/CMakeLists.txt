set(SOURCES
    lexer.cpp)

add_library(tiny STATIC ${SOURCES})

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(tiny PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(TEST_LEXER_SOURCES
    test_lexer.cpp)

add_executable(test_lexer ${TEST_LEXER_SOURCES})

target_link_libraries(test_lexer PRIVATE tiny)
