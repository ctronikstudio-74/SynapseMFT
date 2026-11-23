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

