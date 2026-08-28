#pragma once
// zip.h - minimal ZIP (store only) writer used for .osz packaging.
#include <string>
#include <vector>

namespace zip {

struct Entry {
    std::string name; // entry path inside the archive, '/' separated
    std::string data; // file content
};

// Write a zip archive. Entries are stored uncompressed which is valid and
// accepted by osu!.
bool writeZip(const std::string& path, const std::vector<Entry>& entries,
              std::string* err);

} // namespace zip