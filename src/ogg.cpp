#include "ogg.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "util.h"

namespace vorbis_wrap {

namespace {

// Output sink: writes to either a FILE* or an in-memory std::string.
struct Sink {
    FILE* file = nullptr;
    std::string* mem = nullptr;
    bool ok = true;

    void write(const unsigned char* p, size_t n) {
        if (!ok) return;
        if (mem) {
            mem->append((const char*)p, n);
        } else if (file) {
            if (std::fwrite(p, 1, n, file) != n) ok = false;
        }
    }
    void write(const char* p, size_t n) {
        write(reinterpret_cast<const unsigned char*>(p), n);
    }
};

bool initEncoder(vorbis_info& vi, int channels, int sampleRate,
                 int bitrateBps, float quality) {
    vorbis_info_init(&vi);
    if (bitrateBps > 0) {
        // Managed bitrate pinned to ~bitrateBps: setting min/max/nominal all
        // to the same value makes libvorbis stay close to the target.
        if (vorbis_encode_init(&vi, channels, sampleRate,
                               bitrateBps, bitrateBps, bitrateBps) != 0)
            return false;
    } else {
        if (vorbis_encode_init_vbr(&vi, channels, sampleRate, quality) != 0)
            return false;
    }
    return true;
}

// Shared encode core. Writes Ogg pages through `sink`.
bool encodeOgg(const std::vector<float>& interleaved,
               int sampleRate, int channels, int nFrames,
               int bitrateBps, float quality,
               Sink& sink, std::string* err) {
    if (channels < 1 || channels > 2) {
        if (err) *err = "only 1 or 2 channels supported";
        return false;
    }
    if (interleaved.size() < (size_t)nFrames * (size_t)channels) {
        if (err) *err = "sample count mismatch";
        return false;
    }

    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;

    if (!initEncoder(vi, channels, sampleRate, bitrateBps, quality)) {
        vorbis_info_clear(&vi);
        if (err) *err = "vorbis encoder init failed";
        return false;
    }
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "pms2ogg192");
    if (bitrateBps > 0)
        vorbis_comment_add_tag(&vc, "ENCODED_MODE",
                               (std::to_string(bitrateBps / 1000) + " kbps managed").c_str());

    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    std::srand(12345);
    if (ogg_stream_init(&os, std::rand()) != 0) {
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        if (err) *err = "ogg_stream_init failed";
        return false;
    }

    // write headers
    {
        ogg_packet header, header_comm, header_code;
        vorbis_analysis_headerout(&vd, &vc, &header, &header_comm, &header_code);
        ogg_stream_packetin(&os, &header);
        ogg_stream_packetin(&os, &header_comm);
        ogg_stream_packetin(&os, &header_code);
        while (ogg_stream_flush(&os, &og)) {
            sink.write(og.header, (size_t)og.header_len);
            sink.write(og.body, (size_t)og.body_len);
        }
    }

    // feed samples in blocks
    const int kBlock = 4096;
    int frame = 0;
    while (frame < nFrames && sink.ok) {
        int count = std::min(kBlock, nFrames - frame);
        float** buf = vorbis_analysis_buffer(&vd, count);
        for (int i = 0; i < count; ++i) {
            const float* src = &interleaved[(size_t)(frame + i) * (size_t)channels];
            buf[0][i] = src[0];
            if (channels == 2) buf[1][i] = src[1];
        }
        vorbis_analysis_wrote(&vd, count);
        frame += count;

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    sink.write(og.header, (size_t)og.header_len);
                    sink.write(og.body, (size_t)og.body_len);
                    if (!sink.ok) break;
                }
            }
        }
    }

    // flush
    if (sink.ok) {
        vorbis_analysis_wrote(&vd, 0);
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    sink.write(og.header, (size_t)og.header_len);
                    sink.write(og.body, (size_t)og.body_len);
                    if (!sink.ok) break;
                }
            }
        }
        // empty flush
        while (ogg_stream_flush(&os, &og)) {
            sink.write(og.header, (size_t)og.header_len);
            sink.write(og.body, (size_t)og.body_len);
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);

    if (!sink.ok) {
        if (err) *err = "failed writing ogg pages";
        return false;
    }
    return true;
}

} // namespace

bool writeOgg(const std::string& path,
              const std::vector<float>& interleaved,
              int sampleRate, int channels, int nFrames,
              int bitrateBps,
              float quality,
              std::string* err) {
#ifdef _WIN32
    FILE* fp = _wfopen(util::utf8ToWide(path).c_str(), L"wb");
#else
    FILE* fp = std::fopen(path.c_str(), "wb");
#endif
    if (!fp) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    Sink sink;
    sink.file = fp;
    bool ok = encodeOgg(interleaved, sampleRate, channels, nFrames,
                        bitrateBps, quality, sink, err);
    std::fclose(fp);
    return ok;
}

bool writeOggMem(const std::vector<float>& interleaved,
                 int sampleRate, int channels, int nFrames,
                 int bitrateBps, float quality,
                 std::string* out, std::string* err) {
    if (!out) {
        if (err) *err = "null output buffer";
        return false;
    }
    out->clear();
    // Reasonable size estimate (192 kbps ≈ 0.5 byte/frame) so the buffer
    // avoids most reallocations without over-allocating for long songs.
    out->reserve((size_t)nFrames / 2 + 8192);
    Sink sink;
    sink.mem = out;
    return encodeOgg(interleaved, sampleRate, channels, nFrames,
                     bitrateBps, quality, sink, err);
}

} // namespace vorbis_wrap
