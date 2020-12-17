project(tiny)

cmake_minimum_required(VERSION 3.1)

set(CMAKE_C_STANDARD 99)

add_subdirectory(tiny)

# Temporarily disabled while refactoring
# add_subdirectory(examples)

add_subdirectory(test)

