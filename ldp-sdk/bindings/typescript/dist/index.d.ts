export declare const RoleClient = 1;
export declare const RoleServer = 2;
export declare const ProfileClassic = 1;
export declare const ProfileHybrid = 2;
export declare const MldsaPublicKey = 1952;
export declare const MldsaSecretKey = 4032;
export declare class LdpError extends Error {
}
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
export declare function version(): string;
export declare function transferLoopback(payload: Buffer, options?: TransferOptions): Buffer;
export declare function measureLoopback(payload: Buffer, options?: TransferOptions): BenchRow;
export declare function formatPublicBench(row: BenchRow): string;
export declare class Identity {
    readonly publicKey: Buffer;
    readonly secretKey: Buffer;
    constructor(publicKey: Buffer, secretKey: Buffer);
    static generate(): Identity;
    wipe(): void;
}
export declare class Session {
    private handle;
    constructor(handle: unknown);
    setIdentity(secretKey: Buffer, peerPublicKey: Buffer): void;
    handshake(): void;
    sendAll(payload: Buffer, chunkSize?: number, rekeyEvery?: number): void;
    receiveAll(idleTimeoutMs?: number): Buffer;
    close(): void;
}
export declare class Pipe {
    private handle;
    constructor();
    session(end: number, role: number, profile?: number, ackTimeoutMs?: number): Session;
    close(): void;
}
