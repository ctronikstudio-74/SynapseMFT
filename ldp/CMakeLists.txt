cmake_minimum_required(VERSION 3.15)
# This file builds the earlier draft library named `ldp`.
# The supported protocol is LDP-1: static library target `ldp1` from tests/CMakeLists.txt.
# See README.md.
project(LDP_Protocol VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DNOMINMAX)
endif()

# Options
option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Find dependencies. The in-tree copy is vendor/; third_party/ is only a fallback.
include("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SynapseCrypto.cmake")
find_package(PkgConfig QUIET)

# Find libsodium
if(SYNAPSE_CRYPTO_READY)
    set(LIBSODIUM_FOUND TRUE)
    set(LIBSODIUM_INCLUDE_DIRS ${SODIUM_INCLUDE})
    set(LIBSODIUM_LIBRARIES ${SODIUM_LIBRARY})
endif()

if(NOT LIBSODIUM_FOUND AND PKG_CONFIG_FOUND)
    pkg_check_modules(LIBSODIUM QUIET libsodium)
endif()

if(NOT LIBSODIUM_FOUND)
    find_path(LIBSODIUM_INCLUDE_DIR sodium.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        ${SODIUM_INCLUDE}
    )
    
    find_library(LIBSODIUM_LIBRARY
        NAMES sodium libsodium
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        ${SODIUM_LIBRARY}
    )
    
    if(LIBSODIUM_INCLUDE_DIR AND LIBSODIUM_LIBRARY)
        set(LIBSODIUM_FOUND TRUE)
        set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
        set(LIBSODIUM_LIBRARIES ${LIBSODIUM_LIBRARY})
    endif()
endif()

if(NOT LIBSODIUM_FOUND)
    message(FATAL_ERROR "libsodium not found. Please install libsodium or set LIBSODIUM_INCLUDE_DIR and LIBSODIUM_LIBRARY")
endif()

message(STATUS "Found libsodium: ${LIBSODIUM_LIBRARIES}")

# Source files
set(LDP_CORE_SOURCES
    LDPCrypto.cpp
    LDPSocket.cpp
    LDPTransportTCP.cpp
    LDPFileOps.cpp
    LDPFileTransmitter.cpp
    LDPReceiver.cpp
    LDPStructures.cpp
    LDPMacUtils.cpp
    LDPManager.cpp
    LDPAck.cpp
    LDPCongestionControl.cpp
    LDPRemoteCommands.cpp
    LDPNodeController.cpp
    LDPIdentityServer.cpp
    LDPAuditLogger.cpp
    LDPDisasterRecovery.cpp
    LDPHsmManager.cpp
)

# Check if files exist (some may be optional)
set(SOURCES_FOUND "")
foreach(SOURCE_FILE ${LDP_CORE_SOURCES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
        list(APPEND SOURCES_FOUND ${SOURCE_FILE})
    endif()
endforeach()

# Create library
if(BUILD_SHARED_LIBS)
    add_library(ldp SHARED ${SOURCES_FOUND})
    set_target_properties(ldp PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "ldp_api.h"
    )
    if(APPLE)
        set_target_properties(ldp PROPERTIES
            MACOSX_RPATH ON
            INSTALL_NAME_DIR "@rpath"
        )
    endif()
else()
    add_library(ldp STATIC ${SOURCES_FOUND})
endif()

target_include_directories(ldp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    ${LIBSODIUM_INCLUDE_DIRS}
)

target_link_libraries(ldp PUBLIC
    ${LIBSODIUM_LIBRARIES}
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(ldp PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(ldp PUBLIC Threads::Threads)
endif()

# Compiler-specific options
if(MSVC)
    target_compile_options(ldp PRIVATE /W4 /WX-)
else()
    target_compile_options(ldp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_executable(ldp_send main.cpp)
    target_link_libraries(ldp_send ldp)
    
    add_executable(ldp_receive test_ldp.cpp)
    target_link_libraries(ldp_receive ldp)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS ldp
    EXPORT LDPTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)

install(FILES ldp_api.h
    DESTINATION include
)

install(EXPORT LDPTargets
    FILE LDPTargets.cmake
    NAMESPACE LDP::
    DESTINATION lib/cmake/LDP
)

# Package config
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    LDPConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/LDPConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LDPConfig.cmake
    INSTALL_DESTINATION lib/cmake/LDP
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LDPConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LDPConfigVersion.cmake
    DESTINATION lib/cmake/LDP
)

