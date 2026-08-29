#include "convert.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "ogg.h"
#include "osu.h"
#include "pms.h"
#include "util.h"
#include "zip.h"

namespace fs = std::filesystem;
namespace conv {

Converter::~Converter() { shutdown(); }

void Converter::setOptions(const Options& o) {
    std::lock_guard<std::mutex> lk(m_);
    opts_ = o;
}

bool Converter::addFolder(const std::string& dir) {
    if (!util::dirExists(dir)) return false;
    // A "pms folder" is a single level of .pms files. Sub-folders that also
    // contain .pms are picked up separately by the big-folder scan.
    auto pms = util::listFiles(dir, ".pms", false);
    if (pms.empty()) return false;
    std::lock_guard<std::mutex> lk(m_);
    // Avoid adding the same folder more than once.
    std::string lower = util::toLower(dir);
    for (const auto& t : tasks_)
        if (util::toLower(t.srcDir) == lower) return false;
    Task t;
    t.srcDir = dir;
    t.pmsFiles = std::move(pms);
    tasks_.push_back(std::move(t));
    return true;
}

bool Converter::hasFolder(const std::string& dir) const {
    std::lock_guard<std::mutex> lk(m_);
    std::string lower = util::toLower(dir);
    for (const auto& t : tasks_)
        if (util::toLower(t.srcDir) == lower) return true;
    return false;
}

void Converter::clearTasks() {
    std::lock_guard<std::mutex> lk(m_);
    tasks_.clear();
}

void Converter::removeTask(size_t idx) {
    std::lock_guard<std::mutex> lk(m_);
    if (idx < tasks_.size()) tasks_.erase(tasks_.begin() + idx);
}

void Converter::removeCompleted() {
    std::lock_guard<std::mutex> lk(m_);
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [](const Task& t) {
                                    return t.status == S_Done || t.status == S_Canceled;
                                }),
                 tasks_.end());
}

size_t Converter::taskCount() const {
    std::lock_guard<std::mutex> lk(m_);
    return tasks_.size();
}

std::vector<Task> Converter::snapshot() const {
    std::lock_guard<std::mutex> lk(m_);
    return tasks_;
}

void Converter::setTaskEnabled(size_t idx, bool enabled) {
    std::lock_guard<std::mutex> lk(m_);
    if (idx < tasks_.size()) tasks_[idx].enabled = enabled;
}

bool Converter::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    cancel_ = false;
    progress_ = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        current_.clear();
    }
    // Reuse: if a previous run's thread is still around (the GUI never joins
    // it between runs), join it before assigning a new thread. Assigning onto
    // a joinable std::thread would call std::terminate() and crash the app.
    if (th_.joinable()) th_.join();
    th_ = std::thread(&Converter::worker, this);
    return true;
}

void Converter::requestStop() { cancel_ = true; }

void Converter::shutdown() {
    requestStop();
    if (th_.joinable()) th_.join();
}

void Converter::wait() {
    if (th_.joinable()) th_.join();
}

bool Converter::running() const { return running_; }
int Converter::progress() const { return progress_; }

std::string Converter::currentTask() const {
    std::lock_guard<std::mutex> lk(m_);
    return current_;
}

void Converter::log(const std::string& s) {
    std::lock_guard<std::mutex> lk(m_);
    log_.push_back(s);
    if (log_.size() > 2000) log_.pop_front();
}

void Converter::drainLog(std::vector<std::string>& out) {
    std::lock_guard<std::mutex> lk(m_);
    while (!log_.empty()) {
        out.push_back(log_.front());
        log_.pop_front();
    }
}

void Converter::worker() {
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> lk(m_);
        tasks = tasks_;
    }
    bool anyDone = false;
    std::string firstOutDir;

    size_t enabledCount = 0;
    for (const auto& t : tasks) if (t.enabled) ++enabledCount;
    size_t doneCount = 0;

    for (auto& t : tasks) {
        if (cancel_) break;
        if (!t.enabled) continue;
        {
            std::lock_guard<std::mutex> lk(m_);
            current_ = t.srcDir;
            for (auto& tt : tasks_) if (tt.srcDir == t.srcDir) tt.status = S_Running;
        }
        log("[start] " + t.srcDir);
        bool ok = runTask(t);
        if (ok && firstOutDir.empty()) firstOutDir = t.outDir;
        if (ok) anyDone = true;
        ++doneCount;
        progress_ = enabledCount ? (int)(doneCount * 100 / enabledCount) : 100;
        {
            std::lock_guard<std::mutex> lk(m_);
            for (auto& tt : tasks_) {
                if (tt.srcDir == t.srcDir) {
                    tt.status = ok ? S_Done : (cancel_ ? S_Canceled : S_Failed);
                    tt.msg = t.msg;
                    tt.oszPath = t.oszPath;
                }
            }
        }
    }

    if (cancel_) log("[canceled]");
    else log("[all done]");

    if (anyDone && !cancel_) {
        Options o;
        {
            std::lock_guard<std::mutex> lk(m_);
            o = opts_;
        }
        if (o.openOutDir && !firstOutDir.empty()) util::openInExplorer(firstOutDir);
    }
    running_ = false;
}

bool Converter::runTask(Task& t) {
    try {
        return runTaskImpl(t);
    } catch (const std::exception& e) {
        t.msg = std::string("exception: ") + e.what();
        log("  [error] " + t.msg);
        return false;
    } catch (...) {
        t.msg = "unknown exception";
        log("  [error] " + t.msg);
        return false;
    }
}

bool Converter::runTaskImpl(Task& t) {
    Options o;
    {
        std::lock_guard<std::mutex> lk(m_);
        o = opts_;
    }

    std::error_code ec;
    std::vector<zip::Entry> entries;
    std::string audioPms;
    if (!t.pmsFiles.empty()) audioPms = t.pmsFiles[0];

    // ---- render audio.ogg from the first (main) pms ----
    // Encoded straight into memory: no temp file, no disk round-trip.
    bool audioOk = false;
    if (!audioPms.empty()) {
        log("  render audio: " + util::fileName(audioPms));
        pms::Chart chart = pms::parseFile(audioPms);
        // sample paths are relative to the chart folder
        for (auto& kv : chart.wavs)
            kv.second.path = util::joinPath(util::dirName(audioPms), kv.second.path);

        pms::RenderOptions ropt;
        ropt.normalize = o.normalize;
        ropt.sampleRate = o.sampleRate;
        int rate = 0, frames = 0;
        std::string rerr;
        std::vector<float> pcm = pms::renderStereo(chart, ropt, rate, frames, &rerr);
        if (!pcm.empty()) {
            std::string oggData, oerr;
            audioOk = vorbis_wrap::writeOggMem(pcm, rate, 2, frames, 192000, 0.5f,
                                               &oggData, &oerr);
            if (!audioOk) {
                log("    ogg encode failed: " + oerr);
            } else {
                entries.push_back({"audio.ogg", std::move(oggData)});
                log("    audio.ogg (" +
                    std::to_string((int)(entries.back().data.size() / 1024)) + " KB)");
            }
        } else {
            log("    render failed");
        }
    }
    if (!audioOk) {
        // fallback: a tiny silent audio so the osz is still valid
        std::vector<float> silent(2 * 44100, 0.f);
        std::string oggData;
        if (vorbis_wrap::writeOggMem(silent, 44100, 2, 44100, 192000, 0.5f,
                                     &oggData, nullptr)) {
            entries.push_back({"audio.ogg", std::move(oggData)});
            audioOk = true;
        }
    }

    // ---- build one .osu per pms ----
    int okCount = 0;
    for (const auto& pmsPath : t.pmsFiles) {
        if (cancel_) break;
        pms::Chart chart = pms::parseFile(pmsPath);
        for (auto& kv : chart.wavs)
            kv.second.path = util::joinPath(util::dirName(pmsPath), kv.second.path);

        pms::Timing timing(chart);
        osu::Options oo;
        oo.od = o.od;
        oo.hp = o.hp;
        oo.ar = o.ar;
        oo.creator = o.creator.empty() ? "PMS" : o.creator;
        std::string osuText = osu::buildOsuText(chart, timing, oo);

        std::string osuName = util::sanitizeFileName(util::stem(pmsPath)) + ".osu";
        // Avoid duplicate entry names if two charts share the same filename stem.
        int dup = 1;
        std::string uniqueName = osuName;
        while (std::any_of(entries.begin(), entries.end(),
                           [&](const zip::Entry& e) { return e.name == uniqueName; })) {
            uniqueName = util::stem(osuName) + "_" + std::to_string(dup++) + ".osu";
        }
        entries.push_back({uniqueName, std::move(osuText)});
        log("  + " + uniqueName);
        ++okCount;

        // collect referenced background / banner images
        for (const std::string* ref : {&chart.background, &chart.banner}) {
            if (ref->empty()) continue;
            std::string imgPath = util::joinPath(util::dirName(pmsPath), *ref);
            if (util::fileExists(imgPath)) {
                std::string imgData = util::readFile(imgPath);
                std::string entryName = util::normalizeEntry(*ref);
                bool already = false;
                for (const auto& e : entries)
                    if (e.name == entryName) { already = true; break; }
                if (!already) entries.push_back({entryName, std::move(imgData)});
            }
        }
    }

    // ---- write .osz ----
    std::string outDir = o.outputDir.empty() ? util::dirName(t.srcDir) : o.outputDir;
    t.outDir = outDir;
    fs::create_directories(fs::u8path(outDir), ec);
    std::string oszName = util::sanitizeFileName(util::fileName(t.srcDir)) + ".osz";
    std::string oszPath = util::joinPath(outDir, oszName);
    std::string zerr;
    if (!zip::writeZip(oszPath, entries, &zerr)) {
        t.msg = "zip failed: " + zerr;
        log("  [error] " + t.msg);
        return false;
    }
    t.oszPath = oszPath;
    t.msg = "OK (" + std::to_string(okCount) + " charts)";
    log("  -> " + oszPath);
    return true;
}

} // namespace conv