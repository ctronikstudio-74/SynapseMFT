cmake_minimum_required(VERSION 3.15)
project(SynapseMFT_CPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Includi directory
include_directories(include)
include_directories(src)

# File sorgente
set(SOURCES
    src/LDPAgentServer.cpp
    src/LDPAgentManager.cpp
    src/LDPApiServer.cpp
    src/api_server_main.cpp
)

# Aggiungi file LDP esistenti se disponibili
# Usa path assoluto o relativo corretto
set(LDP_DIR "${CMAKE_SOURCE_DIR}/../ldp")
if(EXISTS "${LDP_DIR}/LDPFileTransmitter.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPFileTransmitter.cpp")
    include_directories("${LDP_DIR}")
endif()
if(EXISTS "${LDP_DIR}/LDPCrypto.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPCrypto.cpp")
endif()
if(EXISTS "${LDP_DIR}/LDPSocket.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPSocket.cpp")
endif()
if(EXISTS "${LDP_DIR}/LDPTransportTCP.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPTransportTCP.cpp")
endif()
if(EXISTS "${LDP_DIR}/LDPFileOps.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPFileOps.cpp")
endif()
if(EXISTS "${LDP_DIR}/LDPMacUtils.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPMacUtils.cpp")
endif()
if(EXISTS "${LDP_DIR}/LDPStructures.h")
    list(APPEND SOURCES "${LDP_DIR}/LDPStructures.cpp")
endif()

# Librerie
find_package(OpenSSL QUIET)
find_package(Threads REQUIRED)

# OpenSSL è opzionale - usa std::hash se non disponibile
if(OPENSSL_FOUND)
    add_definitions(-DUSE_OPENSSL)
    message(STATUS "OpenSSL found - using for hashing")
else()
    message(STATUS "OpenSSL not found - using std::hash")
endif()

# Eseguibile API Server
add_executable(ldp-api-server ${SOURCES})

target_link_libraries(ldp-api-server 
    Threads::Threads
)

# Link OpenSSL solo se trovato
if(OPENSSL_FOUND)
    target_link_libraries(ldp-api-server ${OPENSSL_LIBRARIES})
    target_include_directories(ldp-api-server PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

if(WIN32)
    target_link_libraries(ldp-api-server ws2_32)
endif()

# Compilazione
target_compile_definitions(ldp-api-server PRIVATE
    $<$<PLATFORM_ID:Windows>:_WIN32>
)

# Agentic event bus. The supported control plane lives in include/synapse.
set(SYNAPSE_BUS_SOURCES
    src/bus/codec.cpp
    src/bus/net.cpp
    src/bus/envelope.cpp
    src/bus/bus.cpp
    src/bus/model.cpp
    src/bus/crypto.cpp
    src/bus/redis.cpp
    src/bus/kafka.cpp
    src/bus/amqp.cpp
    src/bus/cloud.cpp
)
add_library(synapse_bus STATIC ${SYNAPSE_BUS_SOURCES})
target_include_directories(synapse_bus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(synapse_bus PUBLIC cxx_std_20)
if(MSVC)
    target_compile_definitions(synapse_bus PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(synapse_bus PRIVATE /W4 /bigobj)
else()
    target_compile_options(synapse_bus PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(synapse_bus PUBLIC ws2_32)
endif()
find_package(Threads REQUIRED)
target_link_libraries(synapse_bus PUBLIC Threads::Threads)

include("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SynapseCrypto.cmake")
if(SYNAPSE_CRYPTO_READY)
    enable_language(C)
    add_library(synapse_pq STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../ldp/pq/mlkem768.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../ldp/pq/mldsa65.cpp
        ${MLKEM_NATIVE}/mlkem_native.c
        ${MLDSA_NATIVE}/mldsa_native.c
    )
    target_include_directories(synapse_pq PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../ldp/pq
        ${SODIUM_INCLUDE}
        ${MLKEM_NATIVE}
        ${MLDSA_NATIVE}
    )
    target_compile_definitions(synapse_pq PRIVATE
        LDP_HAVE_MLKEM
        LDP_HAVE_MLDSA
        "MLK_CONFIG_FILE=\"ldp_mlkem_config.h\""
        "MLD_CONFIG_FILE=\"ldp_mldsa_config.h\""
    )
    if(MSVC)
        target_compile_options(synapse_pq PRIVATE /W3 /bigobj)
    endif()
    target_link_libraries(synapse_pq PUBLIC ${SODIUM_LIBRARY})
    target_compile_definitions(synapse_bus PRIVATE SYNAPSE_HAVE_PQ)
    target_include_directories(synapse_bus PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../ldp/pq
        ${SODIUM_INCLUDE}
    )
    target_link_libraries(synapse_bus PUBLIC synapse_pq)
    target_include_directories(ldp-api-server PRIVATE ${SODIUM_INCLUDE})
    target_link_libraries(ldp-api-server ${SODIUM_LIBRARY})
    if(WIN32)
        target_link_libraries(ldp-api-server advapi32)
    endif()
    add_custom_command(TARGET ldp-api-server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SODIUM_DLL} $<TARGET_FILE_DIR:ldp-api-server>
        VERBATIM)
endif()

find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    target_compile_definitions(synapse_bus PRIVATE SYNAPSE_HAVE_OPENSSL)
    target_link_libraries(synapse_bus PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

add_executable(synapse_bus_tests tests/test_agentic_bus.cpp)
target_link_libraries(synapse_bus_tests PRIVATE synapse_bus)
target_compile_features(synapse_bus_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(synapse_bus_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(synapse_bus_tests PRIVATE /W4)
endif()
if(SYNAPSE_CRYPTO_READY)
    add_custom_command(TARGET synapse_bus_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SODIUM_DLL} $<TARGET_FILE_DIR:synapse_bus_tests>
        VERBATIM)
    if(MSVC)
        set_property(TARGET synapse_bus synapse_pq synapse_bus_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    endif()
endif()

