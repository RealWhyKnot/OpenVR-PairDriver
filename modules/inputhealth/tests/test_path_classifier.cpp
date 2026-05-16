#include <gtest/gtest.h>
#include "inputhealth/PathClassifier.h"

using namespace inputhealth;

// ---------------------------------------------------------------------------
// Trigger paths
// ---------------------------------------------------------------------------

TEST(PathClassifier, TriggerValuePath)
{
    EXPECT_EQ(ClassifyInputPath("/input/trigger/value"), PathClass::Trigger);
}

TEST(PathClassifier, TriggerValueCaseSensitive)
{
    // Driver may uppercase "Trigger" on some devices.
    EXPECT_EQ(ClassifyInputPath("/input/Trigger/value"), PathClass::Trigger);
}

TEST(PathClassifier, TriggerClickIsButton)
{
    // trigger/click is a boolean button, not a scalar trigger.
    EXPECT_EQ(ClassifyInputPath("/input/trigger/click"), PathClass::ControllerButton);
}

TEST(PathClassifier, TriggerTouchIsButton)
{
    EXPECT_EQ(ClassifyInputPath("/input/trigger/touch"), PathClass::ControllerButton);
}

// ---------------------------------------------------------------------------
// Pressure-sensitive analog axes -- classify as Trigger so the
// trigger-remap kernel applies (degraded peak / drifted floor correction).
// ---------------------------------------------------------------------------

TEST(PathClassifier, GripForceAnalog)
{
    EXPECT_EQ(ClassifyInputPath("/input/grip/force"), PathClass::Trigger);
}

TEST(PathClassifier, SqueezeValueAnalog)
{
    EXPECT_EQ(ClassifyInputPath("/input/squeeze/value"), PathClass::Trigger);
}

TEST(PathClassifier, TrackpadForceAnalog)
{
    EXPECT_EQ(ClassifyInputPath("/input/trackpad/force"), PathClass::Trigger);
}

TEST(PathClassifier, GenericPressureSuffix)
{
    EXPECT_EQ(ClassifyInputPath("/input/somecontrol/pressure"), PathClass::Trigger);
}

TEST(PathClassifier, GripClickStaysButton)
{
    // /click is a boolean even when the stem ("grip") also exposes an analog.
    EXPECT_EQ(ClassifyInputPath("/input/grip/click"), PathClass::ControllerButton);
}

// ---------------------------------------------------------------------------
// Stick axis paths
// ---------------------------------------------------------------------------

TEST(PathClassifier, JoystickXAxis)
{
    EXPECT_EQ(ClassifyInputPath("/input/joystick/x"), PathClass::StickAxis);
}

TEST(PathClassifier, JoystickYAxis)
{
    EXPECT_EQ(ClassifyInputPath("/input/joystick/y"), PathClass::StickAxis);
}

TEST(PathClassifier, ThumbstickXAxis)
{
    EXPECT_EQ(ClassifyInputPath("/input/thumbstick/x"), PathClass::StickAxis);
}

TEST(PathClassifier, TrackpadXAxis)
{
    EXPECT_EQ(ClassifyInputPath("/input/trackpad/x"), PathClass::StickAxis);
}

TEST(PathClassifier, TouchpadYAxis)
{
    EXPECT_EQ(ClassifyInputPath("/input/touchpad/y"), PathClass::StickAxis);
}

// ---------------------------------------------------------------------------
// Controller button paths
// ---------------------------------------------------------------------------

TEST(PathClassifier, GripValueAnalog)
{
    // Index Knuckles publishes /input/grip/value as a pressure-sensitive
    // analog 0..1 squeeze. Wand-style controllers only have /input/grip/click
    // (covered separately) so this classification is safe across both.
    EXPECT_EQ(ClassifyInputPath("/input/grip/value"), PathClass::Trigger);
}

TEST(PathClassifier, SystemClick)
{
    EXPECT_EQ(ClassifyInputPath("/input/system/click"), PathClass::ControllerButton);
}

TEST(PathClassifier, AButtonClick)
{
    EXPECT_EQ(ClassifyInputPath("/input/a/click"), PathClass::ControllerButton);
}

TEST(PathClassifier, BButtonClick)
{
    EXPECT_EQ(ClassifyInputPath("/input/b/click"), PathClass::ControllerButton);
}

// ---------------------------------------------------------------------------
// Diagnostics-only paths
// ---------------------------------------------------------------------------

TEST(PathClassifier, EyeOpenness)
{
    EXPECT_EQ(ClassifyInputPath("/input/eye/left/openness"), PathClass::DiagnosticsOnly);
}

TEST(PathClassifier, FaceExpression)
{
    EXPECT_EQ(ClassifyInputPath("/input/face/jaw_open"), PathClass::DiagnosticsOnly);
}

// ---------------------------------------------------------------------------
// Unsupported paths
// ---------------------------------------------------------------------------

TEST(PathClassifier, PupilDilation)
{
    EXPECT_EQ(ClassifyInputPath("/input/pupil/left/dilation"), PathClass::Unsupported);
}

TEST(PathClassifier, ProximitySensor)
{
    EXPECT_EQ(ClassifyInputPath("/input/proximity"), PathClass::Unsupported);
}

TEST(PathClassifier, EmptyPath)
{
    EXPECT_EQ(ClassifyInputPath(""), PathClass::Unsupported);
}

// ---------------------------------------------------------------------------
// IsCompensationPath / IsDiagnosticsOnlyPath helpers
// ---------------------------------------------------------------------------

TEST(PathClassifier, IsCompensationPath_Trigger)
{
    EXPECT_TRUE(IsCompensationPath(PathClass::Trigger));
}

TEST(PathClassifier, IsCompensationPath_StickAxis)
{
    EXPECT_TRUE(IsCompensationPath(PathClass::StickAxis));
}

TEST(PathClassifier, IsCompensationPath_Button)
{
    EXPECT_TRUE(IsCompensationPath(PathClass::ControllerButton));
}

TEST(PathClassifier, IsCompensationPath_DiagnosticsOnly)
{
    EXPECT_FALSE(IsCompensationPath(PathClass::DiagnosticsOnly));
}

TEST(PathClassifier, IsCompensationPath_Unsupported)
{
    EXPECT_FALSE(IsCompensationPath(PathClass::Unsupported));
}

TEST(PathClassifier, IsDiagnosticsOnly)
{
    EXPECT_TRUE(IsDiagnosticsOnlyPath(PathClass::DiagnosticsOnly));
    EXPECT_FALSE(IsDiagnosticsOnlyPath(PathClass::Trigger));
    EXPECT_FALSE(IsDiagnosticsOnlyPath(PathClass::Unsupported));
}
