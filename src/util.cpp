#include "util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
namespace util {

// ---------------- encoding ----------------

std::wstring utf8ToWide(const std::string& s) {
#ifdef _WIN32
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
#else
    std::wstring w;
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
#endif
}

std::string wideToUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
#else
    std::string s;
    for (wchar_t c : w) s.push_back(static_cast<char>(c));
    return s;
#endif
}

static bool isValidUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { i += 1; continue; }
        int extra = 0;
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= n) return false;
        for (int k = 1; k <= extra; ++k)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

std::string decodeText(const std::string& raw) {
    if (isValidUtf8(raw)) return raw;
#ifdef _WIN32
    int n = MultiByteToWideChar(932, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    if (n <= 0) return raw;
    std::wstring w(n, L'\0');
    MultiByteToWideChar(932, 0, raw.c_str(), (int)raw.size(), &w[0], n);
    return wideToUtf8(w);
#else
    // Best effort: assume the bytes are Shift-JIS-like; without a codec we
    // simply pass them through so the map still converts.
    return raw;
#endif
}

std::string ensureUtf8(const std::string& s) {
    if (isValidUtf8(s)) return s;
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return wideToUtf8(w);
#else
    return s;
#endif
}

// ---------------- filesystem ----------------

static fs::path p8(const std::string& s) { return fs::u8path(s); }

bool fileExists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p8(p), ec) && !fs::is_directory(p8(p), ec);
}
bool dirExists(const std::string& p) {
    std::error_code ec;
    return fs::is_directory(p8(p), ec);
}

std::string readFile(const std::string& p) {
    std::ifstream f(p8(p), std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& p, const std::string& data) {
    std::ofstream f(p8(p), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return true;
}

bool copyFile(const std::string& src, const std::string& dst) {
    std::error_code ec;
    fs::copy_file(p8(src), p8(dst), fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool deleteFile(const std::string& p) {
    std::error_code ec;
    return fs::remove(p8(p), ec);
}

bool moveFile(const std::string& src, const std::string& dst, bool overwrite) {
#ifdef _WIN32
    std::wstring s = utf8ToWide(src), d = utf8ToWide(dst);
    DWORD flags = MOVEFILE_COPY_ALLOWED;
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    return MoveFileExW(s.c_str(), d.c_str(), flags) != 0;
#else
    std::error_code ec;
    fs::rename(p8(src), p8(dst), ec);
    if (ec && overwrite) {
        fs::remove(p8(dst), ec);
        fs::rename(p8(src), p8(dst), ec);
    }
    return !ec;
#endif
}

bool removeDirRecursive(const std::string& p) {
    std::error_code ec;
    fs::remove_all(p8(p), ec);
    return true;
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& extLower, bool recursive) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(p8(dir), ec)) return out;
    if (recursive) {
        fs::recursive_directory_iterator it(p8(dir), fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) &&
                toLower(it->path().extension().u8string()) == extLower)
                out.push_back(it->path().u8string());
        }
    } else {
        fs::directory_iterator it(p8(dir), fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) &&
                toLower(it->path().extension().u8string()) == extLower)
                out.push_back(it->path().u8string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> listDirsContainingPms(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(p8(root), ec)) return out;
    fs::recursive_directory_iterator it(p8(root), fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            bool has = false;
            fs::directory_iterator jt(it->path(), fs::directory_options::skip_permission_denied, ec), jend;
            for (; jt != jend; jt.increment(ec)) {
                if (jt->is_regular_file(ec) &&
                    toLower(jt->path().extension().u8string()) == ".pms") { has = true; break; }
            }
            if (has) out.push_back(it->path().u8string());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string tempDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH + 4];
    DWORD n = GetTempPathW(MAX_PATH + 4, buf);
    std::wstring w(buf, n);
    while (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
    return wideToUtf8(w);
#else
    const char* t = std::getenv("TMPDIR");
    return t ? std::string(t) : std::string("/tmp");
#endif
}

std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();
    std::wstring w(buf, n);
    auto pos = w.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::string();
    return wideToUtf8(w.substr(0, pos));
#else
    return std::string();
#endif
}

bool openInExplorer(const std::string& p) {
#ifdef _WIN32
    std::wstring w = utf8ToWide(p);
    bool isDir = dirExists(p);
    return (intptr_t)ShellExecuteW(nullptr, L"open", isDir ? w.c_str() : L"explorer.exe",
                                   isDir ? nullptr : (std::wstring(L"/select,\"") + w + L"\"").c_str(),
                                   nullptr, SW_SHOWNORMAL) > 32;
#else
    (void)p;
    return false;
#endif
}

// ---------------- path helpers ----------------

std::string fileName(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}
std::string stem(const std::string& p) {
    std::string f = fileName(p);
    auto pos = f.find_last_of('.');
    return pos == std::string::npos ? f : f.substr(0, pos);
}
std::string dirName(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    return pos == std::string::npos ? std::string() : p.substr(0, pos);
}
std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}
std::string sanitizeFileName(const std::string& s) {
    static const char* bad = "\\/:*?\"<>|";
    std::string out = s;
    for (auto& c : out) {
        for (const char* p = bad; *p; ++p) {
            if (c == *p) { c = '_'; break; }
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("output") : out;
}
std::string normalizeEntry(std::string rel) {
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    while (rel.compare(0, 2, "./") == 0) rel.erase(0, 2);
    return rel;
}
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// ---------------- formatting ----------------

std::string numStr(double v) {
    return std::to_string((long long)std::llround(v));
}
std::string numStrG(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

} // namespace util