using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceTracking.ModuleSdk;

namespace OpenVRPair.FaceModuleHost.Output;

/// <summary>
/// Sends Unified Expression and eye gaze values as OSC float messages to
/// /avatar/parameters/<paramName> over UDP. Default target: 127.0.0.1:9000.
/// </summary>
public sealed class OscSender(string host, int port, HostLogger logger) : IDisposable
{
    private UdpClient?  _udp;
    private IPEndPoint? _endpoint;
    private bool        _running;

    // Stats exposed via the read-only properties below. Updated lock-free on the
    // module-tick thread; readers (HostStatusWriter on the status-poll thread) see
    // eventually-consistent snapshots, which is fine for a 1 Hz UI refresh.
    private long _packetsSent;
    private long _packetsErrored;
    private long _lastErrorTicks;            // DateTime.UtcNow.Ticks at last error
    private string? _lastErrorMessage;

    public string  TargetHost      => host;
    public int     TargetPort      => port;
    public bool    IsRunning       => _running;
    public long    PacketsSent     => Interlocked.Read(ref _packetsSent);
    public long    PacketsErrored  => Interlocked.Read(ref _packetsErrored);
    public string? LastErrorMessage => _lastErrorMessage;
    public DateTime? LastErrorAt
    {
        get
        {
            long t = Interlocked.Read(ref _lastErrorTicks);
            return t == 0 ? null : new DateTime(t, DateTimeKind.Utc);
        }
    }

    public async Task RunAsync(CancellationToken ct)
    {
        try
        {
            _endpoint = new IPEndPoint(IPAddress.Parse(host), port);
            _udp      = new UdpClient();

            // Port collision detection: attempt to bind a listening socket on the same
            // port. If it fails, another process (e.g. a standalone VRCFT install) owns
            // the port; log a warning but continue sending (the remote process will receive
            // the datagrams instead, which is harmless for our OSC output).
            try
            {
                using var probe = new UdpClient(new IPEndPoint(IPAddress.Loopback, port));
            }
            catch (SocketException)
            {
                Console.Error.WriteLine(
                    $"[OscSender] Port {port} already in use -- another OSC consumer " +
                    $"may be running. Packets will be received by that process.");
                logger.Warn($"OSC port {port} already bound by another process.");
            }

            _running = true;
            logger.Info($"OSC sender ready -> {host}:{port}");

            // Keep-alive; actual sends happen from SendFrame() on the module tick thread.
            await Task.Delay(Timeout.Infinite, ct);
        }
        catch (OperationCanceledException) { /* clean shutdown */ }
        finally
        {
            _running = false;
        }
    }

    public void SendFrame(
        EyeFrameSink eye,
        ExpressionFrameSink expr,
        bool eyeValid,
        bool exprValid)
    {
        if (_udp is null || _endpoint is null || !_running) return;

        try
        {
            if (eyeValid)
            {
                SendFloat("/avatar/parameters/LeftEyeX",        eye.Left.DirHmd.X);
                SendFloat("/avatar/parameters/LeftEyeY",        eye.Left.DirHmd.Y);
                SendFloat("/avatar/parameters/RightEyeX",       eye.Right.DirHmd.X);
                SendFloat("/avatar/parameters/RightEyeY",       eye.Right.DirHmd.Y);
                SendFloat("/avatar/parameters/LeftEyeLid",      eye.LeftOpenness);
                SendFloat("/avatar/parameters/RightEyeLid",     eye.RightOpenness);
                SendFloat("/avatar/parameters/EyesDilation",
                    (eye.PupilDilationLeft + eye.PupilDilationRight) * 0.5f);
            }

            if (exprValid)
            {
                ReadOnlySpan<float> values = expr.Values;
                int count = Math.Min(values.Length, (int)UnifiedExpression.Count);
                for (int i = 0; i < count; i++)
                {
                    string paramName = UnifiedExpressionParams.GetParamName(i);
                    SendFloat($"/avatar/parameters/{paramName}", values[i]);
                }
            }
        }
        catch (Exception ex)
        {
            // OSC send errors are non-fatal; log once and swallow.
            logger.Warn($"OSC send error: {ex.Message}");
        }
    }

    private void SendFloat(string address, float value)
    {
        byte[] packet = BuildOscFloatPacket(address, value);
        try
        {
            _udp!.Send(packet, packet.Length, _endpoint);
            Interlocked.Increment(ref _packetsSent);
        }
        catch (Exception ex)
        {
            Interlocked.Increment(ref _packetsErrored);
            Interlocked.Exchange(ref _lastErrorTicks, DateTime.UtcNow.Ticks);
            _lastErrorMessage = ex.Message;
            // Don't log every send error or we flood the log at frame rate;
            // SendFrame's surrounding catch logs once per outer call.
            throw;
        }
    }

    /// <summary>Builds a minimal OSC 1.0 float message packet.</summary>
    private static byte[] BuildOscFloatPacket(string address, float value)
    {
        // OSC string is null-terminated and padded to a 4-byte boundary.
        int addrLen    = address.Length + 1;
        int addrPadded = (addrLen + 3) & ~3;

        // Type tag string: ",f\0\0"
        const int typePadded = 4;
        const int floatBytes = 4;

        byte[] packet = new byte[addrPadded + typePadded + floatBytes];

        Encoding.ASCII.GetBytes(address, packet.AsSpan(0, address.Length));
        // null terminator + padding already zeroed by array initializer

        int typeOffset = addrPadded;
        packet[typeOffset + 0] = (byte)',';
        packet[typeOffset + 1] = (byte)'f';
        // [2] and [3] remain 0

        // Big-endian float (OSC spec).
        int floatOffset = typeOffset + typePadded;
        int bits = BitConverter.SingleToInt32Bits(value);
        packet[floatOffset + 0] = (byte)(bits >> 24);
        packet[floatOffset + 1] = (byte)(bits >> 16);
        packet[floatOffset + 2] = (byte)(bits >>  8);
        packet[floatOffset + 3] = (byte)(bits);

        return packet;
    }

    public void Dispose()
    {
        _running = false;
        _udp?.Dispose();
    }
}
