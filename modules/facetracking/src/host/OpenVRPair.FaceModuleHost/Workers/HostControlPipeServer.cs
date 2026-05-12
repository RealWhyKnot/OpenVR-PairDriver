using System.Formats.Cbor;
using System.IO.Pipes;
using OpenVRPair.FaceModuleHost.Logging;

namespace OpenVRPair.FaceModuleHost.Workers;

/// <summary>
/// Accepts and processes driver control messages over a named pipe.
/// Messages are length-prefixed (4-byte little-endian uint) CBOR-encoded maps.
/// </summary>
public sealed class HostControlPipeServer(
    string pipeName,
    ModuleLoader loader,
    HostLogger logger,
    CancellationTokenSource shutdownCts)
{
    // Message type tags sent by the driver.
    private const string MsgSelectModule  = "SelectModule";
    private const string MsgUpdateConfig  = "UpdateConfig";
    private const string MsgShutdown      = "Shutdown";

    public async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await using var pipe = new NamedPipeServerStream(
                    pipeName.TrimStart('\\').Split('\\').Last(),
                    PipeDirection.InOut,
                    maxNumberOfServerInstances: 1,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);

                logger.Info($"Control pipe listening on {pipeName}...");
                await pipe.WaitForConnectionAsync(ct);
                logger.Info("Driver connected to control pipe.");

                await HandleConnectionAsync(pipe, ct);
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                logger.Warn($"Control pipe error: {ex.Message}; restarting listener.");
                await Task.Delay(500, ct);
            }
        }
    }

    private async Task HandleConnectionAsync(NamedPipeServerStream pipe, CancellationToken ct)
    {
        byte[] lenBuf = new byte[4];
        while (!ct.IsCancellationRequested && pipe.IsConnected)
        {
            // Read 4-byte length prefix.
            int read = await ReadExactAsync(pipe, lenBuf, 4, ct);
            if (read < 4) break;

            uint msgLen = BitConverter.ToUInt32(lenBuf, 0);
            if (msgLen == 0 || msgLen > 65536)
            {
                logger.Warn($"Control pipe: invalid message length {msgLen}; dropping connection.");
                break;
            }

            byte[] msgBuf = new byte[msgLen];
            read = await ReadExactAsync(pipe, msgBuf, (int)msgLen, ct);
            if (read < (int)msgLen) break;

            try   { DispatchMessage(msgBuf); }
            catch (Exception ex) { logger.Warn($"Control pipe: message dispatch error: {ex.Message}"); }
        }
    }

    private void DispatchMessage(byte[] data)
    {
        var reader = new CborReader(data, CborConformanceMode.Lax);
        reader.ReadStartMap();

        string? msgType = null;
        string? uuid    = null;

        while (reader.PeekState() != CborReaderState.EndMap)
        {
            string key = reader.ReadTextString();
            switch (key)
            {
                case "type":   msgType = reader.ReadTextString(); break;
                case "uuid":   uuid    = reader.ReadTextString(); break;
                // Additional keys (OscConfig etc.) are skipped here; extend as needed.
                default:       reader.SkipValue();                break;
            }
        }
        reader.ReadEndMap();

        switch (msgType)
        {
            case MsgSelectModule when uuid is not null:
                loader.SelectActive(uuid);
                break;

            case MsgShutdown:
                logger.Info("Driver requested shutdown via control pipe.");
                // Cancel the host-wide CTS so all workers wind down.
                shutdownCts.Cancel();
                break;

            default:
                logger.Warn($"Control pipe: unknown message type '{msgType}'.");
                break;
        }
    }

    private static async Task<int> ReadExactAsync(
        PipeStream pipe, byte[] buf, int count, CancellationToken ct)
    {
        int total = 0;
        while (total < count)
        {
            int n = await pipe.ReadAsync(buf.AsMemory(total, count - total), ct);
            if (n == 0) break;
            total += n;
        }
        return total;
    }
}
