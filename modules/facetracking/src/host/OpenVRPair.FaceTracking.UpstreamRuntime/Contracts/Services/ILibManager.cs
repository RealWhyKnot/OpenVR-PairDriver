// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Contracts.Services preserved verbatim.
using System.Collections.ObjectModel;

namespace VRCFaceTracking.Core.Contracts.Services;

public interface ILibManager
{
    public ObservableCollection<ModuleMetadataInternal> LoadedModulesMetadata { get; set; }
    public void Initialize();
    void TeardownAllAndResetAsync();
}
