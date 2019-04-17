cmake_minimum_required(VERSION 3.10)

project(PieceChain
    LANGUAGES C CXX
    VERSION 0.1.0
)

# Compile in Release mode by default
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# Code Coverage Configuration
add_library(coverage_config INTERFACE)

option (CODE_COVERAGE "Enable coverage reporting" OFF)
if (CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Add required flags (GCC & LLVM/Clang)
    target_compile_options (coverage_config INTERFACE
        -O0        # no optimization
        -g         # generate debug info
        --coverage # sets all required flags
    )
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.13)
        target_link_options (coverage_config INTERFACE --coverage)
    else ()
        target_link_libraries (coverage_config INTERFACE --coverage)
    endif ()
endif ()

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
set(common_link_options
    $<$<CONFIG:DEBUG>:-fsanitize=address>
)
target_compile_features(PieceChain PRIVATE ${common_std})
target_compile_options(PieceChain PRIVATE ${common_options})
target_link_libraries(PieceChain PRIVATE "${common_link_options}")
target_link_libraries(PieceChain PRIVATE coverage_config)

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
target_link_libraries(PieceChainTest PRIVATE "${common_link_options}")
target_link_libraries(PieceChainTest PRIVATE PieceChain Catch2::Catch2 coverage_config)
add_custom_target(test COMMAND PieceChainTest)