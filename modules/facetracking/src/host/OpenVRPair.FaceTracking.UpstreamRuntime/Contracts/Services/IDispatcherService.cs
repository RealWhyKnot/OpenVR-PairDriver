// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Contracts.Services preserved verbatim.
namespace VRCFaceTracking.Core.Contracts.Services;

public interface IDispatcherService
{
    public void Run(Action action);
}
