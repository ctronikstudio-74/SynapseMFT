using System.Runtime.InteropServices;

namespace Ctronik.Ldp;

public sealed class LdpException : Exception
{
    public LdpException(string message) : base(message) { }
}

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct TransferOptions
{
    public int Profile;
    public uint ChunkSize;
    public uint RekeyEvery;
    public int AckTimeoutMs;
    public int IdleTimeoutMs;

    public static TransferOptions Classic()
    {
        var options = new TransferOptions();
        Native.ldp_options_init(ref options);
        return options;
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Ansi)]
public struct BenchRow
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Profile;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Transport;
    public ulong PayloadBytes;
    public uint ChunkSize;
    public uint RekeyEvery;
    public double Seconds;
    public double MebibytesPerSecond;
    public int Match;
}

public static class Ldp
{
    public const int RoleClient = 1;
    public const int RoleServer = 2;
    public const int ProfileClassic = 1;
    public const int ProfileHybrid = 2;
    public const int MldsaPublicKey = 1952;
    public const int MldsaSecretKey = 4032;

    public static string Version => Marshal.PtrToStringAnsi(Native.ldp_version()) ?? "";

    public static byte[] TransferLoopback(byte[] payload, TransferOptions? options = null)
    {
        var owned = options ?? TransferOptions.Classic();
        return WithPayload(payload, ptr =>
        {
            Check(Native.ldp_transfer_loopback(ptr, (nuint)payload.Length, ref owned, out var data, out var len));
            return Take(data, len);
        });
    }

    public static BenchRow MeasureLoopback(byte[] payload, TransferOptions? options = null)
    {
        var owned = options ?? TransferOptions.Classic();
        var row = new BenchRow();
        WithPayload(payload, ptr =>
        {
            Check(Native.ldp_measure_loopback(ptr, (nuint)payload.Length, ref owned, ref row));
            return 0;
        });
        return row;
    }

    public static BenchRow MeasureUdpLoopback(byte[] payload, TransferOptions? options = null)
    {
        var owned = options ?? TransferOptions.Classic();
        var row = new BenchRow();
        WithPayload(payload, ptr =>
        {
            Check(Native.ldp_measure_udp_loopback(ptr, (nuint)payload.Length, ref owned, ref row));
            return 0;
        });
        return row;
    }

    public static void UdpListen(ushort port, string outputPath, TransferOptions? options = null)
    {
        var owned = options ?? TransferOptions.Classic();
        Check(Native.ldp_udp_listen(port, outputPath, ref owned));
    }

    public static void UdpSend(string host, ushort port, byte[] payload, TransferOptions? options = null)
    {
        var owned = options ?? TransferOptions.Classic();
        WithPayload(payload, ptr =>
        {
            Check(Native.ldp_udp_send(host, port, ptr, (nuint)payload.Length, ref owned));
            return 0;
        });
    }

    public static BenchRow[] RunPublicBench()
    {
        Check(Native.ldp_run_public_bench(null, 0, out var count), expectSpace: true);
        var rows = new BenchRow[count];
        Check(Native.ldp_run_public_bench(rows, (nuint)rows.Length, out count));
        return rows;
    }

    public static string FormatPublicBench(BenchRow[] rows)
    {
        Check(Native.ldp_format_public_bench(rows, (nuint)rows.Length, out var text));
        var value = Marshal.PtrToStringAnsi(text) ?? "";
        Native.ldp_free(text);
        return value;
    }

    public static byte[] Exchange(Session server, Session client, byte[] payload, uint chunkSize = 512, uint rekeyEvery = 0)
    {
        byte[]? received = null;
        Exception? error = null;
        var worker = new Thread(() =>
        {
            try
            {
                server.Handshake();
                received = server.ReceiveAll();
            }
            catch (Exception ex)
            {
                error = ex;
            }
        });
        worker.Start();
        try
        {
            client.Handshake();
            client.SendAll(payload, chunkSize, rekeyEvery);
        }
        finally
        {
            worker.Join();
        }
        if (error != null) throw error;
        return received ?? Array.Empty<byte>();
    }

    internal static void Check(int rc, bool expectSpace = false)
    {
        if (rc == 0) return;
        if (expectSpace && rc == -3) return;
        var message = Marshal.PtrToStringAnsi(Native.ldp_last_error());
        throw new LdpException(string.IsNullOrEmpty(message) ? "ldp error" : message);
    }

    internal static byte[] Take(IntPtr data, nuint len)
    {
        var bytes = new byte[(int)len];
        if (len > 0) Marshal.Copy(data, bytes, 0, (int)len);
        Native.ldp_free(data);
        return bytes;
    }

    internal static T WithPayload<T>(byte[] payload, Func<IntPtr, T> call)
    {
        if (payload.Length == 0) return call(IntPtr.Zero);
        var pin = GCHandle.Alloc(payload, GCHandleType.Pinned);
        try
        {
            return call(pin.AddrOfPinnedObject());
        }
        finally
        {
            pin.Free();
        }
    }
}

public sealed class Identity : IDisposable
{
    public byte[] PublicKey { get; }
    public byte[] SecretKey { get; }

    Identity(byte[] publicKey, byte[] secretKey)
    {
        PublicKey = publicKey;
        SecretKey = secretKey;
    }

    public static Identity Generate()
    {
        var publicKey = new byte[Ldp.MldsaPublicKey];
        var secretKey = new byte[Ldp.MldsaSecretKey];
        Ldp.Check(Native.ldp_identity_generate(publicKey, secretKey));
        return new Identity(publicKey, secretKey);
    }

    public void Dispose()
    {
        Native.ldp_identity_wipe(SecretKey, (nuint)SecretKey.Length);
        Array.Clear(SecretKey, 0, SecretKey.Length);
    }
}

public sealed class Session : IDisposable
{
    internal IntPtr Handle;

    internal Session(IntPtr handle)
    {
        if (handle == IntPtr.Zero) Ldp.Check(-1);
        Handle = handle;
    }

    public static Session Udp(int role, int profile = Ldp.ProfileClassic, ushort localPort = 0, string? remoteHost = null, ushort remotePort = 0, int ackTimeoutMs = 500)
    {
        return new Session(Native.ldp_udp_session_create(role, profile, localPort, remoteHost, remotePort, ackTimeoutMs));
    }

    public void SetIdentity(byte[] secretKey, byte[] peerPublicKey)
    {
        Ldp.Check(Native.ldp_session_set_identity(Handle, secretKey, (nuint)secretKey.Length, peerPublicKey, (nuint)peerPublicKey.Length));
    }

    public void Handshake() => Ldp.Check(Native.ldp_session_handshake(Handle));

    public bool Established => Native.ldp_session_established(Handle) == 1;

    public void SendAll(byte[] payload, uint chunkSize = 4096, uint rekeyEvery = 0)
    {
        Ldp.WithPayload(payload, ptr =>
        {
            Ldp.Check(Native.ldp_session_send_all(Handle, ptr, (nuint)payload.Length, chunkSize, rekeyEvery));
            return 0;
        });
    }

    public byte[] ReceiveAll(int idleTimeoutMs = 5000)
    {
        Ldp.Check(Native.ldp_session_receive_all(Handle, idleTimeoutMs, out var data, out var len));
        return Ldp.Take(data, len);
    }

    public ushort LocalPort()
    {
        Ldp.Check(Native.ldp_udp_local_port(Handle, out var port));
        return port;
    }

    public void Dispose()
    {
        if (Handle != IntPtr.Zero)
        {
            Native.ldp_session_destroy(Handle);
            Handle = IntPtr.Zero;
        }
    }
}

public sealed class Pipe : IDisposable
{
    IntPtr _handle;

    public Pipe()
    {
        _handle = Native.ldp_pipe_create();
        if (_handle == IntPtr.Zero) Ldp.Check(-1);
    }

    public Session Session(int end, int role, int profile = Ldp.ProfileClassic, int ackTimeoutMs = 500)
    {
        return new Session(Native.ldp_pipe_session(_handle, end, role, profile, ackTimeoutMs));
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            Native.ldp_pipe_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }
}

static class Native
{
    static Native()
    {
        var dir = Path.GetDirectoryName(LibraryPath())!;
        Environment.SetEnvironmentVariable("PATH", dir + Path.PathSeparator + Environment.GetEnvironmentVariable("PATH"));
        NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, _, _) =>
            name == "ldp_c" ? NativeLibrary.Load(LibraryPath()) : IntPtr.Zero);
        if (Marshal.SizeOf<TransferOptions>() != (int)ldp_sizeof_options())
            throw new LdpException("transfer options layout does not match ldp_c");
        if (Marshal.SizeOf<BenchRow>() != (int)ldp_sizeof_bench_row())
            throw new LdpException("bench row layout does not match ldp_c");
    }

    public static string LibraryPath()
    {
        var env = Environment.GetEnvironmentVariable("LDP_C_DLL");
        if (!string.IsNullOrEmpty(env)) return env;
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            var candidate = Path.Combine(dir.FullName, "build", "ldp_c.dll");
            if (File.Exists(candidate)) return candidate;
            dir = dir.Parent;
        }
        throw new LdpException("ldp_c.dll not found. Set LDP_C_DLL.");
    }

    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ldp_version();
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ldp_last_error();
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern nuint ldp_sizeof_options();
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern nuint ldp_sizeof_bench_row();
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern void ldp_free(IntPtr block);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern void ldp_options_init(ref TransferOptions options);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_identity_generate(byte[] publicKey, byte[] secretKey);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern void ldp_identity_wipe(byte[] secretKey, nuint len);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern void ldp_session_destroy(IntPtr session);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_session_set_identity(IntPtr session, byte[] secretKey, nuint secretLen, byte[] peerPublicKey, nuint publicLen);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_session_handshake(IntPtr session);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_session_established(IntPtr session);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_session_send_all(IntPtr session, IntPtr data, nuint len, uint chunkSize, uint rekeyEvery);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_session_receive_all(IntPtr session, int idleTimeoutMs, out IntPtr data, out nuint len);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ldp_pipe_create();
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern void ldp_pipe_destroy(IntPtr pipe);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ldp_pipe_session(IntPtr pipe, int end, int role, int profile, int ackTimeoutMs);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern IntPtr ldp_udp_session_create(int role, int profile, ushort localPort, string? remoteHost, ushort remotePort, int ackTimeoutMs);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_udp_local_port(IntPtr session, out ushort port);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_transfer_loopback(IntPtr payload, nuint len, ref TransferOptions options, out IntPtr data, out nuint outLen);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_measure_loopback(IntPtr payload, nuint len, ref TransferOptions options, ref BenchRow row);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_measure_udp_loopback(IntPtr payload, nuint len, ref TransferOptions options, ref BenchRow row);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)] public static extern int ldp_udp_listen(ushort port, string outputPath, ref TransferOptions options);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)] public static extern int ldp_udp_send(string host, ushort port, IntPtr payload, nuint len, ref TransferOptions options);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_run_public_bench([Out] BenchRow[]? rows, nuint capacity, out nuint count);
    [DllImport("ldp_c", CallingConvention = CallingConvention.Cdecl)] public static extern int ldp_format_public_bench([In] BenchRow[] rows, nuint count, out IntPtr text);
}
