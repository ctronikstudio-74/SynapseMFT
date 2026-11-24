-- LuaJIT FFI binding of the LDP-1 C ABI.
-- PUC-Rio Lua loads the compiled module from ldp_lua.c instead.
-- local ldp = dofile("ldp_ffi.lua"); ldp.load(dll)

local ffi = require("ffi")
local ldp = {}

ffi.cdef[[
typedef struct ldp_transfer_options {
    int32_t profile;
    uint32_t chunk_size;
    uint32_t rekey_every;
    int32_t ack_timeout_ms;
    int32_t idle_timeout_ms;
} ldp_transfer_options;
const char *ldp_version(void);
const char *ldp_last_error(void);
size_t ldp_sizeof_options(void);
void ldp_free(void *block);
void ldp_options_init(ldp_transfer_options *options);
int ldp_identity_generate(uint8_t *public_key, uint8_t *secret_key);
void ldp_identity_wipe(uint8_t *secret_key, size_t len);
void *ldp_pipe_create(void);
void ldp_pipe_destroy(void *pipe);
void *ldp_pipe_session(void *pipe, int end, int role, int profile, int ack_timeout_ms);
int ldp_session_set_identity(void *session, const uint8_t *secret_key, size_t secret_len, const uint8_t *peer_public_key, size_t public_len);
int ldp_session_handshake(void *session);
int ldp_session_send_all(void *session, const uint8_t *data, size_t len, uint32_t chunk_size, uint32_t rekey_every);
int ldp_session_receive_all(void *session, int idle_timeout_ms, uint8_t **out, size_t *out_len);
void ldp_session_destroy(void *session);
int ldp_transfer_loopback(const uint8_t *payload, size_t len, const ldp_transfer_options *options, uint8_t **out, size_t *out_len);
]]

ldp.ROLE_CLIENT = 1
ldp.ROLE_SERVER = 2
ldp.PROFILE_CLASSIC = 1
ldp.PROFILE_HYBRID = 2
ldp.MLDSA_PUBLICKEY = 1952
ldp.MLDSA_SECRETKEY = 4032

local lib

local function check(rc)
    if rc ~= 0 then error(ffi.string(lib.ldp_last_error())) end
end

function ldp.load(dll)
    lib = ffi.load(dll)
    if tonumber(lib.ldp_sizeof_options()) ~= ffi.sizeof("ldp_transfer_options") then
        error("transfer options layout does not match ldp_c")
    end
    return ldp
end

function ldp.version()
    return ffi.string(lib.ldp_version())
end

local function take(out, len)
    local value = ffi.string(out[0], tonumber(len[0]))
    lib.ldp_free(out[0])
    return value
end

local function options_of(input)
    local options = ffi.new("ldp_transfer_options")
    lib.ldp_options_init(options)
    if input then
        if input.profile then options.profile = input.profile end
        if input.chunk_size then options.chunk_size = input.chunk_size end
        if input.rekey_every then options.rekey_every = input.rekey_every end
        if input.ack_timeout_ms then options.ack_timeout_ms = input.ack_timeout_ms end
        if input.idle_timeout_ms then options.idle_timeout_ms = input.idle_timeout_ms end
    end
    return options
end

function ldp.transfer_loopback(payload, options)
    local out = ffi.new("uint8_t*[1]")
    local len = ffi.new("size_t[1]")
    check(lib.ldp_transfer_loopback(payload, #payload, options_of(options), out, len))
    return take(out, len)
end

function ldp.identity_generate()
    local pk = ffi.new("uint8_t[?]", ldp.MLDSA_PUBLICKEY)
    local sk = ffi.new("uint8_t[?]", ldp.MLDSA_SECRETKEY)
    check(lib.ldp_identity_generate(pk, sk))
    return { public_key = ffi.string(pk, ldp.MLDSA_PUBLICKEY), secret_key = ffi.string(sk, ldp.MLDSA_SECRETKEY), _sk = sk }
end

function ldp.identity_wipe(identity)
    lib.ldp_identity_wipe(identity._sk, ldp.MLDSA_SECRETKEY)
end

function ldp.pipe()
    local handle = lib.ldp_pipe_create()
    if handle == nil then check(-1) end
    return {
        session = function(_, end_, role, profile, timeout)
            local session = lib.ldp_pipe_session(handle, end_, role, profile or ldp.PROFILE_CLASSIC, timeout or 500)
            if session == nil then check(-1) end
            return {
                set_identity = function(_, secret_key, peer_public_key)
                    check(lib.ldp_session_set_identity(session, secret_key, #secret_key, peer_public_key, #peer_public_key))
                end,
                handshake = function() check(lib.ldp_session_handshake(session)) end,
                send_all = function(_, payload, chunk, rekey)
                    check(lib.ldp_session_send_all(session, payload, #payload, chunk or 4096, rekey or 0))
                end,
                receive_all = function(_, idle)
                    local out = ffi.new("uint8_t*[1]")
                    local len = ffi.new("size_t[1]")
                    check(lib.ldp_session_receive_all(session, idle or 5000, out, len))
                    return take(out, len)
                end,
                close = function() lib.ldp_session_destroy(session) end,
            }
        end,
        close = function() lib.ldp_pipe_destroy(handle) end,
    }
end

return ldp
