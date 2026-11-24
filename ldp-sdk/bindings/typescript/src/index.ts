import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const koffi = require("koffi") as Koffi;

export const RoleClient = 1;
export const RoleServer = 2;
export const ProfileClassic = 1;
export const ProfileHybrid = 2;
export const MldsaPublicKey = 1952;
export const MldsaSecretKey = 4032;

export class LdpError extends Error {}

export interface TransferOptions {
  profile?: number;
  chunkSize?: number;
  rekeyEvery?: number;
  ackTimeoutMs?: number;
  idleTimeoutMs?: number;
}

export interface BenchRow {
  profile: string;
  transport: string;
  payloadBytes: number;
  chunkSize: number;
  rekeyEvery: number;
  seconds: number;
  mebibytesPerSecond: number;
  match: boolean;
}

interface NativeOptions {
  profile: number;
  chunk_size: number;
  rekey_every: number;
  ack_timeout_ms: number;
  idle_timeout_ms: number;
}

interface NativeRow {
  profile: string;
  transport: string;
  payload_bytes: bigint;
  chunk_size: number;
  rekey_every: number;
  seconds: number;
  mebibytes_per_second: number;
  match: number;
}

interface Koffi {
  load(file: string): Library;
  struct(name: string, fields: Record<string, string>): unknown;
  decode(value: unknown, type: string, length?: number): Buffer;
}

interface Library {
  func(signature: string): (...args: unknown[]) => unknown;
}

const here = path.dirname(fileURLToPath(import.meta.url));
const dll = process.env.LDP_C_DLL ?? path.resolve(here, "../../../build/ldp_c.dll");
process.env.PATH = path.dirname(dll) + path.delimiter + (process.env.PATH ?? "");

const OptionsType = koffi.struct("ldp_transfer_options", {
  profile: "int32",
  chunk_size: "uint32",
  rekey_every: "uint32",
  ack_timeout_ms: "int32",
  idle_timeout_ms: "int32",
});
const RowType = koffi.struct("ldp_bench_row", {
  profile: "char[32]",
  transport: "char[32]",
  payload_bytes: "uint64",
  chunk_size: "uint32",
  rekey_every: "uint32",
  seconds: "double",
  mebibytes_per_second: "double",
  match: "int32",
});

const lib = koffi.load(dll);
const versionFn = lib.func("const char *ldp_version()");
const lastError = lib.func("const char *ldp_last_error()");
const sizeofOptions = lib.func("size_t ldp_sizeof_options()");
const freeFn = lib.func("void ldp_free(void *block)");
const transferFn = lib.func("int ldp_transfer_loopback(uint8_t *payload, size_t len, ldp_transfer_options *options, _Out_ uint8_t **out, _Out_ size_t *out_len)");
const measureFn = lib.func("int ldp_measure_loopback(uint8_t *payload, size_t len, ldp_transfer_options *options, _Out_ ldp_bench_row *row)");
const pipeCreate = lib.func("void *ldp_pipe_create()");
const pipeDestroy = lib.func("void ldp_pipe_destroy(void *pipe)");
const pipeSession = lib.func("void *ldp_pipe_session(void *pipe, int end, int role, int profile, int ack_timeout_ms)");
const setIdentity = lib.func("int ldp_session_set_identity(void *session, uint8_t *secret_key, size_t secret_len, uint8_t *peer_public_key, size_t public_len)");
const handshake = lib.func("int ldp_session_handshake(void *session)");
const sendAll = lib.func("int ldp_session_send_all(void *session, uint8_t *data, size_t len, uint32_t chunk_size, uint32_t rekey_every)");
const receiveAll = lib.func("int ldp_session_receive_all(void *session, int idle_timeout_ms, _Out_ uint8_t **out, _Out_ size_t *out_len)");
const sessionDestroy = lib.func("void ldp_session_destroy(void *session)");
const identityGenerate = lib.func("int ldp_identity_generate(uint8_t *public_key, uint8_t *secret_key)");
const identityWipe = lib.func("void ldp_identity_wipe(uint8_t *secret_key, size_t len)");
const formatBench = lib.func("int ldp_format_public_bench(ldp_bench_row *rows, size_t count, _Out_ void **text)");

if (Number(sizeofOptions()) !== 20) throw new LdpError("transfer options layout does not match ldp_c");
void OptionsType;
void RowType;

function check(rc: unknown): void {
  if (Number(rc) !== 0) throw new LdpError(String(lastError() || "ldp error"));
}

function take(pointer: unknown, length: unknown): Buffer {
  const n = Number(length);
  const bytes = n === 0 ? Buffer.alloc(0) : Buffer.from(koffi.decode(pointer, "uint8_t", n) as Buffer);
  freeFn(pointer);
  return bytes;
}

function nativeOptions(options?: TransferOptions): NativeOptions {
  return {
    profile: options?.profile ?? ProfileClassic,
    chunk_size: options?.chunkSize ?? 4096,
    rekey_every: options?.rekeyEvery ?? 0,
    ack_timeout_ms: options?.ackTimeoutMs ?? 500,
    idle_timeout_ms: options?.idleTimeoutMs ?? 5000,
  };
}

export function version(): string {
  return String(versionFn());
}

export function transferLoopback(payload: Buffer, options?: TransferOptions): Buffer {
  const out: unknown[] = [null];
  const len: unknown[] = [0];
  check(transferFn(payload, payload.length, nativeOptions(options), out, len));
  return take(out[0], len[0]);
}

export function measureLoopback(payload: Buffer, options?: TransferOptions): BenchRow {
  const row: NativeRow[] = [{} as NativeRow];
  check(measureFn(payload, payload.length, nativeOptions(options), row));
  const value = row[0];
  return {
    profile: value.profile,
    transport: value.transport,
    payloadBytes: Number(value.payload_bytes),
    chunkSize: value.chunk_size,
    rekeyEvery: value.rekey_every,
    seconds: value.seconds,
    mebibytesPerSecond: value.mebibytes_per_second,
    match: value.match !== 0,
  };
}

export function formatPublicBench(row: BenchRow): string {
  const native: NativeRow = {
    profile: row.profile,
    transport: row.transport,
    payload_bytes: BigInt(row.payloadBytes),
    chunk_size: row.chunkSize,
    rekey_every: row.rekeyEvery,
    seconds: row.seconds,
    mebibytes_per_second: row.mebibytesPerSecond,
    match: row.match ? 1 : 0,
  };
  const text: unknown[] = [null];
  check(formatBench([native], 1, text));
  const raw = String(koffi.decode(text[0], "char", 65536));
  const end = raw.indexOf("\u0000");
  freeFn(text[0]);
  return end >= 0 ? raw.slice(0, end) : raw;
}

export class Identity {
  constructor(readonly publicKey: Buffer, readonly secretKey: Buffer) {}

  static generate(): Identity {
    const publicKey = Buffer.alloc(MldsaPublicKey);
    const secretKey = Buffer.alloc(MldsaSecretKey);
    check(identityGenerate(publicKey, secretKey));
    return new Identity(publicKey, secretKey);
  }

  wipe(): void {
    identityWipe(this.secretKey, this.secretKey.length);
    this.secretKey.fill(0);
  }
}

export class Session {
  constructor(private handle: unknown) {
    if (!handle) check(-1);
  }

  setIdentity(secretKey: Buffer, peerPublicKey: Buffer): void {
    check(setIdentity(this.handle, secretKey, secretKey.length, peerPublicKey, peerPublicKey.length));
  }

  handshake(): void {
    check(handshake(this.handle));
  }

  sendAll(payload: Buffer, chunkSize = 4096, rekeyEvery = 0): void {
    check(sendAll(this.handle, payload, payload.length, chunkSize, rekeyEvery));
  }

  receiveAll(idleTimeoutMs = 5000): Buffer {
    const out: unknown[] = [null];
    const len: unknown[] = [0];
    check(receiveAll(this.handle, idleTimeoutMs, out, len));
    return take(out[0], len[0]);
  }

  close(): void {
    if (this.handle) {
      sessionDestroy(this.handle);
      this.handle = null;
    }
  }
}

export class Pipe {
  private handle: unknown = pipeCreate();

  constructor() {
    if (!this.handle) check(-1);
  }

  session(end: number, role: number, profile = ProfileClassic, ackTimeoutMs = 500): Session {
    return new Session(pipeSession(this.handle, end, role, profile, ackTimeoutMs));
  }

  close(): void {
    if (this.handle) {
      pipeDestroy(this.handle);
      this.handle = null;
    }
  }
}
