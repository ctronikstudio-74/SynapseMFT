"""Synapse Delivery Protocol SDK for Python.

The calls go to ldp_c, the C ABI of the C++ LDP-1 engine.
FIPS 203 and FIPS 204 describe the algorithms. This build is not a CMVP module.
"""

from __future__ import annotations

import ctypes
import os
import threading
from pathlib import Path

LDP_OK = 0
LDP_ERR = -1
LDP_ERR_ARG = -2
LDP_ERR_SPACE = -3

ROLE_CLIENT = 1
ROLE_SERVER = 2
PROFILE_CLASSIC = 1
PROFILE_HYBRID = 2

MLDSA_PUBLICKEY = 1952
MLDSA_SECRETKEY = 4032


class LdpError(RuntimeError):
    pass


class Options(ctypes.Structure):
    _fields_ = [
        ("profile", ctypes.c_int32),
        ("chunk_size", ctypes.c_uint32),
        ("rekey_every", ctypes.c_uint32),
        ("ack_timeout_ms", ctypes.c_int32),
        ("idle_timeout_ms", ctypes.c_int32),
    ]


class BenchRow(ctypes.Structure):
    _fields_ = [
        ("profile", ctypes.c_char * 32),
        ("transport", ctypes.c_char * 32),
        ("payload_bytes", ctypes.c_uint64),
        ("chunk_size", ctypes.c_uint32),
        ("rekey_every", ctypes.c_uint32),
        ("seconds", ctypes.c_double),
        ("mebibytes_per_second", ctypes.c_double),
        ("match", ctypes.c_int32),
    ]


def _library_path() -> Path:
    override = os.environ.get("LDP_C_DLL")
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[3] / "build" / "ldp_c.dll"


def _load():
    path = _library_path()
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(path.parent))
    os.environ["PATH"] = str(path.parent) + os.pathsep + os.environ.get("PATH", "")
    lib = ctypes.CDLL(str(path))
    lib.ldp_version.restype = ctypes.c_char_p
    lib.ldp_last_error.restype = ctypes.c_char_p
    lib.ldp_sizeof_options.restype = ctypes.c_size_t
    lib.ldp_sizeof_bench_row.restype = ctypes.c_size_t
    lib.ldp_free.argtypes = [ctypes.c_void_p]
    lib.ldp_options_init.argtypes = [ctypes.POINTER(Options)]
    lib.ldp_identity_generate.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.ldp_identity_generate.restype = ctypes.c_int
    lib.ldp_identity_wipe.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    lib.ldp_session_destroy.argtypes = [ctypes.c_void_p]
    lib.ldp_session_set_identity.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    lib.ldp_session_set_identity.restype = ctypes.c_int
    lib.ldp_session_handshake.argtypes = [ctypes.c_void_p]
    lib.ldp_session_handshake.restype = ctypes.c_int
    lib.ldp_session_established.argtypes = [ctypes.c_void_p]
    lib.ldp_session_established.restype = ctypes.c_int
    lib.ldp_session_send_all.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
    lib.ldp_session_send_all.restype = ctypes.c_int
    lib.ldp_session_receive_all.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)]
    lib.ldp_session_receive_all.restype = ctypes.c_int
    lib.ldp_pipe_create.restype = ctypes.c_void_p
    lib.ldp_pipe_destroy.argtypes = [ctypes.c_void_p]
    lib.ldp_pipe_session.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.ldp_pipe_session.restype = ctypes.c_void_p
    lib.ldp_udp_session_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_uint16, ctypes.c_int]
    lib.ldp_udp_session_create.restype = ctypes.c_void_p
    lib.ldp_udp_local_port.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint16)]
    lib.ldp_udp_local_port.restype = ctypes.c_int
    lib.ldp_transfer_loopback.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Options), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)]
    lib.ldp_transfer_loopback.restype = ctypes.c_int
    lib.ldp_measure_loopback.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Options), ctypes.POINTER(BenchRow)]
    lib.ldp_measure_loopback.restype = ctypes.c_int
    lib.ldp_measure_udp_loopback.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Options), ctypes.POINTER(BenchRow)]
    lib.ldp_measure_udp_loopback.restype = ctypes.c_int
    lib.ldp_udp_listen.argtypes = [ctypes.c_uint16, ctypes.c_char_p, ctypes.POINTER(Options)]
    lib.ldp_udp_listen.restype = ctypes.c_int
    lib.ldp_udp_send.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Options)]
    lib.ldp_udp_send.restype = ctypes.c_int
    lib.ldp_run_public_bench.argtypes = [ctypes.POINTER(BenchRow), ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    lib.ldp_run_public_bench.restype = ctypes.c_int
    lib.ldp_format_public_bench.argtypes = [ctypes.POINTER(BenchRow), ctypes.c_size_t, ctypes.POINTER(ctypes.c_void_p)]
    lib.ldp_format_public_bench.restype = ctypes.c_int
    send_fn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t)
    recv_fn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t))
    lib.ldp_session_create.argtypes = [ctypes.c_int, ctypes.c_int, send_fn, recv_fn, ctypes.c_void_p, ctypes.c_int]
    lib.ldp_session_create.restype = ctypes.c_void_p
    lib._send_fn = send_fn
    lib._recv_fn = recv_fn
    if ctypes.sizeof(Options) != lib.ldp_sizeof_options():
        raise LdpError("transfer options layout does not match ldp_c")
    if ctypes.sizeof(BenchRow) != lib.ldp_sizeof_bench_row():
        raise LdpError("bench row layout does not match ldp_c")
    return lib


_lib = _load()


def version() -> str:
    return _lib.ldp_version().decode()


def _raise(rc: int) -> None:
    if rc != LDP_OK:
        message = _lib.ldp_last_error()
        raise LdpError(message.decode() if message else "ldp error")


def _bytes_in(data: bytes) -> ctypes.Array:
    return (ctypes.c_uint8 * len(data)).from_buffer_copy(data)


def _take(pointer: ctypes.c_void_p, size: int) -> bytes:
    if size == 0:
        _lib.ldp_free(pointer)
        return b""
    raw = ctypes.string_at(pointer, size)
    _lib.ldp_free(pointer)
    return raw


def default_options() -> Options:
    options = Options()
    _lib.ldp_options_init(ctypes.byref(options))
    return options


class Identity:
    def __init__(self, public_key: bytes, secret_key: bytearray):
        self.public_key = public_key
        self.secret_key = secret_key

    @staticmethod
    def generate() -> "Identity":
        public = ctypes.create_string_buffer(MLDSA_PUBLICKEY)
        secret = ctypes.create_string_buffer(MLDSA_SECRETKEY)
        _raise(_lib.ldp_identity_generate(public, secret))
        return Identity(public.raw, bytearray(secret.raw))

    def wipe(self) -> None:
        buffer = (ctypes.c_uint8 * len(self.secret_key)).from_buffer(self.secret_key)
        _lib.ldp_identity_wipe(buffer, len(self.secret_key))
        self.secret_key[:] = b"\x00" * len(self.secret_key)


class Session:
    def __init__(self, handle: int):
        if not handle:
            _raise(LDP_ERR)
        self._handle = handle
        self._callbacks: list = []

    def set_identity(self, secret_key: bytes, peer_public_key: bytes) -> None:
        secret = _bytes_in(secret_key)
        peer = _bytes_in(peer_public_key)
        _raise(_lib.ldp_session_set_identity(self._handle, secret, len(secret_key), peer, len(peer_public_key)))

    def handshake(self) -> None:
        _raise(_lib.ldp_session_handshake(self._handle))

    def established(self) -> bool:
        return _lib.ldp_session_established(self._handle) == 1

    def send_all(self, payload: bytes, chunk_size: int = 4096, rekey_every: int = 0) -> None:
        data = _bytes_in(payload) if payload else None
        _raise(_lib.ldp_session_send_all(self._handle, data, len(payload), chunk_size, rekey_every))

    def receive_all(self, idle_timeout_ms: int = 5000) -> bytes:
        out = ctypes.c_void_p()
        out_len = ctypes.c_size_t()
        _raise(_lib.ldp_session_receive_all(self._handle, idle_timeout_ms, ctypes.byref(out), ctypes.byref(out_len)))
        return _take(out, out_len.value)

    def local_port(self) -> int:
        port = ctypes.c_uint16()
        _raise(_lib.ldp_udp_local_port(self._handle, ctypes.byref(port)))
        return port.value

    def close(self) -> None:
        if self._handle:
            _lib.ldp_session_destroy(self._handle)
            self._handle = None
            self._callbacks.clear()

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *_) -> None:
        self.close()


class Pipe:
    def __init__(self):
        self._handle = _lib.ldp_pipe_create()
        if not self._handle:
            _raise(LDP_ERR)

    def session(self, end: int, role: int, profile: int = PROFILE_CLASSIC, ack_timeout_ms: int = 500) -> Session:
        return Session(_lib.ldp_pipe_session(self._handle, end, role, profile, ack_timeout_ms))

    def close(self) -> None:
        if self._handle:
            _lib.ldp_pipe_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> "Pipe":
        return self

    def __exit__(self, *_) -> None:
        self.close()


def callback_session(role: int, profile: int, send, recv, ack_timeout_ms: int = 500) -> Session:
    """send(data: bytes) -> None. recv(timeout_ms: int) -> bytes | None. None means timeout."""
    holder = {"send": send, "recv": recv}

    @_lib._send_fn
    def send_cb(_user, data, length):
        holder["send"](ctypes.string_at(data, length))
        return 0

    @_lib._recv_fn
    def recv_cb(_user, buffer, capacity, timeout_ms, out_len):
        incoming = holder["recv"](timeout_ms)
        if incoming is None:
            return 0
        if len(incoming) > capacity:
            return -1
        ctypes.memmove(buffer, incoming, len(incoming))
        out_len[0] = len(incoming)
        return 1

    session = Session(_lib.ldp_session_create(role, profile, send_cb, recv_cb, None, ack_timeout_ms))
    session._callbacks.extend((send_cb, recv_cb, holder))
    return session


def udp_session(role: int, profile: int = PROFILE_CLASSIC, local_port: int = 0, remote_host: str | None = None, remote_port: int = 0, ack_timeout_ms: int = 500) -> Session:
    host = None if not remote_host else remote_host.encode()
    return Session(_lib.ldp_udp_session_create(role, profile, local_port, host, remote_port, ack_timeout_ms))


def transfer_loopback(payload: bytes, options: Options | None = None) -> bytes:
    owned = options if options is not None else default_options()
    data = _bytes_in(payload) if payload else None
    out = ctypes.c_void_p()
    out_len = ctypes.c_size_t()
    _raise(_lib.ldp_transfer_loopback(data, len(payload), ctypes.byref(owned), ctypes.byref(out), ctypes.byref(out_len)))
    return _take(out, out_len.value)


def _measure(function, payload: bytes, options: Options | None) -> dict:
    owned = options if options is not None else default_options()
    data = _bytes_in(payload) if payload else None
    row = BenchRow()
    _raise(function(data, len(payload), ctypes.byref(owned), ctypes.byref(row)))
    return _row_dict(row)


def measure_loopback(payload: bytes, options: Options | None = None) -> dict:
    return _measure(_lib.ldp_measure_loopback, payload, options)


def measure_udp_loopback(payload: bytes, options: Options | None = None) -> dict:
    return _measure(_lib.ldp_measure_udp_loopback, payload, options)


def _row_dict(row: BenchRow) -> dict:
    return {
        "profile": row.profile.split(b"\x00", 1)[0].decode(),
        "transport": row.transport.split(b"\x00", 1)[0].decode(),
        "payload_bytes": row.payload_bytes,
        "chunk_size": row.chunk_size,
        "rekey_every": row.rekey_every,
        "seconds": row.seconds,
        "mebibytes_per_second": row.mebibytes_per_second,
        "match": bool(row.match),
    }


def udp_listen(port: int, output_path: str, options: Options | None = None) -> None:
    owned = options if options is not None else default_options()
    _raise(_lib.ldp_udp_listen(port, output_path.encode(), ctypes.byref(owned)))


def udp_send(host: str, port: int, payload: bytes, options: Options | None = None) -> None:
    owned = options if options is not None else default_options()
    data = _bytes_in(payload) if payload else None
    _raise(_lib.ldp_udp_send(host.encode(), port, data, len(payload), ctypes.byref(owned)))


def run_public_bench() -> list[dict]:
    count = ctypes.c_size_t()
    rc = _lib.ldp_run_public_bench(None, 0, ctypes.byref(count))
    if rc != LDP_ERR_SPACE:
        _raise(rc)
    rows = (BenchRow * count.value)()
    _raise(_lib.ldp_run_public_bench(rows, count.value, ctypes.byref(count)))
    return [_row_dict(rows[i]) for i in range(count.value)]


def format_public_bench(rows: list[dict]) -> str:
    native = (BenchRow * len(rows))()
    for i, row in enumerate(rows):
        native[i].profile = row["profile"].encode()
        native[i].transport = row["transport"].encode()
        native[i].payload_bytes = row["payload_bytes"]
        native[i].chunk_size = row["chunk_size"]
        native[i].rekey_every = row["rekey_every"]
        native[i].seconds = row["seconds"]
        native[i].mebibytes_per_second = row["mebibytes_per_second"]
        native[i].match = 1 if row["match"] else 0
    text = ctypes.c_void_p()
    _raise(_lib.ldp_format_public_bench(native, len(rows), ctypes.byref(text)))
    value = ctypes.cast(text, ctypes.c_char_p).value or b""
    copied = value.decode()
    _lib.ldp_free(text)
    return copied


def exchange(server: Session, client: Session, payload: bytes, chunk_size: int = 512, rekey_every: int = 0) -> bytes:
    """Run handshake and a transfer. The server receive blocks, so it runs on another thread."""
    box: dict = {}

    def serve() -> None:
        try:
            server.handshake()
            box["bytes"] = server.receive_all()
        except Exception as ex:  # noqa: BLE001 - the caller thread reports it
            box["error"] = ex

    worker = threading.Thread(target=serve)
    worker.start()
    try:
        client.handshake()
        client.send_all(payload, chunk_size, rekey_every)
    finally:
        worker.join()
    if "error" in box:
        raise box["error"]
    return box["bytes"]
