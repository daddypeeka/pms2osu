// main.cpp - pms2osu-v2 GUI entry point.
#include <cstdio>
#include <string>
#include <vector>

// Single source of truth for the app version (kept in sync with the git tag).
static const char* const kVersion = "0.3.1";

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"
#include "app.h"
#include "util.h"

static App g_app;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

static std::vector<std::string> getArgs(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return {};
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) out.push_back(util::wideToUtf8(w[i]));
    LocalFree(w);
    return out;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

static void glfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static void dropCallback(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count; ++i) {
        std::string p = paths[i];
#ifdef _WIN32
        // GLFW gives UTF-8 paths on all platforms; no conversion needed.
#endif
        g_app.addDroppedPath(p);
    }
}

// Load a CJK-capable font so Chinese (language switch) and Japanese
// (folder names) render correctly instead of showing tofu / garbled boxes.
// ImGui's built-in font only has Latin glyphs. Tries a list of common
// system CJK fonts and falls back to the default font if none are found.
static void loadCjkFont() {
    ImGuiIO& io = ImGui::GetIO();
    static const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",                      // Microsoft YaHei (CN)
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/msjh.ttc",                      // Microsoft JhengHei (TW)
        "C:/Windows/Fonts/msgothic.ttc",                  // MS Gothic (JP)
        "C:/Windows/Fonts/meiryo.ttc",                    // Meiryo (JP)
        "C:/Windows/Fonts/simhei.ttf",                    // SimHei
        "C:/Windows/Fonts/simsun.ttc",                    // SimSun
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",  // Linux
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/System/Library/Fonts/PingFang.ttc",             // macOS
    };
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    for (const char* path : candidates) {
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, ranges)) {
            io.FontDefault = f;
            return;
        }
    }
}

// A modern dark "flat" theme for Dear ImGui. The default ImGui dark style is
// functional but dated; this keeps the same backend while giving the app a
// cleaner, more polished look (rounded controls, softer contrast, blue accent).
static void setupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 8.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 5.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.TabRounding = 4.0f;

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;

    s.WindowPadding = ImVec2(10, 10);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 8.0f;

    ImVec4* c = s.Colors;

    // Base surfaces
    c[ImGuiCol_WindowBg]      = ImVec4(0.082f, 0.090f, 0.110f, 1.00f);
    c[ImGuiCol_ChildBg]       = ImVec4(0.114f, 0.125f, 0.153f, 1.00f);
    c[ImGuiCol_PopupBg]       = ImVec4(0.105f, 0.115f, 0.141f, 1.00f);
    c[ImGuiCol_Border]        = ImVec4(0.165f, 0.180f, 0.220f, 1.00f);
    c[ImGuiCol_BorderShadow]  = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // Titles / menu bar
    c[ImGuiCol_TitleBg]          = ImVec4(0.100f, 0.110f, 0.135f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.122f, 0.134f, 0.165f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.100f, 0.110f, 0.135f, 0.50f);
    c[ImGuiCol_MenuBarBg]        = ImVec4(0.100f, 0.110f, 0.135f, 1.00f);

    // Text
    c[ImGuiCol_Text]         = ImVec4(0.910f, 0.925f, 0.950f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.420f, 0.455f, 0.505f, 1.00f);

    // Inputs / frames
    c[ImGuiCol_FrameBg]         = ImVec4(0.145f, 0.157f, 0.190f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.185f, 0.200f, 0.240f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.225f, 0.240f, 0.285f, 1.00f);

    // Buttons
    c[ImGuiCol_Button]         = ImVec4(0.180f, 0.195f, 0.235f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.240f, 0.256f, 0.306f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.295f, 0.310f, 0.370f, 1.00f);

    // Headers / selectable rows
    c[ImGuiCol_Header]         = ImVec4(0.190f, 0.205f, 0.250f, 1.00f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.245f, 0.262f, 0.315f, 1.00f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.295f, 0.310f, 0.370f, 1.00f);

    // Tables
    c[ImGuiCol_TableHeaderBg]    = ImVec4(0.130f, 0.142f, 0.172f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.165f, 0.180f, 0.220f, 1.00f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.150f, 0.163f, 0.199f, 1.00f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(0.055f, 0.060f, 0.075f, 1.00f);

    // Separators
    c[ImGuiCol_Separator]        = ImVec4(0.165f, 0.180f, 0.220f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.300f, 0.320f, 0.385f, 1.00f);
    c[ImGuiCol_SeparatorActive]  = ImVec4(0.350f, 0.370f, 0.445f, 1.00f);

    // Accent / selection
    c[ImGuiCol_CheckMark]        = ImVec4(0.310f, 0.550f, 0.960f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.310f, 0.550f, 0.960f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.390f, 0.610f, 1.000f, 1.00f);
    c[ImGuiCol_TextSelectedBg]   = ImVec4(0.250f, 0.430f, 0.750f, 0.50f);

    // Scrollbars
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.075f, 0.082f, 0.100f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.240f, 0.258f, 0.310f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.290f, 0.310f, 0.370f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.350f, 0.370f, 0.440f, 1.00f);

    // Tabs
    c[ImGuiCol_Tab]                = ImVec4(0.145f, 0.157f, 0.190f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.250f, 0.270f, 0.330f, 1.00f);
    c[ImGuiCol_TabSelected]        = ImVec4(0.260f, 0.330f, 0.500f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.310f, 0.550f, 0.960f, 1.00f);
}

int main(int argc, char** argv) {
    std::vector<std::string> args = getArgs(argc, argv);

    // Simple CLI:  pms2osu-v2 --convert <folder> [outputDir]
    if (args.size() >= 3 && args[1] == "--convert") {
        conv::Options o;
        o.outputDir = (args.size() >= 4) ? args[3] : std::string();
        o.openOutDir = false;
        conv::Converter c;
        c.setOptions(o);
        std::string folder = args[2];
        if (util::dirExists(folder) && !util::listFiles(folder, ".pms", false).empty()) {
            c.addFolder(folder);
        } else {
            auto dirs = util::listDirsContainingPms(folder);
            for (const auto& d : dirs) c.addFolder(d);
        }
        if (c.taskCount() == 0) {
            std::fprintf(stderr, "no pms folders found\n");
            return 1;
        }
        c.start();
        c.wait();
        std::vector<std::string> logs;
        c.drainLog(logs);
        for (const auto& l : logs) std::printf("  %s\n", l.c_str());
        auto tasks = c.snapshot();
        int ok = 0, fail = 0;
        for (const auto& t : tasks) {
            std::printf("%s  %s  %s  %s\n", t.status == conv::S_Done ? "OK " : "FAIL",
                        t.srcDir.c_str(), t.oszPath.c_str(), t.msg.c_str());
            if (t.status == conv::S_Done) ++ok; else ++fail;
        }
        std::printf("done: %d ok, %d failed\n", ok, fail);
        return fail ? 1 : 0;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }
    glfwSetErrorCallback(glfwError);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    const std::string windowTitle = std::string("pms2osu-v2 v") + kVersion;
    GLFWwindow* window = glfwCreateWindow(1000, 700, windowTitle.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, dropCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    setupStyle();
    loadCjkFont();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_app.setWindow(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        g_app.Render();

        ImGui::Render();
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
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