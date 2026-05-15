// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Types preserved verbatim.
namespace VRCFaceTracking.Core.Types
{
    /// <summary>Structure that holds all necessary information for an image</summary>
    public class Image
    {
        /// <summary>
        /// Tuple that represents the horizontal and vertical size of the image (in pixels).
        /// </summary>
        public (int x, int y) ImageSize;

        /// <summary>
        /// Byte that contains the raw image data in RGBA. Length MUST be equal to x * y * 4
        /// </summary>
        public byte[] ImageData;

        /// <summary>
        /// Used to let VRCFaceTracking know if an image is available from the tracking interface.
        /// </summary>
        public bool SupportsImage;
    }
}
