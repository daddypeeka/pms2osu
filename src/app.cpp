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
            if (conv_.addFolder(p)) log("[drop] + " + p);
            else log("[drop] no .pms in " + p);
        } else {
            // Otherwise scan sub-folders.
            auto dirs = util::listDirsContainingPms(p);
            if (dirs.empty()) {
                log("[drop] no pms folder found in " + p);
                return;
            }
            for (const auto& d : dirs)
                if (conv_.addFolder(d)) log("[drop] + " + d);
        }
    } else {
        // A dropped file: use its containing folder.
        std::string low = util::toLower(p);
        if (low.size() > 4 && low.compare(low.size() - 4, 4, ".pms") == 0) {
            std::string d = util::dirName(p);
            if (!d.empty() && conv_.addFolder(d)) log("[drop] + " + d);
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
    RenderLog();

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
    ImGui::TextWrapped(tr(
        "Drop your PMS (a folder contains multiple pms folders, single pms folder, or .pms file)",
        "拖入你的 PMS（一个包含多个 pms 文件夹的文件夹、单个 pms 文件夹，或 .pms 文件）"));
    // Keep the single Import button on the same line as the path box.
    // Measure its actual width (CJK font aware) and let the path box take the
    // remaining space so nothing overflows.
    const char* bImport = tr("Import path", "导入路径");
    const ImGuiStyle& style = ImGui::GetStyle();
    float padX = style.FramePadding.x * 2.0f;
    float wImport = ImGui::CalcTextSize(bImport).x + padX;
    float spacing = style.ItemSpacing.x;

    float avail = ImGui::GetContentRegionAvail().x;
    float inputW = avail - wImport - spacing - 4.0f;
    if (inputW > 500) inputW = 500;   // keep the path box from getting too long
    if (inputW < 0) inputW = 0;       // extremely narrow window: button takes priority
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText(tr("manual path", "路径"), manualDirBuf_, sizeof(manualDirBuf_));
    ImGui::SameLine();
    if (ImGui::Button(bImport)) {
        std::string p = manualDirBuf_;
        if (p.empty()) {
            // No path typed yet: let the user pick a PMS folder directly,
            // so clicking Import always does something.
            p = pickFolder("Import PMS folder");
            if (p.empty()) return;   // dialog canceled
            std::snprintf(manualDirBuf_, sizeof(manualDirBuf_), "%s", p.c_str());
        }
        addDroppedPath(p);
    }
}

void App::RenderTasks() {
    ImGui::SeparatorText(tr("Tasks", "任务"));
    auto tasks = conv_.snapshot();
    if (tasks.empty()) {
        ImGui::TextDisabled(tr("(no tasks)", "（无任务）"));
        return;
    }
    if (ImGui::BeginTable("tasks", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 180))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn(tr("Folder", "文件夹"), ImGuiTableColumnFlags_WidthStretch, 0);
        ImGui::TableSetupColumn(tr("Status", "状态"), ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn(tr("Result", "结果"), ImGuiTableColumnFlags_WidthStretch, 0);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < tasks.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            // Invisible full-row hit area for the right-click context menu.
            // Header colors are pushed transparent so no hover highlight shows.
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
            ImGui::TableSetColumnIndex(0);
            ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns);
            ImGui::PopStyleColor(3);
            if (ImGui::BeginPopupContextItem("##taskctx")) {
                if (ImGui::MenuItem(tr("Delete task", "删除任务"))) conv_.removeTask(i);
                if (ImGui::MenuItem(tr("Delete all tasks", "删除全部任务"))) conv_.clearTasks();
                ImGui::EndPopup();
            }
            // Cell contents (submitted after the selectable so they stay clickable).
            ImGui::TableSetColumnIndex(0);
            // Center the checkbox in its narrow column (the checkbox already
            // drives the row height, so vertical alignment is automatic).
            float cbSize = ImGui::GetFrameHeight();
            float colW = ImGui::GetColumnWidth(0);
            float xOff = (colW - cbSize) * 0.5f;
            if (xOff > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOff);
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
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Text(tr("Progress: %d%%   Current: %s", "进度：%d%%   当前：%s"),
                conv_.progress(), conv_.currentTask().c_str());
}

void App::RenderOptions() {
    ImGui::SeparatorText(tr("Options (one-click apply)", "选项（一键应用）"));

    // Row 1: metadata inputs (label first, then input box).
    // Creator / OD / HP, no AR (osu!mania ignores ApproachRate).
    ImGui::TextUnformatted(tr("Creator", "制作人"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::InputText("##creator", creatorBuf_, sizeof(creatorBuf_));
    ImGui::SameLine();
    ImGui::TextUnformatted("OD");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##od", odBuf_, sizeof(odBuf_));
    ImGui::SameLine();
    ImGui::TextUnformatted("HP");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("##hp", hpBuf_, sizeof(hpBuf_));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Normalize audio", "音频归一化"), &normalize_);

    // Row 2: output directory (label before the box)
    ImGui::TextUnformatted(tr("Output dir", "输出目录"));
    ImGui::SameLine();
    float outW = ImGui::GetContentRegionAvail().x;
    if (outW < 100) outW = 100;
    ImGui::SetNextItemWidth(outW);
    ImGui::InputText("##outdir", outBuf_, sizeof(outBuf_));

    // Row 3: Browse + export (export button fills the remaining width)
    if (ImGui::Button(tr("Browse...", "浏览..."))) DoBrowseOutput();
    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(running_ ? tr("Stop", "停止") : tr("Export .osz", "导出 .osz"), ImVec2(avail, 0)))
        running_ ? DoStop() : DoStart();
}

void App::RenderLog() {
    ImGui::SeparatorText(tr("Log", "日志"));
    ImGui::BeginChild("log", ImVec2(0, 0), true);
    for (const auto& l : logLines_) ImGui::TextUnformatted(l.c_str());
    if (!logLines_.empty()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}
