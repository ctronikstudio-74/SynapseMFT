import { formatPublicBench, Identity, measureLoopback, ProfileHybrid, transferLoopback, version } from "./index.js";
function pattern(size) {
    const bytes = Buffer.alloc(size);
    for (let i = 0; i < size; i++)
        bytes[i] = i & 0xff;
    return bytes;
}
const payload = pattern(6000);
if (!transferLoopback(payload, { chunkSize: 512, rekeyEvery: 4 }).equals(payload))
    throw new Error("classic bytes");
const hybridPayload = pattern(2048);
if (!transferLoopback(hybridPayload, { profile: ProfileHybrid, chunkSize: 256 }).equals(hybridPayload))
    throw new Error("hybrid bytes");
const client = Identity.generate();
const server = Identity.generate();
client.wipe();
server.wipe();
const row = measureLoopback(pattern(4096));
if (!row.match || row.profile !== "classic" || row.transport !== "memory")
    throw new Error("bench row");
if (!formatPublicBench(row).includes("ldp-sdk bench v1"))
    throw new Error("bench text");
if (version() !== "0.2.0")
    throw new Error("version");
console.log("typescript ldp tests passed");
