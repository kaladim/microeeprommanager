cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0003 NEW)
project(mEEM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable testing at the project root level
enable_testing()

add_subdirectory(src)
add_subdirectory(test)