#include "DeviceFilters.h"

#include <gtest/gtest.h>

TEST(DeviceFiltersTest, HidesFaceTrackingSink)
{
	EXPECT_FALSE(openvr_pair::overlay::ShouldShowInCalibrationDeviceList(
		vr::TrackedDeviceClass_GenericTracker,
		"OpenVRPair-FaceTracking-Sink",
		"OpenVRPair FaceTracking"));
	EXPECT_FALSE(openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
		vr::TrackedDeviceClass_GenericTracker,
		"OpenVRPair-FaceTracking-Sink",
		"generic_tracker"));
	EXPECT_FALSE(openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
		vr::TrackedDeviceClass_GenericTracker,
		"WKOpenVR-FaceTracking-Sink",
		"WKOpenVR FaceTracking"));
}

TEST(DeviceFiltersTest, HidesNonUserPoseClasses)
{
	EXPECT_FALSE(openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
		vr::TrackedDeviceClass_TrackingReference,
		"LHB-12345678",
		"lh_basestation_valve_gen2"));
	EXPECT_FALSE(openvr_pair::overlay::ShouldShowInCalibrationDeviceList(
		vr::TrackedDeviceClass_DisplayRedirect,
		"redirect",
		"redirect"));
}

TEST(DeviceFiltersTest, KeepsUserPoseDevices)
{
	EXPECT_TRUE(openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
		vr::TrackedDeviceClass_HMD,
		"hmd-serial",
		"hmd"));
	EXPECT_TRUE(openvr_pair::overlay::ShouldShowInSmoothingPredictionList(
		vr::TrackedDeviceClass_Controller,
		"LHR-controller",
		"valve_controller"));
	EXPECT_TRUE(openvr_pair::overlay::ShouldShowInCalibrationDeviceList(
		vr::TrackedDeviceClass_GenericTracker,
		"LHR-tracker",
		"vive_tracker"));
}
