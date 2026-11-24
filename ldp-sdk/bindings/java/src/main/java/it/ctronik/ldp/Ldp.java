package it.ctronik.ldp;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/** Java binding of the LDP-1 C ABI. The algorithms follow FIPS 203 and FIPS 204. This build is not a CMVP module. */
public final class Ldp {
    public static final int ROLE_CLIENT = 1;
    public static final int ROLE_SERVER = 2;
    public static final int PROFILE_CLASSIC = 1;
    public static final int PROFILE_HYBRID = 2;
    public static final int MLDSA_PUBLICKEY = 1952;
    public static final int MLDSA_SECRETKEY = 4032;

    private static final Linker LINKER = Linker.nativeLinker();
    private static final Arena ARENA = Arena.global();
    private static final SymbolLookup LIB = SymbolLookup.libraryLookup(library(), ARENA);
    private static final Method TRANSFER = Method.of("ldp_transfer_loopback", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final Method MEASURE = Method.of("ldp_measure_loopback", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final Method MEASURE_UDP = Method.of("ldp_measure_udp_loopback", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final Method FREE = Method.of("ldp_free", FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
    private static final Method LAST_ERROR = Method.of("ldp_last_error", FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final Method VERSION = Method.of("ldp_version", FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final Method SIZE_OPTIONS = Method.of("ldp_sizeof_options", FunctionDescriptor.of(ValueLayout.JAVA_LONG));
    private static final Method IDENTITY = Method.of("ldp_identity_generate", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final Method WIPE = Method.of("ldp_identity_wipe", FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final Method PIPE_CREATE = Method.of("ldp_pipe_create", FunctionDescriptor.of(ValueLayout.ADDRESS));
    private static final Method PIPE_DESTROY = Method.of("ldp_pipe_destroy", FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
    private static final Method PIPE_SESSION = Method.of("ldp_pipe_session", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final Method UDP_SESSION = Method.of("ldp_udp_session_create", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_SHORT, ValueLayout.ADDRESS, ValueLayout.JAVA_SHORT, ValueLayout.JAVA_INT));
    private static final Method HANDSHAKE = Method.of("ldp_session_handshake", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    private static final Method SEND = Method.of("ldp_session_send_all", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private static final Method RECEIVE = Method.of("ldp_session_receive_all", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    private static final Method SET_IDENTITY = Method.of("ldp_session_set_identity", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG));
    private static final Method DESTROY = Method.of("ldp_session_destroy", FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
    private static final Method FORMAT = Method.of("ldp_format_public_bench", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    static {
        long size = (long) SIZE_OPTIONS.invoke();
        if (size != 20) throw new LdpException("transfer options layout does not match ldp_c");
    }

    private Ldp() {}

    public static String version() {
        return ((MemorySegment) VERSION.invoke()).reinterpret(64).getString(0);
    }

    public static byte[] transferLoopback(byte[] payload, Options options) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            check((int) TRANSFER.invoke(bytes(arena, payload), payload.length, options.segment(arena), out, len));
            return take(out, len);
        }
    }

    public static BenchRow measureLoopback(byte[] payload, Options options) {
        return measure(MEASURE, payload, options);
    }

    public static BenchRow measureUdpLoopback(byte[] payload, Options options) {
        return measure(MEASURE_UDP, payload, options);
    }

    public static String formatBench(BenchRow row) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRow = arena.allocate(104);
            writeString(nativeRow, 0, row.profile);
            writeString(nativeRow, 32, row.transport);
            nativeRow.set(ValueLayout.JAVA_LONG, 64, row.payloadBytes);
            nativeRow.set(ValueLayout.JAVA_INT, 72, row.chunkSize);
            nativeRow.set(ValueLayout.JAVA_INT, 76, row.rekeyEvery);
            nativeRow.set(ValueLayout.JAVA_DOUBLE, 80, row.seconds);
            nativeRow.set(ValueLayout.JAVA_DOUBLE, 88, row.mebibytesPerSecond);
            nativeRow.set(ValueLayout.JAVA_INT, 96, row.match ? 1 : 0);
            MemorySegment text = arena.allocate(ValueLayout.ADDRESS);
            check((int) FORMAT.invoke(nativeRow, 1L, text));
            MemorySegment ptr = text.get(ValueLayout.ADDRESS, 0);
            String value = ptr.reinterpret(1_000_000).getString(0);
            FREE.invoke(ptr);
            return value;
        }
    }

    public static byte[] exchange(Session server, Session client, byte[] payload, int chunkSize, int rekeyEvery) {
        Box box = new Box();
        Thread worker = new Thread(() -> {
            try {
                server.handshake();
                box.bytes = server.receiveAll(5000);
            } catch (RuntimeException ex) {
                box.error = ex;
            }
        });
        worker.start();
        try {
            client.handshake();
            client.sendAll(payload, chunkSize, rekeyEvery);
        } finally {
            try { worker.join(); } catch (InterruptedException ex) { Thread.currentThread().interrupt(); throw new LdpException("interrupted"); }
        }
        if (box.error != null) throw box.error;
        return box.bytes;
    }

    private static BenchRow measure(Method method, byte[] payload, Options options) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment row = arena.allocate(104);
            check((int) method.invoke(bytes(arena, payload), payload.length, options.segment(arena), row));
            return BenchRow.read(row);
        }
    }

    private static MemorySegment bytes(Arena arena, byte[] payload) {
        MemorySegment segment = arena.allocate(Math.max(payload.length, 1));
        if (payload.length > 0) MemorySegment.copy(MemorySegment.ofArray(payload), 0, segment, 0, payload.length);
        return segment;
    }

    private static byte[] take(MemorySegment outBox, MemorySegment lenBox) {
        long n = lenBox.get(ValueLayout.JAVA_LONG, 0);
        MemorySegment ptr = outBox.get(ValueLayout.ADDRESS, 0);
        byte[] bytes = new byte[(int) n];
        if (n > 0) MemorySegment.copy(ptr.reinterpret(n), ValueLayout.JAVA_BYTE, 0, bytes, 0, (int) n);
        FREE.invoke(ptr);
        return bytes;
    }

    static void check(int rc) {
        if (rc == 0) return;
        String message = ((MemorySegment) LAST_ERROR.invoke()).reinterpret(512).getString(0);
        throw new LdpException(message == null || message.isEmpty() ? "ldp error" : message);
    }

    private static void writeString(MemorySegment segment, long offset, String value) {
        byte[] bytes = value.getBytes(StandardCharsets.US_ASCII);
        MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment, offset, Math.min(bytes.length, 31));
    }

    private static Path library() {
        String env = System.getenv("LDP_C_DLL");
        if (env != null && !env.isEmpty()) return Path.of(env);
        Path dir = Path.of("").toAbsolutePath();
        while (dir != null) {
            Path candidate = dir.resolve("build").resolve("ldp_c.dll");
            if (Files.exists(candidate)) {
                Path sodium = candidate.getParent().resolve("libsodium.dll");
                if (Files.exists(sodium)) System.load(sodium.toString());
                return candidate;
            }
            dir = dir.getParent();
        }
        throw new LdpException("ldp_c.dll not found. Set LDP_C_DLL.");
    }

    public static final class Options {
        public int profile = PROFILE_CLASSIC;
        public int chunkSize = 4096;
        public int rekeyEvery = 0;
        public int ackTimeoutMs = 500;
        public int idleTimeoutMs = 5000;

        public static Options classic() { return new Options(); }

        MemorySegment segment(Arena arena) {
            MemorySegment segment = arena.allocate(20);
            segment.set(ValueLayout.JAVA_INT, 0, profile);
            segment.set(ValueLayout.JAVA_INT, 4, chunkSize);
            segment.set(ValueLayout.JAVA_INT, 8, rekeyEvery);
            segment.set(ValueLayout.JAVA_INT, 12, ackTimeoutMs);
            segment.set(ValueLayout.JAVA_INT, 16, idleTimeoutMs);
            return segment;
        }
    }

    public static final class BenchRow {
        public final String profile;
        public final String transport;
        public final long payloadBytes;
        public final int chunkSize;
        public final int rekeyEvery;
        public final double seconds;
        public final double mebibytesPerSecond;
        public final boolean match;

        BenchRow(String profile, String transport, long payloadBytes, int chunkSize, int rekeyEvery, double seconds, double mebibytesPerSecond, boolean match) {
            this.profile = profile;
            this.transport = transport;
            this.payloadBytes = payloadBytes;
            this.chunkSize = chunkSize;
            this.rekeyEvery = rekeyEvery;
            this.seconds = seconds;
            this.mebibytesPerSecond = mebibytesPerSecond;
            this.match = match;
        }

        static BenchRow read(MemorySegment row) {
            return new BenchRow(field(row, 0), field(row, 32), row.get(ValueLayout.JAVA_LONG, 64), row.get(ValueLayout.JAVA_INT, 72),
                    row.get(ValueLayout.JAVA_INT, 76), row.get(ValueLayout.JAVA_DOUBLE, 80), row.get(ValueLayout.JAVA_DOUBLE, 88), row.get(ValueLayout.JAVA_INT, 96) != 0);
        }

        private static String field(MemorySegment row, long offset) {
            return row.asSlice(offset, 32).getString(0);
        }
    }

    public static final class Identity implements AutoCloseable {
        public final byte[] publicKey;
        public final byte[] secretKey;

        private Identity(byte[] publicKey, byte[] secretKey) {
            this.publicKey = publicKey;
            this.secretKey = secretKey;
        }

        public static Identity generate() {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment pk = arena.allocate(MLDSA_PUBLICKEY);
                MemorySegment sk = arena.allocate(MLDSA_SECRETKEY);
                check((int) IDENTITY.invoke(pk, sk));
                return new Identity(pk.toArray(ValueLayout.JAVA_BYTE), sk.toArray(ValueLayout.JAVA_BYTE));
            }
        }

        public void close() {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment sk = arena.allocate(secretKey.length);
                MemorySegment.copy(MemorySegment.ofArray(secretKey), 0, sk, 0, secretKey.length);
                WIPE.invoke(sk, (long) secretKey.length);
            }
            java.util.Arrays.fill(secretKey, (byte) 0);
        }
    }

    public static final class Session implements AutoCloseable {
        private MemorySegment handle;

        Session(MemorySegment handle) {
            if (handle.address() == 0) check(-1);
            this.handle = handle;
        }

        public static Session udp(int role, int profile, int localPort, String remoteHost, int remotePort, int ackTimeoutMs) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment host = remoteHost == null ? MemorySegment.NULL : arena.allocateFrom(remoteHost);
                return new Session((MemorySegment) UDP_SESSION.invoke(role, profile, (short) localPort, host, (short) remotePort, ackTimeoutMs));
            }
        }

        public void setIdentity(byte[] secretKey, byte[] peerPublicKey) {
            try (Arena arena = Arena.ofConfined()) {
                check((int) SET_IDENTITY.invoke(handle, copy(arena, secretKey), (long) secretKey.length, copy(arena, peerPublicKey), (long) peerPublicKey.length));
            }
        }

        public void handshake() { check((int) HANDSHAKE.invoke(handle)); }

        public void sendAll(byte[] payload, int chunkSize, int rekeyEvery) {
            try (Arena arena = Arena.ofConfined()) {
                check((int) SEND.invoke(handle, bytes(arena, payload), (long) payload.length, chunkSize, rekeyEvery));
            }
        }

        public byte[] receiveAll(int idleTimeoutMs) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment out = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
                check((int) RECEIVE.invoke(handle, idleTimeoutMs, out, len));
                return take(out, len);
            }
        }

        public void close() {
            if (handle != null && handle.address() != 0) {
                DESTROY.invoke(handle);
                handle = MemorySegment.NULL;
            }
        }

        private static MemorySegment copy(Arena arena, byte[] bytes) {
            MemorySegment segment = arena.allocate(bytes.length);
            MemorySegment.copy(MemorySegment.ofArray(bytes), 0, segment, 0, bytes.length);
            return segment;
        }
    }

    public static final class Pipe implements AutoCloseable {
        private MemorySegment handle = (MemorySegment) PIPE_CREATE.invoke();

        public Pipe() {
            if (handle.address() == 0) check(-1);
        }

        public Session session(int end, int role, int profile, int ackTimeoutMs) {
            return new Session((MemorySegment) PIPE_SESSION.invoke(handle, end, role, profile, ackTimeoutMs));
        }

        public void close() {
            if (handle.address() != 0) {
                PIPE_DESTROY.invoke(handle);
                handle = MemorySegment.NULL;
            }
        }
    }

    public static final class LdpException extends RuntimeException {
        public LdpException(String message) { super(message); }
    }

    private static final class Box {
        byte[] bytes;
        RuntimeException error;
    }

    private static final class Method {
        private final java.lang.invoke.MethodHandle handle;

        private Method(java.lang.invoke.MethodHandle handle) { this.handle = handle; }

        static Method of(String name, FunctionDescriptor descriptor) {
            MemorySegment symbol = LIB.find(name).orElseThrow(() -> new LdpException("missing " + name));
            return new Method(LINKER.downcallHandle(symbol, descriptor));
        }

        Object invoke(Object... args) {
            try {
                return handle.invokeWithArguments(args);
            } catch (Throwable ex) {
                throw new LdpException(ex.getMessage() == null ? "native call failed" : ex.getMessage());
            }
        }
    }
}
