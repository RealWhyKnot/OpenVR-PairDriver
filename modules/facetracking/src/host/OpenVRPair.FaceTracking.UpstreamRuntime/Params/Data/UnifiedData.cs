// Vendored from VRCFaceTracking (Apache-2.0) -- namespace VRCFaceTracking.Core.Params.Data preserved verbatim.
using VRCFaceTracking.Core.Params.Expressions;
using VRCFaceTracking.Core.Types;

namespace VRCFaceTracking.Core.Params.Data
{
    /// <summary>
    /// Struct that represents a single eye.
    /// </summary>
    public struct UnifiedSingleEyeData
    {
        public Vector2 Gaze;
        public float PupilDiameter_MM;
        public float Openness = 1.0f;

        public UnifiedSingleEyeData() {}
    }

    /// <summary>
    /// Struct that represents all possible eye data.
    /// </summary>
    public class UnifiedEyeData
    {
        public UnifiedSingleEyeData Left = new (), Right = new ();
        public float _maxDilation, _minDilation = 999f;
        public float _leftDiameter, _rightDiameter;

        public UnifiedSingleEyeData Combined()
        {
            var averageDilation = (Left.PupilDiameter_MM + Right.PupilDiameter_MM) / 2.0f;
            var leftDiff = Math.Abs(_leftDiameter - Left.PupilDiameter_MM) > 0f;
            var rightDiff = Math.Abs(_rightDiameter - Right.PupilDiameter_MM) > 0f;

            if (leftDiff || rightDiff)
            {
                if (averageDilation > _maxDilation)
                    _maxDilation = averageDilation;
                else if (averageDilation < _minDilation)
                    _minDilation = averageDilation;
            }
            if (leftDiff)
                _leftDiameter = Left.PupilDiameter_MM;
            if (rightDiff)
                _rightDiameter = Right.PupilDiameter_MM;

            var normalizedDilation = (averageDilation - _minDilation) / (_maxDilation - _minDilation);

            return new UnifiedSingleEyeData
            {
                Gaze = (Left.Gaze + Right.Gaze) / 2.0f,
                Openness = (Left.Openness + Right.Openness) / 2.0f,
                PupilDiameter_MM = float.IsNaN(normalizedDilation) ? 0.5f : normalizedDilation,
            };
        }

        public void CopyPropertiesOf(UnifiedEyeData data)
        {
            data.Combined();
            this.Left = data.Left;
            this.Right = data.Right;
            this._maxDilation = data._maxDilation;
            this._minDilation = data._minDilation;
            this._rightDiameter = data._rightDiameter;
            this._leftDiameter = data._leftDiameter;
        }
    }

    /// <summary>
    /// Container of information pertaining to a singular Unified Expression shape.
    /// </summary>
    public struct UnifiedExpressionShape
    {
        public float Weight;
    }

    public struct UnifiedHeadData
    {
        /// <summary>
        /// Head rotation values normalized to [-1, 1] representing -90d to 90d rotation.
        ///   Yaw - Y axis (positive = looking right)
        ///   Pitch - X axis (positive = looking down)
        ///   Roll - Z axis (positive = side-tilt left)
        /// Head position normalized to [-1, 1] representing ~0.5m deviation from origin.
        /// </summary>
        public float HeadYaw;
        public float HeadPitch;
        public float HeadRoll;

        public float HeadPosX;
        public float HeadPosY;
        public float HeadPosZ;
    }

    /// <summary>
    /// All data that is accessible by modules and is output to parameters.
    /// </summary>
    public class UnifiedTrackingData
    {
        public UnifiedEyeData Eye = new UnifiedEyeData();

        public UnifiedExpressionShape[] Shapes = new UnifiedExpressionShape[(int)UnifiedExpressions.Max + 1];

        public UnifiedHeadData Head = new UnifiedHeadData();

        public void CopyPropertiesOf(UnifiedTrackingData data)
        {
            Eye.CopyPropertiesOf(data.Eye);
            for (int i = 0; i < Shapes.Length; i++)
                Shapes[i].Weight = data.Shapes[i].Weight;

            Head.HeadYaw   = data.Head.HeadYaw;
            Head.HeadPitch = data.Head.HeadPitch;
            Head.HeadRoll  = data.Head.HeadRoll;
            Head.HeadPosX  = data.Head.HeadPosX;
            Head.HeadPosY  = data.Head.HeadPosY;
            Head.HeadPosZ  = data.Head.HeadPosZ;
        }
    }
}
