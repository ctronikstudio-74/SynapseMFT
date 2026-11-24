#include "ldp/ldp_c.h"

#include <lua.h>
#include <lauxlib.h>
#include <string.h>

static void push_error(lua_State* L, int rc) {
    if (rc == LDP_OK) return;
    luaL_error(L, "%s", ldp_last_error()[0] ? ldp_last_error() : "ldp error");
}

static void read_options(lua_State* L, int index, ldp_transfer_options* options) {
    ldp_options_init(options);
    if (!lua_istable(L, index)) return;
    lua_getfield(L, index, "profile");
    if (lua_isnumber(L, -1)) options->profile = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "chunk_size");
    if (lua_isnumber(L, -1)) options->chunk_size = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "rekey_every");
    if (lua_isnumber(L, -1)) options->rekey_every = (uint32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "ack_timeout_ms");
    if (lua_isnumber(L, -1)) options->ack_timeout_ms = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "idle_timeout_ms");
    if (lua_isnumber(L, -1)) options->idle_timeout_ms = (int32_t)lua_tointeger(L, -1);
    lua_pop(L, 1);
}

static int l_version(lua_State* L) {
    lua_pushstring(L, ldp_version());
    return 1;
}

static int l_transfer(lua_State* L) {
    size_t len = 0;
    const char* payload = luaL_checklstring(L, 1, &len);
    ldp_transfer_options options;
    read_options(L, 2, &options);
    uint8_t* out = NULL;
    size_t out_len = 0;
    const int rc = ldp_transfer_loopback((const uint8_t*)payload, len, &options, &out, &out_len);
    push_error(L, rc);
    lua_pushlstring(L, (const char*)out, out_len);
    ldp_free(out);
    return 1;
}

static int l_identity(lua_State* L) {
    uint8_t pk[LDP_MLDSA_PUBLICKEY];
    uint8_t sk[LDP_MLDSA_SECRETKEY];
    push_error(L, ldp_identity_generate(pk, sk));
    lua_newtable(L);
    lua_pushlstring(L, (const char*)pk, sizeof(pk));
    lua_setfield(L, -2, "public_key");
    lua_pushlstring(L, (const char*)sk, sizeof(sk));
    lua_setfield(L, -2, "secret_key");
    ldp_identity_wipe(sk, sizeof(sk));
    return 1;
}

typedef struct { ldp_session* session; } LuaSession;
typedef struct { ldp_pipe* pipe; } LuaPipe;

static int session_gc(lua_State* L) {
    LuaSession* self = luaL_checkudata(L, 1, "ldp.session");
    if (self->session) {
        ldp_session_destroy(self->session);
        self->session = NULL;
    }
    return 0;
}

static int session_handshake(lua_State* L) {
    LuaSession* self = luaL_checkudata(L, 1, "ldp.session");
    push_error(L, ldp_session_handshake(self->session));
    return 0;
}

static int session_send(lua_State* L) {
    LuaSession* self = luaL_checkudata(L, 1, "ldp.session");
    size_t len = 0;
    const char* payload = luaL_checklstring(L, 2, &len);
    const lua_Integer chunk = luaL_optinteger(L, 3, 4096);
    const lua_Integer rekey = luaL_optinteger(L, 4, 0);
    push_error(L, ldp_session_send_all(self->session, (const uint8_t*)payload, len, (uint32_t)chunk, (uint32_t)rekey));
    return 0;
}

static int session_receive(lua_State* L) {
    LuaSession* self = luaL_checkudata(L, 1, "ldp.session");
    const int idle = (int)luaL_optinteger(L, 2, 5000);
    uint8_t* out = NULL;
    size_t out_len = 0;
    push_error(L, ldp_session_receive_all(self->session, idle, &out, &out_len));
    lua_pushlstring(L, (const char*)out, out_len);
    ldp_free(out);
    return 1;
}

static int session_identity(lua_State* L) {
    LuaSession* self = luaL_checkudata(L, 1, "ldp.session");
    size_t secret_len = 0;
    size_t public_len = 0;
    const char* secret = luaL_checklstring(L, 2, &secret_len);
    const char* peer = luaL_checklstring(L, 3, &public_len);
    push_error(L, ldp_session_set_identity(self->session, (const uint8_t*)secret, secret_len, (const uint8_t*)peer, public_len));
    return 0;
}

static void session_meta(lua_State* L) {
    if (luaL_newmetatable(L, "ldp.session")) {
        lua_pushcfunction(L, session_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        lua_pushcfunction(L, session_handshake);
        lua_setfield(L, -2, "handshake");
        lua_pushcfunction(L, session_send);
        lua_setfield(L, -2, "send_all");
        lua_pushcfunction(L, session_receive);
        lua_setfield(L, -2, "receive_all");
        lua_pushcfunction(L, session_identity);
        lua_setfield(L, -2, "set_identity");
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
}

static int pipe_gc(lua_State* L) {
    LuaPipe* self = luaL_checkudata(L, 1, "ldp.pipe");
    if (self->pipe) {
        ldp_pipe_destroy(self->pipe);
        self->pipe = NULL;
    }
    return 0;
}

static int pipe_session(lua_State* L) {
    LuaPipe* self = luaL_checkudata(L, 1, "ldp.pipe");
    const int end = (int)luaL_checkinteger(L, 2);
    const int role = (int)luaL_checkinteger(L, 3);
    const int profile = (int)luaL_optinteger(L, 4, LDP_PROFILE_CLASSIC);
    const int timeout = (int)luaL_optinteger(L, 5, 500);
    ldp_session* session = ldp_pipe_session(self->pipe, end, role, profile, timeout);
    if (!session) push_error(L, LDP_ERR);
    LuaSession* box = lua_newuserdata(L, sizeof(*box));
    box->session = session;
    session_meta(L);
    return 1;
}

static int l_pipe(lua_State* L) {
    ldp_pipe* pipe = ldp_pipe_create();
    if (!pipe) push_error(L, LDP_ERR);
    LuaPipe* box = lua_newuserdata(L, sizeof(*box));
    box->pipe = pipe;
    if (luaL_newmetatable(L, "ldp.pipe")) {
        lua_pushcfunction(L, pipe_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        lua_pushcfunction(L, pipe_session);
        lua_setfield(L, -2, "session");
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

static const luaL_Reg functions[] = {
    {"version", l_version},
    {"transfer_loopback", l_transfer},
    {"identity_generate", l_identity},
    {"pipe", l_pipe},
    {NULL, NULL}
};

#ifdef _WIN32
__declspec(dllexport)
#endif
int luaopen_ldp(lua_State* L) {
    luaL_newlib(L, functions);
    lua_pushinteger(L, LDP_ROLE_CLIENT);
    lua_setfield(L, -2, "ROLE_CLIENT");
    lua_pushinteger(L, LDP_ROLE_SERVER);
    lua_setfield(L, -2, "ROLE_SERVER");
    lua_pushinteger(L, LDP_PROFILE_CLASSIC);
    lua_setfield(L, -2, "PROFILE_CLASSIC");
    lua_pushinteger(L, LDP_PROFILE_HYBRID);
    lua_setfield(L, -2, "PROFILE_HYBRID");
    return 1;
}
