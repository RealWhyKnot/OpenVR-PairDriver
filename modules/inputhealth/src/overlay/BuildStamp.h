#pragma once

// Per-build version stamp. Defined as a #define rather than a constexpr so
// the project can use it from a few points (window title, IPC handshake
// log) without an include-order headache.
//
// build.ps1 regenerates this file on every build; the placeholder here is
// what an in-tree edit (no build) ends up with.

#ifndef OPENVR_INPUTHEALTH_VERSION
#define OPENVR_INPUTHEALTH_VERSION "0.1.0.0-dev"
#endif
