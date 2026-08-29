#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

App::App() {
    std::string exe = util::exeDir();
    outputDir_ = exe.empty() ? "export" : util::joinPath(exe, "export");
    std::snprintf(outBuf_, sizeof(outBuf_), "%s", outputDir_.c_str());
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(outputDir_), ec);

    // Optional auto-load for testing / scripting (PMS2OSU_AUTOLOAD=<folder or .pms>)
#ifdef _WIN32
    wchar_t wbuf[4096];
    DWORD wlen = GetEnvironmentVariableW(L"PMS2OSU_AUTOLOAD", wbuf, 4096);
    if (wlen > 0 && wlen < 4096)
        addDroppedPath(util::wideToUtf8(std::wstring(wbuf, wlen)));
#else
    if (const char* autoLoad = std::getenv("PMS2OSU_AUTOLOAD")) {
        if (autoLoad[0]) addDroppedPath(autoLoad);
    }
#endif
}

App::~App() { Shutdown(); }

void App::setWindow(void* w) { win_ = w; }

const char* App::tr(const char* en, const char* zh) const {
    return lang_ ? zh : en;
}

void App::Shutdown() { conv_.shutdown(); }

void App::log(const std::string& s) {
    logLines_.push_back(s);
    if (logLines_.size() > 3000) logLines_.erase(logLines_.begin(), logLines_.begin() + 500);
}

void App::drainLog() {
    std::vector<std::string> tmp;
    conv_.drainLog(tmp);
    for (auto& l : tmp) log(l);
    running_ = conv_.running();
    // Auto-remove finished tasks from the list.
    conv_.removeCompleted();
}

std::string App::pickFolder(const char* title) {
    std::string result;
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);
    IFileOpenDialog* pfd = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD opts = 0;
        pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        pfd->SetTitle(util::utf8ToWide(title).c_str());
        hr = pfd->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = util::wideToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    if (needUninit) CoUninitialize();
#else
    (void)title;
#endif
    return result;
}

void App::DoAddFolder() {
    std::string dir = pickFolder("Add PMS folder");
    if (dir.empty()) return;
    addDroppedPath(dir);
}

void App::DoScanBigFolder() {
    std::string root = pickFolder("Import big folder (scan)");
    if (root.empty()) return;
    auto dirs = util::listDirsContainingPms(root);
    if (dirs.empty()) {
        log("[info] no sub-folder contains .pms under: " + root);
        return;
    }
    for (const auto& d : dirs) {
        if (conv_.addFolder(d)) log("[scan] + " + d);
    }
    log(std::string("[scan] found ") + std::to_string(dirs.size()) + " pms folder(s)");
}

void App::DoBrowseOutput() {
    std::string dir = pickFolder("Output directory");
    if (dir.empty()) return;
    outputDir_ = dir;
    std::snprintf(outBuf_, sizeof(outBuf_), "%s", dir.c_str());
}

void App::addDroppedPath(const std::string& path) {
    if (path.empty()) return;
    std::string p = util::ensureUtf8(path);   // robust against non-UTF-8 OS input
    if (util::dirExists(p)) {
        // If this folder directly contains .pms files, treat as one task.
        auto pms = util::listFiles(p, ".pms", false);
        if (!pms.empty()) {
            if (conv_.hasFolder(p)) {
                log("[skip] already added: " + p);
            } else if (conv_.addFolder(p)) {
                log("[drop] + " + p);
            } else {
                log("[drop] no .pms in " + p);
            }
        } else {
            // Otherwise scan sub-folders.
            auto dirs = util::listDirsContainingPms(p);
            if (dirs.empty()) {
                log("[drop] no pms folder found in " + p);
                return;
            }
            for (const auto& d : dirs) {
                if (conv_.hasFolder(d)) {
                    log("[skip] already added: " + d);
                } else if (conv_.addFolder(d)) {
                    log("[drop] + " + d);
                }
            }
        }
    } else {
        // A dropped file: use its containing folder.
        std::string low = util::toLower(p);
        if (low.size() > 4 && low.compare(low.size() - 4, 4, ".pms") == 0) {
            std::string d = util::dirName(p);
            if (conv_.hasFolder(d)) {
                log("[skip] already added: " + d);
            } else if (!d.empty() && conv_.addFolder(d)) {
                log("[drop] + " + d);
            }
        } else {
            log("[drop] ignored: " + p);
        }
    }
}

void App::DoStart() {
    if (running_) return;
    if (conv_.taskCount() == 0) { log("[error] no tasks"); return; }

    conv::Options o;
    o.normalize = normalize_;
    o.od = std::atof(odBuf_);
    o.hp = std::atof(hpBuf_);
    // osu!mania ignores ApproachRate; keep it in sync with OD for the .osu line.
    o.ar = o.od;
    if (o.od < 0 || o.od > 10) o.od = 7.5;
    if (o.hp < 0 || o.hp > 10) o.hp = 7.5;
    o.creator = creatorBuf_[0] ? std::string(creatorBuf_) : std::string("PMS");
    o.outputDir = outputDir_;
    o.openOutDir = true;
    conv_.setOptions(o);
    if (conv_.start()) log("[info] conversion started");
}

void App::DoStop() { conv_.requestStop(); }

// ---------------- rendering ----------------

void App::Render() {
    drainLog();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##Main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    RenderMenuBar();
    RenderOptions();
    RenderScanPanel();
    RenderTasks();
    RenderProgress();

    ImGui::End();
}

void App::RenderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu(tr("File", "文件"))) {
        if (ImGui::MenuItem(tr("Import big folder (scan)", "导入大文件夹（扫描）"))) DoScanBigFolder();
        if (ImGui::MenuItem(tr("Add pms folder", "添加 PMS 文件夹"))) DoAddFolder();
        if (ImGui::MenuItem(tr("Set output directory", "设置输出目录"))) DoBrowseOutput();
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Clear tasks", "清空任务"))) conv_.clearTasks();
        if (ImGui::MenuItem(tr("Quit", "退出"))) glfwSetWindowShouldClose((GLFWwindow*)win_, true);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Language", "语言"))) {
        if (ImGui::MenuItem("English", nullptr, lang_ == 0)) lang_ = 0;
        if (ImGui::MenuItem(tr("中文", "中文"), nullptr, lang_ == 1)) lang_ = 1;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::RenderScanPanel() {
    ImGui::SeparatorText(tr("Input", "输入"));
    ImGui::TextWrapped("%s", tr(
        "Drop your PMS (a folder contains multiple pms folders, single pms folder, or .pms file)",
        "拖入你的 PMS（一个包含多个 pms 文件夹的文件夹、单个 pms 文件夹，或 .pms 文件）"));
    const char* bBrowse = tr("Browse...", "浏览...");
    const ImGuiStyle& style = ImGui::GetStyle();
    float padX = style.FramePadding.x * 2.0f;
    float wBrowse = ImGui::CalcTextSize(bBrowse).x + padX;
    float spacing = style.ItemSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x;

    // Auto-layout: keep the (read-only) path box and the Browse button on one
    // line when the window is wide enough; stack them when it gets too narrow.
    // Clicking Browse always opens the folder picker (same dialog as the output
    // directory Browse), and the picked folder is added without duplicates.
    const float minInput = 120.0f;
    const ImGuiInputTextFlags ro = ImGuiInputTextFlags_ReadOnly;

    auto browseAndAdd = [&]() {
        std::string p = pickFolder(tr("Select PMS folder", "选择 PMS 文件夹"));
        if (p.empty()) return;   // dialog canceled
        std::snprintf(manualDirBuf_, sizeof(manualDirBuf_), "%s", p.c_str());
        addDroppedPath(p);
    };

    if (avail >= wBrowse + minInput + spacing) {
        float inputW = avail - wBrowse - spacing - 4.0f;
        if (inputW > 500) inputW = 500;   // keep the path box from getting too long
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText(tr("manual path", "路径"), manualDirBuf_, sizeof(manualDirBuf_), ro);
        ImGui::SameLine();
        ImGui::PushID("browseInput");
        if (ImGui::Button(bBrowse)) browseAndAdd();
        ImGui::PopID();
    } else {
        ImGui::SetNextItemWidth(avail);
        ImGui::InputText(tr("manual path", "路径"), manualDirBuf_, sizeof(manualDirBuf_), ro);
        ImGui::PushID("browseInput");
        if (ImGui::Button(bBrowse)) browseAndAdd();
        ImGui::PopID();
    }
}

void App::RenderTasks() {
    ImGui::SeparatorText(tr("Tasks", "任务"));

    // Reserve vertical space at the bottom for the progress box; the task
    // table (the main body) takes all the remaining height.
    const float kProgressH = 100.0f;
    if (!ImGui::BeginChild("##taskbody", ImVec2(0, -kProgressH), false)) {
        ImGui::EndChild();
        return;
    }

    auto tasks = conv_.snapshot();
    std::vector<size_t> toDelete;
    if (tasks.empty()) {
        ImGui::TextDisabled("%s", tr("(no tasks)", "（无任务）"));
    } else if (ImGui::BeginTable("tasks", 5,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_ScrollY,
                                 ImVec2(0, 0))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn(tr("Folder", "文件夹"), ImGuiTableColumnFlags_WidthStretch, 0);
        ImGui::TableSetupColumn(tr("Status", "状态"), ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn(tr("Result", "结果"), ImGuiTableColumnFlags_WidthStretch, 0);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 48);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < tasks.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool en = tasks[i].enabled;
            if (ImGui::Checkbox(("##en" + std::to_string(i)).c_str(), &en))
                conv_.setTaskEnabled(i, en);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tasks[i].srcDir.c_str());

            ImGui::TableNextColumn();
            const char* st = tr("ready", "就绪");
            switch (tasks[i].status) {
                case conv::S_Running: st = tr("running", "转换中"); break;
                case conv::S_Done: st = tr("done", "完成"); break;
                case conv::S_Failed: st = tr("failed", "失败"); break;
                case conv::S_Canceled: st = tr("canceled", "已取消"); break;
                default: break;
            }
            ImGui::TextUnformatted(st);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tasks[i].msg.c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton(tr("Del", "删"))) toDelete.push_back(i);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Apply deletions after the loop so snapshot indices stay valid.
    for (auto it = toDelete.rbegin(); it != toDelete.rend(); ++it)
        conv_.removeTask(*it);
}

void App::RenderOptions() {
    ImGui::SeparatorText(tr("Options (one-click apply)", "选项（一键应用）"));

    const ImGuiStyle& st = ImGui::GetStyle();
    const float sp = st.ItemSpacing.x;

    // Row 1: Creator / OD / HP / Normalize. Items wrap onto new lines when the
    // window is too narrow so nothing ever overflows the right edge.
    ImGui::TextUnformatted(tr("Creator", "制作人"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##creator", creatorBuf_, sizeof(creatorBuf_));

    const float wOd = ImGui::CalcTextSize("OD").x + sp + 70;
    if (ImGui::GetContentRegionAvail().x < wOd) ImGui::NewLine(); else ImGui::SameLine();
    ImGui::TextUnformatted("OD");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##od", odBuf_, sizeof(odBuf_));

    const float wHp = ImGui::CalcTextSize("HP").x + sp + 70;
    if (ImGui::GetContentRegionAvail().x < wHp) ImGui::NewLine(); else ImGui::SameLine();
    ImGui::TextUnformatted("HP");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##hp", hpBuf_, sizeof(hpBuf_));

    const float wNorm = ImGui::CalcTextSize(tr("Normalize audio", "音频归一化")).x +
                        sp + ImGui::GetFrameHeight();
    if (ImGui::GetContentRegionAvail().x < wNorm) ImGui::NewLine(); else ImGui::SameLine();
    ImGui::Checkbox(tr("Normalize audio", "音频归一化"), &normalize_);

    // Row 2: output directory (label before the box, read-only — set via Browse)
    ImGui::TextUnformatted(tr("Output dir", "输出目录"));
    const float wOut = ImGui::CalcTextSize(tr("Output dir", "输出目录")).x + sp + 100;
    if (ImGui::GetContentRegionAvail().x < wOut) ImGui::NewLine(); else ImGui::SameLine();
    float outW = ImGui::GetContentRegionAvail().x;
    if (outW < 100) outW = 100;
    if (outW > 500) outW = 500;
    ImGui::SetNextItemWidth(outW);
    ImGui::InputText("##outdir", outBuf_, sizeof(outBuf_), ImGuiInputTextFlags_ReadOnly);

    // Row 3: Browse + export (export button fills the remaining width)
    ImGui::PushID("browseOutput");
    if (ImGui::Button(tr("Browse...", "浏览..."))) DoBrowseOutput();
    ImGui::PopID();
    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail < 60) {   // too narrow: move Export onto its own line
        ImGui::NewLine();
        avail = ImGui::GetContentRegionAvail().x;
    }
    if (ImGui::Button(running_ ? tr("Stop", "停止") : tr("Export .osz", "导出 .osz"),
                      ImVec2(avail, 0)))
        running_ ? DoStop() : DoStart();
}

void App::RenderProgress() {
    ImGui::Separator();

    // Total batch progress: completed / enabled tasks.
    int p = conv_.progress();
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%", p);
    ImGui::ProgressBar(p / 100.0f, ImVec2(-1, 0), pct);

    std::string cur = conv_.currentTask();
    if (cur.empty()) cur = tr("(idle)", "（空闲）");
    ImGui::TextDisabled("%s", (std::string(tr("Current:", "当前：")) + " " + cur).c_str());

    // Show the latest log line as a one-line status instead of a full log panel.
    if (!logLines_.empty())
        ImGui::TextDisabled("%s", logLines_.back().c_str());
}
