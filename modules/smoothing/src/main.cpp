// WKOpenVR-Smoothing -- minimal config-UI overlay for the finger-smoothing
// feature implemented inside WKOpenVR. Connects to the driver via
// the smoothing named pipe and pushes RequestSetFingerSmoothing on every UI
// change. Persists the live state to %LocalAppDataLow%\WKOpenVR-Smoothing\
// config.txt so the next launch (or the next driver reconnect) restores it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Config.h"
#include "IPCClient.h"
#include "Protocol.h"

#include <cstdio>
#include <exception>
#include <string>

#ifndef SMOOTHING_VERSION_STRING
#define SMOOTHING_VERSION_STRING "0.0.0.0-dev"
#endif

namespace
{
    void SendConfig(IPCClient &client, const SmoothingConfig &cfg)
    {
        if (!client.IsConnected()) return;
        protocol::Request req(protocol::RequestSetFingerSmoothing);
        req.setFingerSmoothing.master_enabled = 1;
        int s = cfg.smoothness;
        if (s < 0) s = 0;
        if (s > 100) s = 100;
        req.setFingerSmoothing.smoothness  = (uint8_t)s;
        req.setFingerSmoothing.finger_mask = cfg.finger_mask;
        req.setFingerSmoothing._reserved   = 0;
        try {
            client.SendBlocking(req);
        } catch (const std::exception &) {
            // Connection may have just dropped. Next UI change will retry.
        }
    }

    void GlfwErrorCallback(int code, const char *desc)
    {
        fprintf(stderr, "[glfw] error %d: %s\n", code, desc ? desc : "(null)");
    }
}

int main(int /*argc*/, char ** /*argv*/)
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        MessageBoxA(nullptr, "Failed to initialise GLFW.", "WKOpenVR-Smoothing", MB_ICONERROR);
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(640, 480, "WKOpenVR-Smoothing", nullptr, nullptr);
    if (!window) {
        MessageBoxA(nullptr, "Failed to create window.", "WKOpenVR-Smoothing", MB_ICONERROR);
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (gl3wInit() != 0) {
        MessageBoxA(nullptr, "Failed to load OpenGL functions.", "WKOpenVR-Smoothing", MB_ICONERROR);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    SmoothingConfig cfg = LoadConfig();

    IPCClient client;
    std::string connectError;
    double lastConnectionAttempt = glfwGetTime();
    try {
        client.Connect();
        SendConfig(client, cfg);
    } catch (const std::exception &e) {
        connectError = e.what();
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!client.IsConnected()) {
            const double now = glfwGetTime();
            if (now - lastConnectionAttempt >= 1.0) {
                lastConnectionAttempt = now;
                try {
                    client.Connect();
                    SendConfig(client, cfg);
                    connectError.clear();
                } catch (const std::exception &e) {
                    connectError = e.what();
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Single full-window panel.
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##root", nullptr, flags);

        ImGui::Text("Finger smoothing");
        ImGui::TextDisabled("(Index Knuckles only -- smooths per-frame finger bone updates before VRChat sees them.)");
        ImGui::Spacing();

        bool dirty = false;

        int strength = cfg.smoothness;
        if (ImGui::SliderInt("Strength##fingers", &strength, 0, 100, "%d%%")) {
            if (strength < 0) strength = 0;
            if (strength > 100) strength = 100;
            cfg.smoothness = strength;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "0   = no smoothing (each frame snaps to incoming bones).\n"
                "50  = moderate (good starting point).\n"
                "100 = heavy lag (slerp factor 0.05 per frame). Never fully freezes.\n"
                "Drag above 0%% to enable per-finger overrides below.");
        }

        ImGui::BeginDisabled(cfg.smoothness == 0);

        ImGui::Spacing();
        ImGui::Text("Per-finger toggles (uncheck to bypass that finger only)");
        ImGui::TextDisabled("Useful for isolating a finger whose smoothing produces an artifact.");

        const char *fingerLabels[5] = { "Thumb", "Index", "Middle", "Ring", "Pinky" };
        const char *handLabels[2]   = { "Left", "Right" };

        if (ImGui::BeginTable("fingers_grid", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Hand");
            for (int f = 0; f < 5; ++f) ImGui::TableSetupColumn(fingerLabels[f]);
            ImGui::TableHeadersRow();

            for (int hand = 0; hand < 2; ++hand) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", handLabels[hand]);
                for (int finger = 0; finger < 5; ++finger) {
                    int bit = hand * 5 + finger;
                    ImGui::TableNextColumn();
                    bool enabled = ((cfg.finger_mask >> bit) & 1) != 0;
                    ImGui::PushID(bit);
                    if (ImGui::Checkbox("##fingerbit", &enabled)) {
                        if (enabled) cfg.finger_mask |= (uint16_t)(1u << bit);
                        else         cfg.finger_mask &= (uint16_t)~(1u << bit);
                        dirty = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Enable all fingers")) {
            if (cfg.finger_mask != protocol::kAllFingersMask) {
                cfg.finger_mask = protocol::kAllFingersMask;
                dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable all fingers")) {
            if (cfg.finger_mask != 0) {
                cfg.finger_mask = 0;
                dirty = true;
            }
        }

        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();

        if (client.IsConnected()) {
            ImGui::TextColored(ImVec4(0.20f, 0.80f, 0.30f, 1.0f),
                "Driver connected -- changes apply live as you drag the slider.");
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.25f, 1.0f),
                "Driver not connected. Settings will save locally and apply once SteamVR is running.");
            if (!connectError.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
                    "Smoothing driver connection failed");
                ImGui::TextWrapped("%s", connectError.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("WKOpenVR-Smoothing %s", SMOOTHING_VERSION_STRING);

        if (dirty) {
            SaveConfig(cfg);
            SendConfig(client, cfg);
        }

        ImGui::End();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.11f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
