cmake_minimum_required(VERSION 3.15)
project(ldp_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

set(LDP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../ldp)
include("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SynapseCrypto.cmake")
if(NOT SYNAPSE_CRYPTO_READY)
    message(FATAL_ERROR "Crypto libraries not found in vendor/. See vendor/README.md.")
endif()

enable_language(C)
add_library(ldp1 STATIC
    ${LDP_ROOT}/LDP1Session.cpp
    ${LDP_ROOT}/pq/mlkem768.cpp
    ${LDP_ROOT}/pq/mldsa65.cpp
    ${MLKEM_NATIVE}/mlkem_native.c
    ${MLDSA_NATIVE}/mldsa_native.c
)
target_include_directories(ldp1
    PUBLIC ${LDP_ROOT}
    PRIVATE ${LDP_ROOT}/pq ${SODIUM_INCLUDE} ${MLKEM_NATIVE} ${MLDSA_NATIVE}
)
target_compile_definitions(ldp1 PRIVATE
    LDP_HAVE_MLKEM
    LDP_HAVE_MLDSA
    "MLK_CONFIG_FILE=\"ldp_mlkem_config.h\""
    "MLD_CONFIG_FILE=\"ldp_mldsa_config.h\""
)
if(MSVC)
    target_compile_options(ldp1 PRIVATE /W4 /WX- /bigobj)
else()
    target_compile_options(ldp1 PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(ldp1 PUBLIC ${SODIUM_LIBRARY} advapi32)
else()
    target_link_libraries(ldp1 PUBLIC ${SODIUM_LIBRARY})
endif()

add_library(ldp_sdk STATIC
    src/sdk.cpp
    src/udp_transport.cpp
)
target_include_directories(ldp_sdk
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${SODIUM_INCLUDE}
)
target_link_libraries(ldp_sdk PUBLIC ldp1)
if(WIN32)
    target_link_libraries(ldp_sdk PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(ldp_sdk PRIVATE /W4 /WX-)
else()
    target_compile_options(ldp_sdk PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(ldp_sdk_tool name)
    add_executable(${name} tools/${name}.cpp)
    target_link_libraries(${name} PRIVATE ldp_sdk)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX-)
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SODIUM_DLL} $<TARGET_FILE_DIR:${name}>
        VERBATIM)
endfunction()

ldp_sdk_tool(ldp_try)
ldp_sdk_tool(ldp_bench)

# The DevOps commands are ldp-try and ldp-bench. CMake target names cannot contain '-'.
set_target_properties(ldp_try PROPERTIES OUTPUT_NAME ldp-try)
set_target_properties(ldp_bench PROPERTIES OUTPUT_NAME ldp-bench)
