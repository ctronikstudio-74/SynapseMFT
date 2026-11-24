using Ctronik.Ldp;

var classic = TransferOptions.Classic();
classic.ChunkSize = 512;
classic.RekeyEvery = 4;
var payload = Pattern(6000);
var received = Ldp.TransferLoopback(payload, classic);
if (!received.AsSpan().SequenceEqual(payload)) throw new Exception("classic bytes");

var hybrid = TransferOptions.Classic();
hybrid.Profile = Ldp.ProfileHybrid;
hybrid.ChunkSize = 256;
var hybridPayload = Pattern(2048);
if (!Ldp.TransferLoopback(hybridPayload, hybrid).AsSpan().SequenceEqual(hybridPayload)) throw new Exception("hybrid bytes");

using (var pipe = new Pipe())
{
    using var server = pipe.Session(1, Ldp.RoleServer);
    using var client = pipe.Session(0, Ldp.RoleClient);
    var body = Pattern(3000);
    if (!Ldp.Exchange(server, client, body, 400, 2).AsSpan().SequenceEqual(body)) throw new Exception("pipe bytes");
}

using (var clientId = Identity.Generate())
using (var serverId = Identity.Generate())
using (var pipe = new Pipe())
{
    using var server = pipe.Session(1, Ldp.RoleServer, Ldp.ProfileHybrid);
    using var client = pipe.Session(0, Ldp.RoleClient, Ldp.ProfileHybrid);
    server.SetIdentity(serverId.SecretKey, clientId.PublicKey);
    client.SetIdentity(clientId.SecretKey, serverId.PublicKey);
    var body = Pattern(1500);
    if (!Ldp.Exchange(server, client, body, 300, 0).AsSpan().SequenceEqual(body)) throw new Exception("identity bytes");
}

var row = Ldp.MeasureLoopback(Pattern(4096));
if (row.Match == 0 || row.Profile != "classic" || row.Transport != "memory") throw new Exception("bench row");
if (!Ldp.FormatPublicBench(new[] { row }).Contains("ldp-sdk bench v1")) throw new Exception("bench text");
if (Ldp.Version != "0.2.0") throw new Exception("version");
Console.WriteLine("csharp ldp tests passed");

static byte[] Pattern(int size)
{
    var bytes = new byte[size];
    for (var i = 0; i < size; i++) bytes[i] = (byte)(i & 0xff);
    return bytes;
}
