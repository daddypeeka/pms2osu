#pragma once
// app.h - ImGui application front-end.
#include <string>
#include <vector>

#include "convert.h"

struct App {
    App();
    ~App();

    void setWindow(void* win);
    void addDroppedPath(const std::string& path);   // folder / file from drag&drop
    void Render();
    void Shutdown();

private:
    void log(const std::string& s);
    void drainLog();
    void DoAddFolder();
    void DoScanBigFolder();
    void DoBrowseOutput();
    void DoStart();
    void DoStop();
    void RenderMenuBar();
    void RenderScanPanel();
    void RenderTasks();
    void RenderOptions();
    void RenderProgress();
    std::string pickFolder(const char* title);
    const char* tr(const char* en, const char* zh) const;   // 0 = English, 1 = 中文

    conv::Converter conv_;
    bool running_ = false;
    std::vector<std::string> logLines_;

    // option buffers
    char creatorBuf_[128] = "PMS";
    char odBuf_[16] = "7.5";
    char hpBuf_[16] = "7.5";
    char outBuf_[1024] = "";
    char manualDirBuf_[2048] = "";
    bool normalize_ = true;

    int lang_ = 0;   // 0 = English, 1 = 中文

    std::string outputDir_;
    void* win_ = nullptr;
};