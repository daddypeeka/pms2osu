// main.cpp - pms2osu-v2 GUI entry point.
#include <cstdio>
#include <string>
#include <vector>

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

    GLFWwindow* window = glfwCreateWindow(1000, 700, "pms2osu-v2", nullptr, nullptr);
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
    ImGui::StyleColorsDark();
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