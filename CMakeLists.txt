cmake_minimum_required(VERSION 3.1 FATAL_ERROR)

project(tiny)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Core language
set(SOURCES
    pos_error.cpp
    lexer.cpp
    ast_visitor.cpp
    ast.cpp
    parser.cpp
    ast_interpreter.cpp
    type_name_pool.cpp)

add_library(tiny STATIC ${SOURCES})

target_include_directories(tiny PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Tests
set(TEST_LEXER_SOURCES
    test_lexer.cpp)

add_executable(test_lexer ${TEST_LEXER_SOURCES})

target_link_libraries(test_lexer PRIVATE tiny)

set(TEST_PARSER_SOURCES
    test_parser.cpp)

add_executable(test_parser ${TEST_PARSER_SOURCES})

target_link_libraries(test_parser PRIVATE tiny)

set(TEST_AST_INTERPRETER_SOURCES
    test_ast_interpreter.cpp)

add_executable(test_ast_interpreter ${TEST_AST_INTERPRETER_SOURCES})

target_link_libraries(test_ast_interpreter PRIVATE tiny)

set(TEST_TYPE_NAME_POOL_SOURCES
    test_type_name_pool.cpp)

add_executable(test_type_name_pool ${TEST_TYPE_NAME_POOL_SOURCES})

target_link_libraries(test_type_name_pool PRIVATE tiny)
