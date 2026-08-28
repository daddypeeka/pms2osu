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

struct FileOut {
    FILE* f = nullptr;
};

size_t oggWrite(void* ptr, size_t size, size_t nmemb, void* datasource) {
    FileOut* fo = (FileOut*)datasource;
    if (!fo->f) return 0;
    return std::fwrite(ptr, size, nmemb, fo->f);
}
int oggClose(void* datasource) {
    FileOut* fo = (FileOut*)datasource;
    if (fo->f) { std::fclose(fo->f); fo->f = nullptr; }
    return 0;
}

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

} // namespace

bool writeOgg(const std::string& path,
              const std::vector<float>& interleaved,
              int sampleRate, int channels, int nFrames,
              int bitrateBps,
              float quality,
              std::string* err) {
    if (channels < 1 || channels > 2) {
        if (err) *err = "only 1 or 2 channels supported";
        return false;
    }
#ifdef _WIN32
    FILE* fp = _wfopen(util::utf8ToWide(path).c_str(), L"wb");
#else
    FILE* fp = std::fopen(path.c_str(), "wb");
#endif
    if (!fp) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    if (interleaved.size() < (size_t)nFrames * (size_t)channels) {
        std::fclose(fp);
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
        std::fclose(fp);
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
        std::fclose(fp);
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
            std::fwrite(og.header, 1, og.header_len, fp);
            std::fwrite(og.body, 1, og.body_len, fp);
        }
    }

    // feed samples in blocks
    const int kBlock = 2048;
    int frame = 0;
    bool ok = true;
    while (frame < nFrames && ok) {
        int count = std::min(kBlock, nFrames - frame);
        float** buf = vorbis_analysis_buffer(&vd, count);
        for (int i = 0; i < count; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                int idx = (frame + i) * channels + ch;
                buf[ch][i] = interleaved[idx];
            }
        }
        vorbis_analysis_wrote(&vd, count);
        frame += count;

        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    if (std::fwrite(og.header, 1, og.header_len, fp) != (size_t)og.header_len ||
                        std::fwrite(og.body, 1, og.body_len, fp) != (size_t)og.body_len) {
                        ok = false;
                        break;
                    }
                }
            }
        }
    }

    // flush
    if (ok) {
        vorbis_analysis_wrote(&vd, 0);
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    if (std::fwrite(og.header, 1, og.header_len, fp) != (size_t)og.header_len ||
                        std::fwrite(og.body, 1, og.body_len, fp) != (size_t)og.body_len) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        // empty flush
        while (ogg_stream_flush(&os, &og)) {
            std::fwrite(og.header, 1, og.header_len, fp);
            std::fwrite(og.body, 1, og.body_len, fp);
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    std::fclose(fp);

    if (!ok && err) *err = "failed writing ogg pages";
    return ok;
}

} // namespace vorbis_wrap
