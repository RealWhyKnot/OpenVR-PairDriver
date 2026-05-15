// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Types preserved verbatim.
using System.Runtime.InteropServices;

namespace VRCFaceTracking.Core.Types
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Vector2
    {
        public float x;
        public float y;

        public Vector2(float x, float y)
        {
            this.x = x;
            this.y = y;
        }

        public static implicit operator Vector2(Vector3 v) => new Vector2(v.x, v.y);

        public static Vector2 operator *(Vector2 a, float d)
        => new Vector2(a.x * d, a.y * d);

        public static Vector2 operator /(Vector2 a, float d)
        => new Vector2(a.x / d, a.y / d);

        public static Vector2 operator +(Vector2 a, Vector2 b)
        => new Vector2(a.x + b.x, a.y + b.y);

        public static Vector2 operator -(Vector2 a, Vector2 b)
        => new Vector2(a.x - b.x, a.y - b.y);

        public static Vector2 zero => new Vector2(0, 0);

        public Vector2 PolarTo2DCartesian(float r = 1)
        => new Vector2(r * (float)Math.Cos(x), r * (float)Math.Sin(y));

        public Vector2 FlipXCoordinates()
        {
            x *= -1;
            return this;
        }

        public float ToYaw()
        => (float)(Math.Atan(x) * (180 / Math.PI));

        public float ToPitch()
        => -(float)(Math.Atan(y) * (180 / Math.PI));
    }
}
