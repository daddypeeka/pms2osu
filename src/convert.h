#pragma once
// convert.h - background conversion worker.
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace conv {

struct Options {
    bool normalize = true;
    double od = 7.5;
    double hp = 7.5;
    double ar = 5.0;
    std::string creator = "PMS";
    std::string outputDir; // empty -> parent of source folder
    int sampleRate = 44100;
    bool openOutDir = false;
};

enum Status { S_Ready = 0, S_Running, S_Done, S_Failed, S_Canceled };

struct Task {
    std::string srcDir;
    std::vector<std::string> pmsFiles;
    bool enabled = true;
    Status status = S_Ready;
    std::string msg;
    std::string oszPath;
    std::string outDir;
};

class Converter {
public:
    Converter() = default;
    ~Converter();
    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;

    void setOptions(const Options& o);
    bool addFolder(const std::string& dir);       // one pms folder -> one task (deduped)
    bool hasFolder(const std::string& dir) const; // true if a task already uses this folder
    void clearTasks();
    void removeTask(size_t idx);                  // remove one task by index
    void removeCompleted();                       // drop done/canceled tasks
    size_t taskCount() const;
    std::vector<Task> snapshot() const;
    void setTaskEnabled(size_t idx, bool enabled);

    bool start();
    void requestStop();
    void shutdown();
    void wait();           // join without requesting stop
    bool running() const;
    int  progress() const;
    std::string currentTask() const;
    void drainLog(std::vector<std::string>& out);

private:
    void worker();
    bool runTask(Task& t);
    bool runTaskImpl(Task& t);
    void log(const std::string& s);

    Options opts_;
    mutable std::mutex m_;
    std::vector<Task> tasks_;
    std::deque<std::string> log_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> progress_{0};
    std::string current_;
    std::thread th_;
};

} // namespace conv