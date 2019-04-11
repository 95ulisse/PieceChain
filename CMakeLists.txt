cmake_minimum_required(VERSION 3.5)

project(PieceChain
    LANGUAGES CXX
    VERSION 0.1.0
)

# Main library
add_library(PieceChain src/test.cpp)

# Alias for testing
add_library(PieceChain::PieceChain ALIAS PieceChain)

# Include directories
target_include_directories(PieceChain
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler options
target_compile_options(PieceChain PRIVATE -Wall -Wextra -pedantic -Werror)
target_compile_features(PieceChain PRIVATE cxx_std_11)

# Export the target so that it can be referenced by our users
export(PACKAGE PieceChain)

# Tests
include(CTest)
enable_testing()
add_subdirectory(test)
