using OpenVRPair.FaceModuleHost;
using OpenVRPair.FaceModuleHost.Logging;
using OpenVRPair.FaceModuleHost.Workers;
using OpenVRPair.FaceTracking.Registry;

var opts = HostOptions.FromArgs(args);
var cts  = new CancellationTokenSource();
var ct   = cts.Token;

var logger = new HostLogger();
logger.Info("[startup] phase=logger-open");

AppDomain.CurrentDomain.UnhandledException += (s, e) => {
    try { logger.Error($"[crash] AppDomain.UnhandledException: {e.ExceptionObject}"); logger.Flush(); } catch { }
};
TaskScheduler.UnobservedTaskException += (s, e) => {
    try { logger.Error($"[crash] TaskScheduler.UnobservedTaskException: {e.Exception}"); logger.Flush(); e.SetObserved(); } catch { }
};
logger.Info("[startup] phase=crash-handlers-installed");

// Wire SIGINT / SIGTERM -> graceful shutdown.
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };
AppDomain.CurrentDomain.ProcessExit += (_, _) => cts.Cancel();

var registry = RegistryClient.Create();
var loader   = new ModuleLoader(opts, logger);
var writer   = new FrameWriter(opts.ShmemName, logger);

logger.Info($"[startup] phase=opening-ipc-pipe pipe={opts.DriverHandshakePipe}");
// Pass the same CTS so MsgShutdown cancels all workers.
var pipe     = new HostControlPipeServer(opts.DriverHandshakePipe, loader, logger, cts);
var calib    = new CalibrationCache();
var status   = new HostStatusWriter(opts.StatusFilePath, loader, logger, opts);

logger.Info($"OpenVRPair.FaceModuleHost starting. shmem={opts.ShmemName} pipe={opts.DriverHandshakePipe}");

try
{
    await writer.OpenAsync(ct);
    logger.Info("Shmem ring opened for write.");

    logger.Info($"[startup] phase=discovering-modules path={opts.ModulesInstallDir}");
    var loadedModules = await loader.LoadAllAsync();
    logger.Info($"[startup] phase=modules-loaded count={loadedModules.Count}");

    logger.Info("[startup] phase=starting-workers");
    // Start I/O workers concurrently.
    var workers = new Task[]
    {
        pipe.RunAsync(ct),
        RunRegistryPollAsync(registry, logger, ct),
        loader.RunActiveAsync(writer, calib, ct),
        status.RunAsync(ct),
    };

    logger.Info($"[startup] shared-assemblies=[{string.Join(", ", ModuleLoader.SharedAssemblyNamesSnapshot())}]");
    logger.Info("[startup] phase=running");

    // Surface any worker fault rather than silently discarding it.
    var finishedTask = await Task.WhenAny(workers);
    try
    {
        await finishedTask;
    }
    catch (OperationCanceledException) { /* normal shutdown path */ }
    catch (Exception ex)
    {
        logger.Error($"Worker faulted: {ex}");
    }

    // Cancel remaining workers and wait for them all to finish cleanly.
    await cts.CancelAsync();
    try { await Task.WhenAll(workers); }
    catch { /* individual faults already logged above or in the workers themselves */ }
}
catch (OperationCanceledException) { /* clean shutdown */ }
catch (Exception ex)
{
    logger.Error($"[crash] Main threw: {ex}");
    logger.Flush();
    await loader.UnloadAllAsync();
    writer.Dispose();
    logger.Info("Shutdown complete.");
    logger.Flush();
    logger.Dispose();
    return 1;
}

await loader.UnloadAllAsync();
writer.Dispose();
logger.Info("Shutdown complete.");
logger.Flush();
logger.Dispose();
return 0;

static async Task RunRegistryPollAsync(
    RegistryClient registry, HostLogger logger, CancellationToken ct)
{
    while (!ct.IsCancellationRequested)
    {
        try
        {
            await Task.Delay(TimeSpan.FromHours(6), ct);
            var index = await registry.GetIndexAsync(ct);
            if (index is not null)
                logger.Info($"Registry index refreshed: {index.Modules.Length} module(s) listed.");
        }
        catch (OperationCanceledException) { break; }
        catch (Exception ex)
        {
            logger.Warn($"Registry poll failed: {ex.Message}");
        }
    }
}
