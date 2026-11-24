import it.ctronik.ldp.Ldp;

public final class LdpTest {
    public static void main(String[] args) {
        if (!"0.2.0".equals(Ldp.version())) throw new AssertionError("version");
        byte[] payload = pattern(6000);
        Ldp.Options classic = Ldp.Options.classic();
        classic.chunkSize = 512;
        classic.rekeyEvery = 4;
        if (!java.util.Arrays.equals(Ldp.transferLoopback(payload, classic), payload)) throw new AssertionError("classic");

        Ldp.Options hybrid = Ldp.Options.classic();
        hybrid.profile = Ldp.PROFILE_HYBRID;
        hybrid.chunkSize = 256;
        byte[] hybridPayload = pattern(2048);
        if (!java.util.Arrays.equals(Ldp.transferLoopback(hybridPayload, hybrid), hybridPayload)) throw new AssertionError("hybrid");

        try (Ldp.Pipe pipe = new Ldp.Pipe()) {
            Ldp.Session server = pipe.session(1, Ldp.ROLE_SERVER, Ldp.PROFILE_CLASSIC, 500);
            Ldp.Session client = pipe.session(0, Ldp.ROLE_CLIENT, Ldp.PROFILE_CLASSIC, 500);
            byte[] body = pattern(3000);
            if (!java.util.Arrays.equals(Ldp.exchange(server, client, body, 400, 2), body)) throw new AssertionError("pipe");
            server.close();
            client.close();
        }

        try (Ldp.Identity clientId = Ldp.Identity.generate();
             Ldp.Identity serverId = Ldp.Identity.generate();
             Ldp.Pipe pipe = new Ldp.Pipe()) {
            Ldp.Session server = pipe.session(1, Ldp.ROLE_SERVER, Ldp.PROFILE_HYBRID, 500);
            Ldp.Session client = pipe.session(0, Ldp.ROLE_CLIENT, Ldp.PROFILE_HYBRID, 500);
            server.setIdentity(serverId.secretKey, clientId.publicKey);
            client.setIdentity(clientId.secretKey, serverId.publicKey);
            byte[] body = pattern(1500);
            if (!java.util.Arrays.equals(Ldp.exchange(server, client, body, 300, 0), body)) throw new AssertionError("identity");
            server.close();
            client.close();
        }

        Ldp.BenchRow row = Ldp.measureLoopback(pattern(4096), Ldp.Options.classic());
        if (!row.match || !"classic".equals(row.profile) || !"memory".equals(row.transport)) throw new AssertionError("row");
        if (!Ldp.formatBench(row).contains("ldp-sdk bench v1")) throw new AssertionError("text");
        System.out.println("java ldp tests passed");
    }

    private static byte[] pattern(int size) {
        byte[] bytes = new byte[size];
        for (int i = 0; i < size; i++) bytes[i] = (byte) (i & 0xff);
        return bytes;
    }
}
