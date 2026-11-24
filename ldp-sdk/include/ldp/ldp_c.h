#ifndef LDP_C_H
#define LDP_C_H

#include <stddef.h>
#include <stdint.h>

/* C ABI of the Synapse Delivery Protocol SDK.
   The engine is LDP-1. This header is what Lua, Python, TypeScript, C# and Java call.
   The algorithms follow FIPS 203 and FIPS 204. This build is not a CMVP-validated module.
   A loopback or udp-loopback number is a same-host measurement, not a wide-area result.

   Strings from ldp_last_error() are valid until the next call on the same thread.
   Buffers from the API are released with ldp_free(). A session is used from one thread.
   Destroy sessions before the pipe they were opened on. */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(LDP_C_BUILD)
#    define LDP_API __declspec(dllexport)
#  else
#    define LDP_API __declspec(dllimport)
#  endif
#else
#  define LDP_API
#endif

#define LDP_C_VERSION "0.2.0"

#define LDP_OK 0
#define LDP_ERR (-1)
#define LDP_ERR_ARG (-2)
#define LDP_ERR_SPACE (-3)

#define LDP_ROLE_CLIENT 1
#define LDP_ROLE_SERVER 2
#define LDP_PROFILE_CLASSIC 1
#define LDP_PROFILE_HYBRID 2

#define LDP_MLDSA_PUBLICKEY 1952
#define LDP_MLDSA_SECRETKEY 4032

#define LDP_WINDOW 8

typedef struct ldp_transfer_options {
    int32_t profile;
    uint32_t chunk_size;
    uint32_t rekey_every;
    int32_t ack_timeout_ms;
    int32_t idle_timeout_ms;
} ldp_transfer_options;

typedef struct ldp_bench_row {
    char profile[32];
    char transport[32];
    uint64_t payload_bytes;
    uint32_t chunk_size;
    uint32_t rekey_every;
    double seconds;
    double mebibytes_per_second;
    int32_t match;
} ldp_bench_row;

/* send returns 0 on success. recv returns 1 and sets *out_len, 0 on timeout, negative on error. */
typedef int (*ldp_send_fn)(void* user, const uint8_t* data, size_t len);
typedef int (*ldp_recv_fn)(void* user, uint8_t* buffer, size_t capacity, int timeout_ms, size_t* out_len);

typedef struct ldp_session ldp_session;
typedef struct ldp_pipe ldp_pipe;

LDP_API const char* ldp_version(void);
LDP_API const char* ldp_last_error(void);
LDP_API size_t ldp_sizeof_options(void);
LDP_API size_t ldp_sizeof_bench_row(void);
LDP_API void ldp_free(void* block);
LDP_API void ldp_options_init(ldp_transfer_options* options);

LDP_API int ldp_identity_generate(uint8_t public_key[LDP_MLDSA_PUBLICKEY], uint8_t secret_key[LDP_MLDSA_SECRETKEY]);
LDP_API void ldp_identity_wipe(uint8_t* secret_key, size_t len);

LDP_API ldp_session* ldp_session_create(int role, int profile, ldp_send_fn send, ldp_recv_fn recv, void* user, int ack_timeout_ms);
LDP_API ldp_session* ldp_udp_session_create(int role, int profile, uint16_t local_port, const char* remote_host, uint16_t remote_port, int ack_timeout_ms);
LDP_API int ldp_udp_local_port(ldp_session* session, uint16_t* port);
LDP_API void ldp_session_destroy(ldp_session* session);
LDP_API int ldp_session_set_identity(ldp_session* session, const uint8_t* secret_key, size_t secret_len, const uint8_t* peer_public_key, size_t public_len);
LDP_API int ldp_session_handshake(ldp_session* session);
LDP_API int ldp_session_established(ldp_session* session);
LDP_API int ldp_session_send_all(ldp_session* session, const uint8_t* data, size_t len, uint32_t chunk_size, uint32_t rekey_every);
LDP_API int ldp_session_receive_all(ldp_session* session, int idle_timeout_ms, uint8_t** out, size_t* out_len);

LDP_API ldp_pipe* ldp_pipe_create(void);
LDP_API void ldp_pipe_destroy(ldp_pipe* pipe);
LDP_API ldp_session* ldp_pipe_session(ldp_pipe* pipe, int end, int role, int profile, int ack_timeout_ms);

LDP_API int ldp_transfer_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, uint8_t** out, size_t* out_len);
LDP_API int ldp_measure_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, ldp_bench_row* row);
LDP_API int ldp_measure_udp_loopback(const uint8_t* payload, size_t len, const ldp_transfer_options* options, ldp_bench_row* row);
LDP_API int ldp_udp_listen(uint16_t port, const char* output_path, const ldp_transfer_options* options);
LDP_API int ldp_udp_send(const char* host, uint16_t port, const uint8_t* payload, size_t len, const ldp_transfer_options* options);
LDP_API int ldp_run_public_bench(ldp_bench_row* rows, size_t capacity, size_t* out_count);
LDP_API int ldp_format_public_bench(const ldp_bench_row* rows, size_t count, char** text);

#ifdef __cplusplus
}
#endif

#endif
