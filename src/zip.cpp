#include "zip.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "util.h"

namespace zip {

namespace {

uint32_t g_crcTable[256];
bool g_crcReady = false;

void initCrc() {
    if (g_crcReady) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        g_crcTable[i] = c;
    }
    g_crcReady = true;
}

uint32_t crc32(const char* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = g_crcTable[(c ^ (uint8_t)data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void putU16(std::string& b, uint16_t v) {
    b.push_back((char)(v & 0xFF));
    b.push_back((char)((v >> 8) & 0xFF));
}
void putU32(std::string& b, uint32_t v) {
    b.push_back((char)(v & 0xFF));
    b.push_back((char)((v >> 8) & 0xFF));
    b.push_back((char)((v >> 16) & 0xFF));
    b.push_back((char)((v >> 24) & 0xFF));
}

struct CentralEntry {
    std::string name;
    uint32_t crc = 0, size = 0, offset = 0;
};

} // namespace

bool writeZip(const std::string& path, const std::vector<Entry>& entries,
              std::string* err) {
    initCrc();

#ifdef _WIN32
    FILE* f = _wfopen(util::utf8ToWide(path).c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }

    std::vector<CentralEntry> cd;
    uint32_t offset = 0;

    for (const auto& e : entries) {
        std::string name = util::normalizeEntry(e.name);
        if (name.empty()) continue;
        const std::string& data = e.data;
        uint32_t sz = (uint32_t)data.size();
        uint32_t crc = crc32(data.data(), data.size());
        uint16_t nameLen = (uint16_t)name.size();

        // local file header
        std::string lh;
        putU32(lh, 0x04034b50);
        putU16(lh, 20);
        putU16(lh, 0x0800); // UTF-8 flag
        putU16(lh, 0);      // store
        putU16(lh, 0);
        putU16(lh, 0);
        putU32(lh, crc);
        putU32(lh, sz);
        putU32(lh, sz);
        putU16(lh, nameLen);
        putU16(lh, 0);
        lh += name;
        if (std::fwrite(lh.data(), 1, lh.size(), f) != lh.size()) {
            std::fclose(f);
            if (err) *err = "write failed";
            return false;
        }
        if (sz && std::fwrite(data.data(), 1, data.size(), f) != data.size()) {
            std::fclose(f);
            if (err) *err = "write failed";
            return false;
        }

        CentralEntry ce;
        ce.name = name;
        ce.crc = crc;
        ce.size = sz;
        ce.offset = offset;
        cd.push_back(ce);
        offset += (uint32_t)lh.size() + sz;
    }

    // central directory
    uint32_t cdStart = offset;
    std::string cdbuf;
    for (const auto& ce : cd) {
        std::string b;
        putU32(b, 0x02014b50);
        putU16(b, 20);
        putU16(b, 20);
        putU16(b, 0x0800);
        putU16(b, 0);
        putU16(b, 0);
        putU16(b, 0);
        putU32(b, ce.crc);
        putU32(b, ce.size);
        putU32(b, ce.size);
        putU16(b, (uint16_t)ce.name.size());
        putU16(b, 0);
        putU16(b, 0);
        putU16(b, 0);
        putU16(b, 0);
        putU32(b, 0);
        putU32(b, ce.offset);
        b += ce.name;
        cdbuf += b;
    }
    std::fwrite(cdbuf.data(), 1, cdbuf.size(), f);

    // EOCD
    std::string eocd;
    putU32(eocd, 0x06054b50);
    putU16(eocd, 0);
    putU16(eocd, 0);
    putU16(eocd, (uint16_t)cd.size());
    putU16(eocd, (uint16_t)cd.size());
    putU32(eocd, (uint32_t)cdbuf.size());
    putU32(eocd, cdStart);
    putU16(eocd, 0);
    std::fwrite(eocd.data(), 1, eocd.size(), f);

    std::fclose(f);
    return true;
}

} // namespace zip