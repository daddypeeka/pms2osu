#pragma once
// util.h - cross-platform path / file / encoding helpers.
#include <cstdint>
#include <string>
#include <vector>

namespace util {

// UTF-8 <-> wide string (Windows) / byte preserving (other platforms)
std::wstring utf8ToWide(const std::string& s);
std::string  wideToUtf8(const std::wstring& w);

// Decode a PMS/BMS text buffer. If it is valid UTF-8, keep it unchanged.
// Otherwise try Shift-JIS (CP932 on Windows, best effort elsewhere).
std::string decodeText(const std::string& raw);

// Make sure a path/string is UTF-8. If it is already valid UTF-8 it is
// returned unchanged; otherwise it is interpreted as the system ANSI code
// page (Windows) and converted to UTF-8. This makes drag&drop and manual
// paths robust against non-UTF-8 OS input.
std::string ensureUtf8(const std::string& s);

// Filesystem helpers (paths are UTF-8)
bool fileExists(const std::string& p);
bool dirExists(const std::string& p);
std::string readFile(const std::string& p);
bool writeFile(const std::string& p, const std::string& data);
bool copyFile(const std::string& src, const std::string& dst);
bool deleteFile(const std::string& p);
bool moveFile(const std::string& src, const std::string& dst, bool overwrite);
bool removeDirRecursive(const std::string& p);
std::vector<std::string> listFiles(const std::string& dir, const std::string& extLower, bool recursive);
std::vector<std::string> listDirsContainingPms(const std::string& root);
std::string tempDir();
std::string exeDir();
bool openInExplorer(const std::string& p);

// Path helpers
std::string fileName(const std::string& p);
std::string stem(const std::string& p);
std::string dirName(const std::string& p);
std::string joinPath(const std::string& a, const std::string& b);
std::string sanitizeFileName(const std::string& s);
std::string normalizeEntry(std::string rel);
std::string toLower(std::string s);

// Formatting
std::string numStr(double v);
std::string numStrG(double v);

} // namespace util