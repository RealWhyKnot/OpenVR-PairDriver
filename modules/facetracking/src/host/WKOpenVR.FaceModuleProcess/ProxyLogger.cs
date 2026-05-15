// ----------------------------------------------------------------------------
// Vendored from VRCFaceTracking (Apache-2.0).
// Original: https://github.com/benaclejames/VRCFaceTracking/blob/35857c01315c32e0e45dcde2f6f8fe495216fa0c/
//   VRCFaceTracking.ModuleProcess/ProxyLogger.cs
// Copyright (c) benaclejames and contributors. Licensed under Apache 2.0.
// Modifications: namespace renamed to WKOpenVR.FaceModuleProcess.
// ----------------------------------------------------------------------------
using System.Collections.ObjectModel;
using Microsoft.Extensions.Logging;

namespace WKOpenVR.FaceModuleProcess;

public delegate void OnLog(LogLevel level, string msg);

public class ProxyLogger : ILogger
{
    private readonly string _categoryName;
    public static OnLog OnLog;

    public ProxyLogger(string categoryName)
    {
        _categoryName = categoryName;
    }

    public IDisposable BeginScope<TState>(TState state) where TState : notnull => default!;

    public bool IsEnabled(LogLevel logLevel) => true;

    public void Log<TState>(
        LogLevel logLevel,
        EventId eventId,
        TState state,
        Exception? exception,
        Func<TState, Exception?, string> formatter)
    {
        if ( OnLog != null )
        {
            OnLog(logLevel, $"[{_categoryName}] {logLevel}: {formatter(state, exception)}");
        }
    }
}
