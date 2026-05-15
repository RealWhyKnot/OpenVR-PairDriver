#include "DiscordPresence.h"

// pair_discord_rpc links statically; do not define DISCORD_DYNAMIC_LIB so the
// header decls fall through to plain external linkage rather than dllimport.
#include <discord_rpc.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace WKOpenVR {

namespace {

constexpr const char* kAppId = "1504757904253779988";
constexpr const char* kLargeImageKey  = "logo";
constexpr const char* kLargeImageText = "WKOpenVR";

// Minimum wall-clock interval between Discord_UpdatePresence calls.
// The SDK rate-limits on its own, but we avoid hammering it on every frame.
constexpr double kUpdateIntervalSec = 2.0;
constexpr double kTickIntervalSec   = 2.0;

bool g_initialized = false;

int64_t     g_startTimestamp = 0;
std::string g_state;
std::string g_details;

using Clock = std::chrono::steady_clock;
Clock::time_point g_lastTick;
Clock::time_point g_lastUpdate;

void PushPresence()
{
    if (!g_initialized) return;

    DiscordRichPresence rp{};
    rp.state          = g_state.empty()   ? nullptr : g_state.c_str();
    rp.details        = g_details.empty() ? nullptr : g_details.c_str();
    rp.startTimestamp = g_startTimestamp;
    rp.largeImageKey  = kLargeImageKey;
    rp.largeImageText = kLargeImageText;

    Discord_UpdatePresence(&rp);
    g_lastUpdate = Clock::now();
}

void OnReady(const DiscordUser* user)
{
    if (user && user->username && user->username[0] != '\0') {
        fprintf(stderr, "[discord-rpc] connected as %s\n", user->username);
    } else {
        fprintf(stderr, "[discord-rpc] connected\n");
    }
}

void OnDisconnected(int code, const char* message)
{
    fprintf(stderr, "[discord-rpc] disconnected: %d %s\n", code, message ? message : "");
}

void OnErrored(int code, const char* message)
{
    fprintf(stderr, "[discord-rpc] error: %d %s\n", code, message ? message : "");
}

} // namespace

void DiscordPresence_Init()
{
    DiscordEventHandlers handlers{};
    handlers.ready        = OnReady;
    handlers.disconnected = OnDisconnected;
    handlers.errored      = OnErrored;

    Discord_Initialize(kAppId, &handlers, /*autoRegister=*/0, /*steamId=*/nullptr);

    // Discord_Initialize does not surface a synchronous error return; the
    // errored/disconnected callbacks fire asynchronously if the client is
    // unavailable. We mark ourselves initialized and let the first callback
    // (or its absence) indicate the actual connection state.
    g_initialized = true;

    const auto now = std::chrono::system_clock::now();
    g_startTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    g_state   = "Idle";
    g_details = "WKOpenVR";

    g_lastTick   = Clock::now();
    g_lastUpdate = Clock::now();

    PushPresence();
}

void DiscordPresence_Shutdown()
{
    if (!g_initialized) return;
    Discord_ClearPresence();
    Discord_Shutdown();
    g_initialized = false;
}

void DiscordPresence_Tick()
{
    if (!g_initialized) return;

    const auto now = Clock::now();
    const double secSinceTick = std::chrono::duration<double>(now - g_lastTick).count();
    if (secSinceTick < kTickIntervalSec) return;

    g_lastTick = now;
    Discord_RunCallbacks();
}

void DiscordPresence_SetState(const char* state, const char* details)
{
    if (!g_initialized) return;

    const std::string newState   = state   ? state   : "";
    const std::string newDetails = details ? details : "";

    if (newState == g_state && newDetails == g_details) return;

    g_state   = newState;
    g_details = newDetails;

    const auto now = Clock::now();
    const double secSinceUpdate = std::chrono::duration<double>(now - g_lastUpdate).count();
    if (secSinceUpdate >= kUpdateIntervalSec) {
        PushPresence();
    }
}


} // namespace WKOpenVR
