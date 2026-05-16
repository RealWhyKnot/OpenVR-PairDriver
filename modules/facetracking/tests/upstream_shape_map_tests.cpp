// Verify the hand-coded upstream -> ours expression remap table.
//
// The table lives in core/src/common/facetracking/UpstreamShapeMap.h and
// is maintained in lockstep with two C# enums:
//   - VRCFaceTracking.Core.Params.Expressions.UnifiedExpressions (upstream)
//   - OpenVRPair.FaceTracking.ModuleSdk.UnifiedExpression        (ours)
//
// If either enum gains entries upstream-side or ours-side, the table needs
// a matching pass. These tests anchor a handful of well-known shapes (the
// names a code reviewer would recognise from VRCFaceTracking docs) so any
// drift surfaces immediately rather than silently producing zero values.

#include <gtest/gtest.h>

#include "facetracking/UpstreamShapeMap.h"

#include <algorithm>

using namespace facetracking;
namespace p = protocol;

TEST(UpstreamShapeMap, TableSizeIs88)
{
    EXPECT_EQ(kUpstreamShapeCount, 88);
    // Compile-time -- if the array shape ever changed this would not link.
    EXPECT_EQ(static_cast<int>(sizeof(kUpstreamToOurs) / sizeof(kUpstreamToOurs[0])),
              kUpstreamShapeCount);
}

TEST(UpstreamShapeMap, JawOpenMapsThroughCorrectly)
{
    // Upstream UnifiedExpressions.JawOpen lives at index 22.
    // Our UnifiedExpression.JawOpen lives at index 26.
    EXPECT_EQ(kUpstreamToOurs[22], 26);

    float src[kUpstreamShapeCount] = {};
    src[22] = 0.75f;

    float dst[p::FACETRACKING_EXPRESSION_COUNT] = {};
    RemapUpstreamShapes(src, dst);

    EXPECT_NEAR(dst[26], 0.75f, 1e-6f);
    // Everything else stays zero.
    for (int i = 0; i < static_cast<int>(p::FACETRACKING_EXPRESSION_COUNT); ++i) {
        if (i == 26) continue;
        EXPECT_FLOAT_EQ(dst[i], 0.0f) << "slot " << i << " unexpectedly nonzero";
    }
}

TEST(UpstreamShapeMap, EyeSquintWideMapsThroughCorrectly)
{
    // Hand-verified pairs from UpstreamShapeMap.h:
    //   upstream[0] EyeSquintRight -> ours[11] EyeSquintRight
    //   upstream[1] EyeSquintLeft  -> ours[10] EyeSquintLeft
    //   upstream[2] EyeWideRight   -> ours[ 9] EyeWideRight
    //   upstream[3] EyeWideLeft    -> ours[ 8] EyeWideLeft
    EXPECT_EQ(kUpstreamToOurs[0], 11);
    EXPECT_EQ(kUpstreamToOurs[1], 10);
    EXPECT_EQ(kUpstreamToOurs[2],  9);
    EXPECT_EQ(kUpstreamToOurs[3],  8);

    float src[kUpstreamShapeCount] = {};
    src[0] = 0.4f; src[1] = 0.5f; src[2] = 0.6f; src[3] = 0.7f;

    float dst[p::FACETRACKING_EXPRESSION_COUNT] = {};
    RemapUpstreamShapes(src, dst);

    EXPECT_NEAR(dst[11], 0.4f, 1e-6f);
    EXPECT_NEAR(dst[10], 0.5f, 1e-6f);
    EXPECT_NEAR(dst[ 9], 0.6f, 1e-6f);
    EXPECT_NEAR(dst[ 8], 0.7f, 1e-6f);
}

TEST(UpstreamShapeMap, NasalAndJawBackwardAreDropped)
{
    // Upstream slots without an ours equivalent must remain dropped
    // (table entry -1). Picking the ones the audit memo called out
    // explicitly: nasal dilation/constriction, jaw backward, mouth corner.
    EXPECT_EQ(kUpstreamToOurs[12], -1); // NasalDilationRight
    EXPECT_EQ(kUpstreamToOurs[13], -1); // NasalDilationLeft
    EXPECT_EQ(kUpstreamToOurs[14], -1); // NasalConstrictRight
    EXPECT_EQ(kUpstreamToOurs[15], -1); // NasalConstrictLeft
    EXPECT_EQ(kUpstreamToOurs[26], -1); // JawBackward
    EXPECT_EQ(kUpstreamToOurs[27], -1); // JawClench
    EXPECT_EQ(kUpstreamToOurs[56], -1); // MouthCornerPullRight
    EXPECT_EQ(kUpstreamToOurs[60], -1); // MouthFrownRight

    float src[kUpstreamShapeCount] = {};
    src[12] = 0.9f; src[26] = 0.8f; src[56] = 0.7f;

    float dst[p::FACETRACKING_EXPRESSION_COUNT] = {};
    for (int i = 0; i < static_cast<int>(p::FACETRACKING_EXPRESSION_COUNT); ++i)
        dst[i] = 0.0f;
    RemapUpstreamShapes(src, dst);

    // Every slot stays zero -- nothing landed.
    for (int i = 0; i < static_cast<int>(p::FACETRACKING_EXPRESSION_COUNT); ++i) {
        EXPECT_FLOAT_EQ(dst[i], 0.0f) << "slot " << i << " unexpectedly nonzero";
    }
}

TEST(UpstreamShapeMap, NaNAndInfAreDropped)
{
    float src[kUpstreamShapeCount] = {};
    src[22] = std::nanf("");
    src[ 0] = std::numeric_limits<float>::infinity();
    src[ 1] = -std::numeric_limits<float>::infinity();
    src[ 2] = 0.5f; // OK value -- this one should land

    float dst[p::FACETRACKING_EXPRESSION_COUNT] = {};
    RemapUpstreamShapes(src, dst);

    EXPECT_FLOAT_EQ(dst[26], 0.0f); // JawOpen slot stays zero from NaN
    EXPECT_FLOAT_EQ(dst[11], 0.0f); // EyeSquintRight stays zero from +Inf
    EXPECT_FLOAT_EQ(dst[10], 0.0f); // EyeSquintLeft  stays zero from -Inf
    EXPECT_NEAR(dst[9], 0.5f, 1e-6f); // EyeWideRight got the OK 0.5
}

TEST(UpstreamShapeMap, ValuesAreClampedTo01)
{
    float src[kUpstreamShapeCount] = {};
    src[22] = 2.5f;  // JawOpen overshoot
    src[ 0] = -0.5f; // EyeSquintRight undershoot

    float dst[p::FACETRACKING_EXPRESSION_COUNT] = {};
    RemapUpstreamShapes(src, dst);

    EXPECT_FLOAT_EQ(dst[26], 1.0f);
    EXPECT_FLOAT_EQ(dst[11], 0.0f);
}

TEST(UpstreamShapeMap, MappedCountMatchesExpectation)
{
    // Spot-check the total count of mapped vs dropped entries so a future
    // table edit that accidentally drops a previously-mapped slot is loud.
    int mapped = 0;
    int dropped = 0;
    for (int i = 0; i < kUpstreamShapeCount; ++i) {
        if (kUpstreamToOurs[i] < 0) ++dropped;
        else ++mapped;
    }
    // 48 mapped covers the eye-squint/wide (4), brow (8), cheek puff/suck (4),
    // jaw forward/left/right/open (4), lip suck upper/lower (4), lip funnel (4),
    // lip pucker upper (2), mouth upper/lower direction (4), stretch (2),
    // dimple (2), raiser (2), press (2), tightener (2), tongue out/up/down/left
    // (4). 40 dropped are slots upstream has but ours does not (nasal, cheek
    // squint, jaw backward/clench/mandible, mouth closed, lip suck corner,
    // lip pucker lower, mouth upper-up / lower-down / deepen, mouth corner
    // pull / slant, mouth frown, tongue right / roll / bend / curl / squish /
    // flat / twist, soft palate, throat, neck flex).
    EXPECT_EQ(mapped,  48);
    EXPECT_EQ(dropped, 40);
}
