# CMakeLists.txt per test LDP con coverage
cmake_minimum_required(VERSION 3.10)
project(ldp_tests)

set(CMAKE_CXX_STANDARD 17)

# Abilita coverage se supportato
option(COVERAGE "Enable coverage reporting" ON)
if(COVERAGE)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage)
        link_libraries(--coverage)
    endif()
endif()

# GoogleTest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# LDP API
include_directories(../include)

add_executable(test_ldp_api test_ldp_api.cpp)
target_link_libraries(test_ldp_api ${GTEST_LIBRARIES} pthread)
