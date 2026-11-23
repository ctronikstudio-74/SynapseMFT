cmake_minimum_required(VERSION 3.15)

# Standalone: cmake -S ldp/tests -B ldp/tests/build
# Also consumed by ldp/CMakeLists.txt via add_subdirectory(tests).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ldp_unit_tests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    enable_testing()
endif()

set(LDP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(ldp_unit_tests
    test_packet_and_congestion.cpp
    ${LDP_ROOT}/LDPStructures.cpp
    ${LDP_ROOT}/LDPCongestionControl.cpp
)

target_include_directories(ldp_unit_tests PRIVATE ${LDP_ROOT})

if(MSVC)
    target_compile_options(ldp_unit_tests PRIVATE /W4 /WX-)
else()
    target_compile_options(ldp_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ldp_packet_and_congestion COMMAND ldp_unit_tests)

# Standalone repo: dependencies live in ldp/third_party.
# Inside the SynapseMFT tree they live one level above ldp.
set(LDP_DEPS "")
foreach(_cand "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party")
    if(EXISTS "${_cand}/libsodium-msvc/libsodium/include/sodium.h"
       AND EXISTS "${_cand}/mlkem-native/mlkem/mlkem_native.c"
       AND EXISTS "${_cand}/mldsa-native/mldsa/mldsa_native.c")
        set(LDP_DEPS "${_cand}")
        break()
    endif()
endforeach()

set(SODIUM_INCLUDE ${LDP_DEPS}/libsodium-msvc/libsodium/include)
set(SODIUM_LIBRARY ${LDP_DEPS}/libsodium-msvc/libsodium/x64/Release/v143/dynamic/libsodium.lib)
set(SODIUM_DLL ${LDP_DEPS}/libsodium-msvc/libsodium/x64/Release/v143/dynamic/libsodium.dll)
set(MLKEM_NATIVE ${LDP_DEPS}/mlkem-native/mlkem)
set(MLDSA_NATIVE ${LDP_DEPS}/mldsa-native/mldsa)

if(LDP_DEPS AND EXISTS ${SODIUM_INCLUDE}/sodium.h AND EXISTS ${SODIUM_LIBRARY}
   AND EXISTS ${MLKEM_NATIVE}/mlkem_native.c AND EXISTS ${MLDSA_NATIVE}/mldsa_native.c)
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
        PRIVATE
            ${LDP_ROOT}/pq
            ${SODIUM_INCLUDE}
            ${MLKEM_NATIVE}
            ${MLDSA_NATIVE}
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

    add_executable(ldp1_tests test_ldp1.cpp)
    target_include_directories(ldp1_tests PRIVATE ${SODIUM_INCLUDE})
    target_link_libraries(ldp1_tests PRIVATE ldp1)
    add_custom_command(TARGET ldp1_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SODIUM_DLL} $<TARGET_FILE_DIR:ldp1_tests>
        VERBATIM)
    add_test(NAME ldp1_session COMMAND ldp1_tests)
else()
    message(STATUS "LDP-1 dependencies not found; ldp1 will not be built. See README.md.")
endif()
