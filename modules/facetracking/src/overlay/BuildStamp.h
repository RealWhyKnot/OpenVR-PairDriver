#pragma once

// Per-build version stamp. Defined as a #define rather than a constexpr so
// the project can use it from a few call sites (window title, IPC log)
// without include-order constraints.
//
// build.ps1 regenerates this file at release-build time; the placeholder
// here is what you get from an in-tree edit without a build.

#ifndef FACETRACKING_BUILD_STAMP
#define FACETRACKING_BUILD_STAMP "0.0.0.0-dev"
#endif

#ifndef FACETRACKING_BUILD_CHANNEL
#define FACETRACKING_BUILD_CHANNEL "dev"
#endif
