# LDP SDK

SDK 0.1.0 for the Synapse Delivery Protocol, LDP-1. It links the session engine in the sibling `ldp` folder. A developer or a DevOps engineer can compile it, move one mebibyte through the protocol, and publish a benchmark in one sitting.

This is the public trial surface. It is not a certified module, and a loopback number is not a wide-area result.

## Ten minutes

Dependencies are the same ones used by `ldp`: libsodium 1.0.20 MSVC, `mlkem-native`, and `mldsa-native`. On this tree they live in `../vendor`. The commits are in `ldp/pq/README.md`.

From this folder on Windows:

```bat
build_sdk.bat
```

The script builds `ldp-try` and `ldp-bench`, then runs a classic loopback transfer of 1 MiB. Success ends with `ldp-try ok`.

The hybrid profile, with ML-KEM-768 and ML-DSA-65, is the next command:

```bat
build\ldp-try.exe --hybrid
```

Two terminals, still on this machine, show the datagram path over UDP. The listener binds `127.0.0.1`.

```bat
build\ldp-try.exe listen --port 4590 --output received.bin
build\ldp-try.exe send --host 127.0.0.1 --port 4590 --bytes 1048576
```

`--input file.bin` sends a file instead of the counter pattern. Chunk size on UDP in this SDK defaults to 4096 bytes for the file tools. On a network beyond the local host, use 1200 bytes: that is what `ldp-bench` uses for the UDP row. The engine does not fragment a chunk across datagrams.

## Public benchmark

```bat
build\ldp-bench.exe
```

The tool prints a text report and writes `ldp-bench-result.txt` in the current directory. The first lines are the format, version 1:

```text
# ldp-sdk bench v1
# sdk 0.1.0
# protocol LDP-1 Synapse Delivery Protocol
# columns profile transport payload_bytes chunk_size rekey_every seconds MiB/s match
```

Each following line is one row: `classic` or `hybrid-pq1`, `memory` or `udp-loopback`, then the payload size, the chunk size, the rekey interval, seconds, mebibytes per second, and `match`. `match` means the receiver assembled the same bytes the sender passed in. A row with `MISMATCH` is a failed run.

The matrix is fixed:

| Profile | Transport | Payload | Chunk | Rekey |
| --- | --- | --- | --- | --- |
| classic | memory | 1 MiB | 4096 | none |
| classic | memory | 1 MiB | 4096 | every 64 chunks |
| hybrid-pq1 | memory | 256 KiB | 4096 | none |
| classic | udp-loopback | 1 MiB | 1200 | none |

Publish the result file with the SDK version, the CPU, and the OS. The seconds will differ between machines. The format is what another implementation can print so the two reports can be compared. That is the route toward a de facto interchange: the same protocol, the same columns, a payload that either matches or does not.

## API

`include/ldp/sdk.hpp` is the supported header.

- `transfer_loopback` runs a client and a server in one process.
- `measure_loopback` and `measure_udp_loopback` return one `BenchRow`.
- `udp_listen` and `udp_send` are the two-process tools.
- `run_public_bench` and `format_public_bench` produce the report above.

`Profile::Classic` is X25519 and XChaCha20-Poly1305. `Profile::HybridPq1` adds ML-KEM-768 and pinned ML-DSA-65 identities. The SDK generates those identities for the in-process calls. The two-process commands in 0.1.0 are classic only.

The algorithms follow FIPS 203 and FIPS 204. This build is not a CMVP-validated module.

## Paid tools, not in this SDK

This SDK and the LDP-1 engine are the open part. Three C-Tronik tools are sold separately, by annual invoice, and are not in this tree:

- **SynapseMFT Governance Suite** — approval, audit, and node policy. Starter €6,000 a year (10 nodes). Professional €18,000 a year (50 nodes, four-eyes, event bus, 10 desktop seats). Enterprise from €36,000 a year on a quote.
- **Virtual Desktop** — one named operator console. €1,200 a seat a year. Ten seats are inside Professional. It is sold only with Governance.
- **Synapse Event Bus** — encrypted events for transfer and governance. Included in Professional and Enterprise. On its own, or added to Starter: €4,800 a year up to 10 nodes, €9,600 a year up to 50.

A perpetual license exists only for the Governance Suite: three times the annual fee, then maintenance at 22% from the second year. Prices exclude VAT. The order is placed from the C-Tronik portal page for SynapseMFT. The product site describes the three tools and links back to that page. Nothing in `ldp-try` or `ldp-bench` enables those tools.

## Layout

| Path | Role |
| --- | --- |
| `include/ldp/sdk.hpp` | public API |
| `src/` | loopback, UDP transport, benchmark |
| `tools/ldp_try.cpp` | ten-minute command |
| `tools/ldp_bench.cpp` | public benchmark |
| `../ldp` | LDP-1 session engine |

The engine still has no congestion control wired into the session, and the UDP helper in this SDK is a datagram pipe for trial and measurement. A second implementation that speaks the same preamble (`LDP1`, version 1.0) can be checked against `ldp-try` and can publish the same benchmark text.
