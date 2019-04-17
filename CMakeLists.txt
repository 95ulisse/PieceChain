cmake_minimum_required(VERSION 3.13)

project(PieceChain
    LANGUAGES C CXX
    VERSION 0.1.0
)

# Compile in Release mode by default
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# Main library
add_library(PieceChain src/PieceChain.c)

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
set(common_std cxx_std_17)
set(common_options
    -Wall -Wextra -pedantic -Werror
    $<$<CONFIG:RELEASE>:-O3 -DNDEBUG>
    $<$<CONFIG:DEBUG>:-g -fsanitize=address -DDEBUG>
)
target_compile_features(PieceChain PRIVATE ${common_std})
target_compile_options(PieceChain PRIVATE ${common_options})
target_link_options(PieceChain PRIVATE ${common_options})

# Export the target so that it can be referenced by our users
export(PACKAGE PieceChain)

# Tests
add_subdirectory(deps/Catch2)
add_executable(PieceChainTest
    test/main.cpp
    test/PieceChain.cpp
)
target_compile_features(PieceChainTest PRIVATE ${common_std})
target_compile_options(PieceChainTest PRIVATE ${common_options})
target_link_options(PieceChainTest PRIVATE ${common_options})
target_link_libraries(PieceChainTest PRIVATE PieceChain Catch2::Catch2)
add_custom_target(test COMMAND PieceChainTest)