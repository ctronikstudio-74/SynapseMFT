# Binding LDP

SDK 0.2.0 parla con il motore C++ attraverso `ldp_c`. Un programmatore usa la sessione, non una copia del protocollo.

The same calls exist in every language:

| Call | What it does |
| --- | --- |
| `transfer_loopback` | client and server in one process |
| `measure_loopback`, `measure_udp_loopback` | one benchmark row |
| `run_public_bench`, `format_public_bench` | the fixed public matrix |
| `identity_generate`, `set_identity` | ML-DSA-65 keys, pinned before a hybrid handshake |
| `pipe` + `session` | two ends of an in-memory datagram pipe |
| `udp_session` | a real UDP socket; port 0 lets the OS choose |
| `handshake`, `send_all`, `receive_all` | the LDP-1 session |
| `udp_listen`, `udp_send` | the two-process tools, classic profile only |

`send_all` blocks until the peer has acknowledged the file. Drive the server from another thread. Destroy the sessions before the pipe. `ldp_free` releases buffers returned by the C ABI; the language objects do that for you.

A loopback figure is a same-host measurement. ML-KEM-768 and ML-DSA-65 follow FIPS 203 and FIPS 204. This build is not a CMVP-validated module.

Build the library first, from `ldp-sdk`:

```bat
build_sdk.bat
```

`build/ldp_c.dll` and `build/libsodium.dll` must stay side by side. `LDP_C_DLL` overrides the path.

## Python

```bat
python bindings\python\test_transfer.py
```

```python
import ldp
options = ldp.default_options()
options.profile = ldp.PROFILE_HYBRID
received = ldp.transfer_loopback(payload, options)
```

## C#

```bat
dotnet run --project bindings\csharp\Ldp.Tests
```

The library project is `bindings/csharp/Ldp`. Reference it from an application.

## Java

JDK 22 or newer, Foreign Function & Memory API. From `ldp-sdk`:

```bat
javac --release 22 -d bindings\java\out bindings\java\src\main\java\it\ctronik\ldp\Ldp.java bindings\java\src\test\java\LdpTest.java
java --enable-native-access=ALL-UNNAMED -cp bindings\java\out LdpTest
```

## TypeScript

```bat
cd bindings\typescript
npm install
npm test
```

`Session` and `Pipe` are in the module. Node runs one JavaScript thread, so the server `receive_all` belongs in a `worker_threads` worker. `transferLoopback` does that pairing inside the native library.

## Lua

`bindings/lua/ldp_ffi.lua` is the LuaJIT FFI module: `ldp.load(dll)` then `ldp.transfer_loopback`. `bindings/lua/ldp_lua.c` is the PUC-Rio module, Lua 5.3 or 5.4. Compile it against Lua and link `ldp_c`:

```bat
cl /nologo /LD /I path\to\lua\src bindings\lua\ldp_lua.c build\ldp_c.lib path\to\lua54.lib /Fe:ldp.dll
```

`require("ldp")` then `ldp.transfer_loopback(payload, { profile = ldp.PROFILE_HYBRID, chunk_size = 256 })`. Lua runs one state, so `transfer_loopback` is the call that pairs client and server. `pipe` and `session` are there for a host that already has two operating-system threads.
