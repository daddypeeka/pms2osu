#include "pms.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "util.h"

// ---- Ogg/Vorbis decode ----
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

namespace pms {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int hexToInt(const std::string& s) {
    int r = 0;
    for (char c : s) {
        int v = hexVal(c);
        if (v < 0) return -1;
        r = r * 16 + v;
    }
    return r;
}

// BMS object indices (WAV/BPM/STOP) are base-36 (0-9, A-Z), not hex.
int base36Val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}
int base36ToInt(const std::string& s) {
    int r = 0;
    for (char c : s) {
        int v = base36Val(c);
        if (v < 0) return -1;
        r = r * 36 + v;
    }
    return r;
}

bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

double parseFloatSafe(const std::string& s) {
    const char* b = s.c_str();
    char* end = nullptr;
    double v = std::strtod(b, &end);
    if (end == b) return 0.0 / 0.0;
    return v;
}

// Split a string into 2-char tokens (BMS channel payloads), skipping whitespace.
std::vector<std::string> splitEvery2(const std::string& s) {
    std::string clean;
    for (char c : s)
        if (!std::isspace((unsigned char)c)) clean += c;
    std::vector<std::string> out;
    for (size_t i = 0; i + 2 <= clean.size(); i += 2)
        out.push_back(clean.substr(i, 2));
    return out;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Channel-number normalization for note channels.
// Returns true and fills chNum/flags if it is a note-style channel.
bool normalizeNoteChannel(const std::string& ch, int& chNum, bool& isLong,
                          bool& isInvisible, bool& isDamage) {
    chNum = hexToInt(ch);
    if (chNum < 0) return false;
    isLong = isInvisible = isDamage = false;
    if (chNum > 0xD0) { isDamage = true; chNum -= 0xC0; }
    else if (chNum > 0x50) { isLong = true; chNum -= 0x40; }
    else if (chNum > 0x30) { isInvisible = true; chNum -= 0x20; }
    return true;
}

} // namespace

// ---------------- parsing ----------------

Chart parseText(const std::string& text, const std::string& fileName) {
    Chart c;
    std::string decoded = util::decodeText(text);

    for (const auto& line0 : splitLines(decoded)) {
        if (line0.empty() || line0[0] != '#') continue;

        // Channel line: #mmmcc:data  (3-digit measure, 2-char channel)
        if (line0.size() >= 7 && line0[6] == ':' &&
            isAllDigits(line0.substr(1, 3))) {
            double measure = (double)std::atoi(line0.substr(1, 3).c_str());
            std::string ch = line0.substr(4, 2);
            std::string data = line0.substr(7);

            if (ch == "02") {
                double v = parseFloatSafe(trim(data));
                if (!std::isnan(v) && v > 0)
                    c.timeSigs.push_back({measure, v * 4.0});
                continue;
            }
            // Channel 02 already handled above; other channels are token streams.
            auto toks = splitEvery2(data);
            if (toks.empty()) continue;
            size_t denom = toks.size();
            for (size_t j = 0; j < toks.size(); ++j) {
                Chart::Event e;
                e.measure = measure + (double)j / (double)denom;
                e.channel = ch;
                e.token = toks[j];
                c.events.push_back(std::move(e));
            }
            continue;
        }

        // Header command: #NAME value
        size_t sp = line0.find(' ', 1);
        if (sp == std::string::npos) continue;
        std::string name = upper(trim(line0.substr(1, sp - 1)));
        std::string val = trim(line0.substr(sp + 1));

        if (name == "TITLE") { c.title = val; continue; }
        if (name == "SUBTITLE") { c.subtitle = val; continue; }
        if (name == "ARTIST") { c.artist = val; continue; }
        if (name == "SUBARTIST") { c.subArtist = val; continue; }
        if (name == "GENRE") { c.genre = val; continue; }
        if (name == "MAKER") { c.maker = val; continue; }
        if (name == "COMMENT") { c.comment = val; continue; }
        if (name == "STAGEFILE") { c.stageFile = val; continue; }
        if (name == "BANNER") { c.banner = val; continue; }
        if (name == "BACKBMP") { c.background = val; continue; }
        if (name == "BPM") {
            double b = parseFloatSafe(val);
            if (!std::isnan(b) && b > 0) c.defaultBpm = b;
            continue;
        }
        if (name == "PLAYER") {
            int p = (int)std::strtol(val.c_str(), nullptr, 10);
            if (p >= 1 && p <= 4) c.player = p;
            continue;
        }
        if (name == "LNOBJ") { c.lnoBJ = val; continue; }
        if (name == "DIFFICULTY") {
            c.difficulty = (int)std::strtol(val.c_str(), nullptr, 10);
            continue;
        }
        if (name == "PLAYLEVEL") {
            c.playlevel = (int)std::strtol(val.c_str(), nullptr, 10);
            continue;
        }

        // Indexed commands: WAVxx / EXWAVxx / BPMxx / STOPxx
        // NOTE: xx is base-36 in BMS (e.g. 0G, I5, 7K), NOT hex.
        if (name.size() > 2) {
            std::string tag = name.substr(0, name.size() - 2);
            std::string idx = name.substr(name.size() - 2);
            if (base36ToInt(idx) < 0) continue;
            if (tag == "WAV") {
                c.wavs[idx].path = val;
            } else if (tag == "EXWAV") {
                // p/v/f  modifications + filename
                std::vector<std::string> parts;
                std::string cur;
                for (char cc : val) {
                    if (cc == ' ' || cc == '\t') { if (!cur.empty()) { parts.push_back(cur); cur.clear(); } }
                    else cur += cc;
                }
                if (!cur.empty()) parts.push_back(cur);
                if (parts.size() >= 2) {
                    const std::string& head = parts[0];
                    auto& w = c.wavs[idx];
                    for (size_t k = 0; k < head.size() && k + 1 < parts.size(); ++k) {
                        double v = parseFloatSafe(parts[k + 1]);
                        if (std::isnan(v)) continue;
                        switch (head[k]) {
                            case 'p': if (v >= -10000 && v <= 10000) w.pan = v / 10000.0; break;
                            case 'v': if (v >= -10000 && v <= 0) w.volume = v / 10000.0 + 1.0; break;
                            default: break;
                        }
                    }
                    w.path = parts.back();
                } else {
                    c.wavs[idx].path = val;
                }
            } else if (tag == "BPM" || tag == "EXBPM") {
                double b = parseFloatSafe(val);
                if (!std::isnan(b) && b > 0) c.bpms[idx] = b;
            } else if (tag == "STOP") {
                double b = parseFloatSafe(val);
                if (!std::isnan(b) && b >= 0) c.stops[idx] = b;
            }
            continue;
        }
    }

    // Determine track type from used key channels.
    auto hasCh = [&](int ch) {
        for (const auto& e : c.events) {
            int n = hexToInt(e.channel);
            if (n == ch || n == ch + 0x20 || n == ch + 0x40 || n == ch + 0xC0) return true;
        }
        return false;
    };
    auto hasAny = [&](std::initializer_list<int> chs) {
        for (int ch : chs) if (hasCh(ch)) return true;
        return false;
    };
    if (!hasAny({0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29})) {
        c.trackType = "pms3"; c.trackKey = 3;
    } else if (!hasAny({0x11, 0x12, 0x16, 0x17, 0x18, 0x19, 0x21, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29})) {
        c.trackType = "pms5"; c.trackKey = 5;
    } else if (!hasAny({0x16, 0x17, 0x18, 0x19, 0x21, 0x26, 0x27, 0x28, 0x29})) {
        c.trackType = "pms"; c.trackKey = 9;
    } else {
        c.trackType = "pmse"; c.trackKey = 9;
    }
    (void)fileName;
    return c;
}

Chart parseFile(const std::string& path) {
    std::string raw = util::readFile(path);
    return parseText(raw, util::fileName(path));
}

bool keyTrackFor(const Chart& c, const std::string& channel, int& track, int& player) {
    int ch = hexToInt(channel);
    if (ch < 0) return false;
    player = 0;
    const std::string& t = c.trackType;
    if (t == "pms3") {
        if (ch >= 0x11 && ch <= 0x13) { track = ch - 0x11; return true; }
    } else if (t == "pms5") {
        if (ch >= 0x13 && ch <= 0x15) { track = ch - 0x13; return true; }
        if (ch >= 0x22 && ch <= 0x23) { track = ch - 0x1F; return true; } // -> 3,4
    } else if (t == "pms" || t == "pmse") {
        if (ch >= 0x11 && ch <= 0x15) { track = ch - 0x11; return true; }
        if (ch >= 0x22 && ch <= 0x25) { track = ch - 0x1D; return true; } // -> 5..8
    }
    return false;
}

// ---------------- timing ----------------

Timing::Timing(const Chart& c) {
    bpm_ = c.defaultBpm > 0 ? c.defaultBpm : 120;
    double defaultBpm = bpm_;

    // bpm / bar-length / stop changes keyed by measure.
    // NOTE: unlike BPM (which persists once changed), the BMS channel 02
    // "bar length ratio" applies to ONE bar only (per bmx2wav semantics).
    std::map<double, double> bpmAt;    // measure -> bpm (persistent)
    std::map<double, double> barBpn;   // measure -> beats-per-measure for that bar ONLY
    std::map<double, double> stopAt;   // measure -> stop seconds

    bpmAt[0] = defaultBpm;

    for (const auto& ts : c.timeSigs) {
        // channel 02 value v is a bar-length ratio: beats = v * 4
        barBpn[ts.measure] = ts.bpm;
        if (ts.measure <= 0.001) meter_ = ts.bpm; // main meter from measure 0
    }

    for (const auto& e : c.events) {
        // bmx2wav semantics:
        //   channel 03 = direct BPM change (value is hex BPM)
        //   channel 08 = extended BPM change (index into #BPMxx)
        //   channel 09 = STOP sequence
        if (e.channel == "03") {
            int b = hexToInt(e.token);
            if (b > 0) bpmAt[e.measure] = (double)b;
        } else if (e.channel == "08") {
            auto it = c.bpms.find(e.token);
            if (it != c.bpms.end()) bpmAt[e.measure] = it->second;
        } else if (e.channel == "09") {
            auto it = c.stops.find(e.token);
            if (it != c.stops.end()) {
                // bmx2wav: stop seconds = (60/bpm) * (value/192) * 4  -> value/48 beats
                double beats = it->second / 192.0 * 4.0;
                double bpmHere = bpmAt.empty() ? defaultBpm : bpmAt.rbegin()->second;
                stopAt[e.measure] += beats * 60.0 / bpmHere;
            }
        }
    }

    // collect all relevant measures
    std::set<double> measures;
    for (const auto& kv : bpmAt) measures.insert(kv.first);
    for (const auto& kv : barBpn) measures.insert(kv.first);
    for (const auto& kv : stopAt) measures.insert(kv.first);
    for (const auto& e : c.events) measures.insert(e.measure);
    // end measure: last event measure + one measure
    double last = measures.empty() ? 0 : *measures.rbegin();
    measures.insert(last + 1.0);
    // Add every integer bar boundary so a segment never spans two bars
    // (bar length is per-bar, not persistent).
    for (int i = 0; i <= (int)last + 1; ++i) measures.insert((double)i);

    // beats per measure for the bar that contains measure m
    auto bpnFor = [&](double m) -> double {
        double bar = std::floor(m);
        auto it = barBpn.find(bar);
        return (it != barBpn.end()) ? it->second : 4.0;
    };

    // Build segments
    double prevM = 0, prevTime = 0, prevBpm = defaultBpm, prevStop = 0;
    bool first = true;
    std::map<double, double> bpmDuration; // bpm -> total duration (seconds)
    bpmPoints_.push_back({0.0, defaultBpm});

    for (double m : measures) {
        if (first) { prevM = m; first = false; continue; }
        Segment s;
        s.from = prevM;
        s.to = m;
        s.secPerMeasure = bpnFor(prevM) * 60.0 / prevBpm;
        s.stopSec = prevStop;
        s.timeAtFrom = prevTime;
        segs_.push_back(s);
        // advance
        double segDur = (m - prevM) * s.secPerMeasure;
        prevTime += segDur + prevStop;
        bpmDuration[prevBpm] += segDur + prevStop;

        auto itb = bpmAt.find(m);
        if (itb != bpmAt.end() && itb->second != prevBpm) {
            prevBpm = itb->second;
            bpmPoints_.push_back({prevTime, prevBpm});
        }
        auto its = stopAt.find(m);
        prevStop = (its != stopAt.end()) ? its->second : 0.0;
        prevM = m;
    }
    endMeasure_ = last;

    if (std::getenv("PMS2OGG_DEBUG")) {
        std::printf("  [timing-debug] last=%.3f endMeasure=%.3f endTime=%.3f mainBpm=%.1f segments=%zu\n",
                    last, endMeasure_, this->measureToTime(endMeasure_), bpm_, segs_.size());
        std::printf("    bpmAt:");
        for (auto& kv : bpmAt) std::printf(" %.0f@%.1f", kv.first, kv.second);
        std::printf("\n    barBpn:");
        for (auto& kv : barBpn) std::printf(" %.0f@%.1f", kv.first, kv.second);
        std::printf("\n    first 8 segments:\n");
        for (size_t i = 0; i < segs_.size() && i < 8; ++i)
            std::printf("      [%.2f..%.2f] secPerM=%.3f t0=%.3f\n",
                        segs_[i].from, segs_[i].to, segs_[i].secPerMeasure, segs_[i].timeAtFrom);
        std::printf("    last 8 segments (total=%zu):\n", segs_.size());
        for (size_t i = segs_.size() > 8 ? segs_.size() - 8 : 0; i < segs_.size(); ++i)
            std::printf("      [%.2f..%.2f] secPerM=%.3f t0=%.3f\n",
                        segs_[i].from, segs_[i].to, segs_[i].secPerMeasure, segs_[i].timeAtFrom);
        std::printf("    prevTime(final)=%.3f\n", prevTime);
    }

    // main BPM = the one covering the longest duration (like rmstZ getMainBpm)
    double bestDur = 0;
    for (const auto& kv : bpmDuration)
        if (kv.second > bestDur) { bestDur = kv.second; bpm_ = kv.first; }
    if (bpmDuration.empty()) bpm_ = defaultBpm;
}

double Timing::measureToTime(double measure) const {
    if (segs_.empty()) return 0;
    for (const auto& s : segs_) {
        if (measure < s.to)
            return s.timeAtFrom + (measure - s.from) * s.secPerMeasure;
    }
    const Segment& last = segs_.back();
    return last.timeAtFrom + (measure - last.from) * last.secPerMeasure;
}

// ---------------- note collection ----------------

std::vector<NoteEvent> collectNotes(const Chart& c, const Timing& timing) {
    struct Raw {
        double measure;
        int track;
        std::string token;
        bool isLong;
        bool invisible;
    };
    std::vector<Raw> raw;
    for (const auto& e : c.events) {
        int chNum; bool isLong, isInvisible, isDamage;
        if (!normalizeNoteChannel(e.channel, chNum, isLong, isInvisible, isDamage)) continue;
        if (e.token == "00") continue;
        int track, player;
        if (!keyTrackFor(c, hex2ch(chNum), track, player)) continue;
        (void)player;
        if (isDamage) continue; // landmines not supported in mania output
        Raw r;
        r.measure = e.measure;
        r.track = track;
        r.token = e.token;
        r.isLong = isLong;
        r.invisible = isInvisible;
        raw.push_back(r);
    }
    std::stable_sort(raw.begin(), raw.end(),
                     [](const Raw& a, const Raw& b) {
                         if (a.track != b.track) return a.track < b.track;
                         return a.measure < b.measure;
                     });

    // Pair long notes (per track).
    std::vector<NoteEvent> out;
    for (size_t i = 0; i < raw.size();) {
        size_t j = i;
        while (j + 1 < raw.size() && raw[j + 1].track == raw[i].track) ++j;
        // sub-range raw[i..j] belongs to one track
        std::vector<Raw> tr(raw.begin() + i, raw.begin() + j + 1);

        // Step 1: visible/invisible long channels - consecutive same token.
        std::vector<NoteEvent> trackNotes;
        for (size_t k = 0; k < tr.size();) {
            NoteEvent ne;
            ne.measure = tr[k].measure;
            ne.time = timing.measureToTime(ne.measure);
            ne.track = tr[k].track;
            ne.wav = tr[k].token;
            ne.isLong = false;
            if (tr[k].isLong && k + 1 < tr.size() && tr[k + 1].token == tr[k].token) {
                ne.isLong = true;
                ne.endTime = timing.measureToTime(tr[k + 1].measure);
                trackNotes.push_back(ne);
                k += 2;
            } else {
                trackNotes.push_back(ne);
                k += 1;
            }
        }

        // Step 2: LNOBJ pairing on normal (non-long) tokens.
        if (!c.lnoBJ.empty()) {
            std::vector<NoteEvent> paired;
            for (auto& n : trackNotes) {
                if (!n.isLong && n.wav == c.lnoBJ) {
                    // find the previous normal note that is not an LNOBJ marker
                    for (auto it = paired.rbegin(); it != paired.rend(); ++it) {
                        if (!it->isLong && it->wav != c.lnoBJ) {
                            it->isLong = true;
                            it->endTime = n.time;
                            break;
                        }
                    }
                    continue; // skip the marker
                }
                paired.push_back(n);
            }
            trackNotes = std::move(paired);
        }

        out.insert(out.end(), trackNotes.begin(), trackNotes.end());
        i = j + 1;
    }

    std::sort(out.begin(), out.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.track < b.track;
    });
    return out;
}

std::vector<NoteEvent> collectAudioEvents(const Chart& c, const Timing& timing) {
    std::vector<NoteEvent> out;
    for (const auto& e : c.events) {
        if (e.token == "00") continue;
        // BGM channel 01
        if (e.channel == "01") {
            NoteEvent ne;
            ne.measure = e.measure;
            ne.time = timing.measureToTime(ne.measure);
            ne.track = -1;
            ne.wav = e.token;
            out.push_back(ne);
            continue;
        }
        int chNum; bool isLong, isInvisible, isDamage;
        if (!normalizeNoteChannel(e.channel, chNum, isLong, isInvisible, isDamage)) continue;
        int track, player;
        if (!keyTrackFor(c, hex2ch(chNum), track, player)) continue;
        NoteEvent ne;
        ne.measure = e.measure;
        ne.time = timing.measureToTime(ne.measure);
        ne.track = track;
        ne.wav = e.token;
        out.push_back(ne);
    }
    std::sort(out.begin(), out.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.time < b.time;
    });
    return out;
}

// ---------------- sample loading ----------------

namespace {

// memory-backed callback source for libvorbisfile
struct MemSrc {
    const unsigned char* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

size_t memRead(void* ptr, size_t size, size_t nmemb, void* datasource) {
    MemSrc* m = (MemSrc*)datasource;
    size_t avail = m->size - m->pos;
    size_t want = size * nmemb;
    if (want > avail) want = avail;
    if (want) std::memcpy(ptr, m->data + m->pos, want);
    m->pos += want;
    return want / (size ? size : 1);
}
int memSeek(void* datasource, ogg_int64_t offset, int whence) {
    MemSrc* m = (MemSrc*)datasource;
    ogg_int64_t base = 0;
    if (whence == SEEK_CUR) base = (ogg_int64_t)m->pos;
    else if (whence == SEEK_END) base = (ogg_int64_t)m->size;
    ogg_int64_t np = base + offset;
    if (np < 0) np = 0;
    if (np > (ogg_int64_t)m->size) np = (ogg_int64_t)m->size;
    m->pos = (size_t)np;
    return 0;
}
long memTell(void* datasource) {
    return (long)((MemSrc*)datasource)->pos;
}
int memClose(void*) { return 0; }

bool decodeOgg(const std::string& data, Sample& out, std::string* err) {
    MemSrc m{ (const unsigned char*)data.data(), data.size(), 0 };
    OggVorbis_File vf;
    ov_callbacks cb;
    cb.read_func = memRead;
    cb.seek_func = memSeek;
    cb.tell_func = memTell;
    cb.close_func = memClose;
    if (ov_open_callbacks(&m, &vf, nullptr, 0, cb) != 0) {
        if (err) *err = "not a valid ogg";
        return false;
    }
    vorbis_info* vi = ov_info(&vf, -1);
    int srcRate = vi ? vi->rate : 44100;
    int channels = vi ? vi->channels : 2;

    std::vector<float> l, r;
    const int kChunk = 4096;
    float** pcm = nullptr;
    long n = 0;
    while ((n = ov_read_float(&vf, &pcm, kChunk, nullptr)) > 0) {
        for (long i = 0; i < n; ++i) {
            float L = (channels >= 1) ? pcm[0][i] : 0.f;
            float R = (channels >= 2) ? pcm[1][i] : L;
            l.push_back(L);
            r.push_back(R);
        }
    }
    ov_clear(&vf);

    if (l.empty()) {
        if (err) *err = "empty ogg";
        return false;
    }
    out.sampleRate = srcRate;
    out.left = std::move(l);
    out.right = std::move(r);
    return true;
}

uint32_t rd32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

bool decodeWav(const std::string& data, Sample& out, std::string* err) {
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0) {
        if (err) *err = "not a wav";
        return false;
    }
    size_t pos = 12;
    uint16_t channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t dataPos = 0, dataLen = 0;
    while (pos + 8 <= data.size()) {
        std::string id(data.data() + pos, 4);
        uint32_t sz = rd32((const unsigned char*)data.data() + pos + 4);
        if (id == "fmt ") {
            channels = rd16((const unsigned char*)data.data() + pos + 10);
            rate = rd32((const unsigned char*)data.data() + pos + 12);
            bits = rd16((const unsigned char*)data.data() + pos + 22);
        } else if (id == "data") {
            dataPos = pos + 8;
            dataLen = sz;
        }
        pos += 8 + sz + (sz & 1);
        if (id == "data") break;
    }
    if (channels == 0 || rate == 0 || bits == 0 || dataPos == 0) {
        if (err) *err = "wav: missing fmt/data";
        return false;
    }
    const unsigned char* d = (const unsigned char*)data.data() + dataPos;
    size_t n = std::min(dataLen, data.size() - dataPos);
    size_t frameBytes = channels * (bits / 8);
    size_t frames = n / frameBytes;
    if (frames == 0) { if (err) *err = "wav: no samples"; return false; }

    out.sampleRate = (int)rate;
    out.left.resize(frames);
    out.right.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const unsigned char* f = d + i * frameBytes;
        auto getSample = [&](size_t ch) -> float {
            if (ch >= channels) return 0.f;
            const unsigned char* s = f + ch * (bits / 8);
            if (bits == 8) return (s[0] - 128) / 128.0f;
            if (bits == 16) {
                int16_t v = (int16_t)rd16(s);
                return v / 32768.0f;
            }
            if (bits == 24) {
                int32_t v = (s[0]) | (s[1] << 8) | ((int32_t)(s[2] & 0x7F) << 16);
                if (s[2] & 0x80) v |= 0xFF800000;
                return v / 8388608.0f;
            }
            if (bits == 32) {
                // may be float or int; treat as float for now
                float v;
                std::memcpy(&v, s, 4);
                return v;
            }
            return 0.f;
        };
        out.left[i] = getSample(0);
        out.right[i] = getSample(1);
    }
    return true;
}

// linear resample to target rate
void resampleTo(const Sample& in, int targetRate, std::vector<float>& l, std::vector<float>& r) {
    if (in.sampleRate == targetRate) {
        l = in.left;
        r = in.right;
        return;
    }
    size_t n = in.left.size();
    if (n == 0) return;
    double step = (double)in.sampleRate / (double)targetRate;
    size_t outN = (size_t)std::ceil(n / step);
    l.resize(outN);
    r.resize(outN);
    for (size_t i = 0; i < outN; ++i) {
        double src = i * step;
        size_t i0 = (size_t)src;
        size_t i1 = i0 + 1 < n ? i0 + 1 : i0;
        double frac = src - i0;
        l[i] = (float)(in.left[i0] * (1 - frac) + in.left[i1] * frac);
        r[i] = (float)(in.right[i0] * (1 - frac) + in.right[i1] * frac);
    }
}

} // namespace

namespace {
// Load and decode one file; returns false (without clobbering out) on failure.
bool loadOne(const std::string& path, Sample& out) {
    std::string data = util::readFile(path);
    if (data.empty()) return false;
    std::string low = util::toLower(path);
    bool isOgg = low.size() > 4 && low.compare(low.size() - 4, 4, ".ogg") == 0;
    if (isOgg) return decodeOgg(data, out, nullptr);
    return decodeWav(data, out, nullptr);
}
} // namespace

bool loadSample(const std::string& path, Sample& out, std::string* err) {
    // Exact file first, then fall back to the sibling extension. Many PMS
    // files reference "#WAVxx foo.wav" while the actual files are foo.ogg.
    if (loadOne(path, out)) return true;

    std::string low = util::toLower(path);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, ".wav") == 0) {
        std::string alt = path;
        alt.replace(alt.size() - 4, 4, ".ogg");
        if (loadOne(alt, out)) return true;
    } else if (low.size() > 4 && low.compare(low.size() - 4, 4, ".ogg") == 0) {
        std::string alt = path;
        alt.replace(alt.size() - 4, 4, ".wav");
        if (loadOne(alt, out)) return true;
    }
    if (err) *err = "cannot read " + path;
    return false;
}

// ---------------- rendering ----------------

std::vector<float> renderStereo(const Chart& c, const RenderOptions& opt,
                                int& outSampleRate, int& outFrames,
                                std::string* err) {
    outSampleRate = opt.sampleRate > 0 ? opt.sampleRate : 44100;
    Timing timing(c);
    auto events = collectAudioEvents(c, timing);

    // Load and cache samples
    std::map<std::string, std::pair<Sample, bool>> cache;
    std::map<std::string, std::string> dirs; // unused
    for (const auto& e : events) {
        auto it = c.wavs.find(e.wav);
        if (it == c.wavs.end()) continue;
        if (cache.count(e.wav)) continue;
        Sample s;
        std::string loadErr;
        if (loadSample(it->second.path, s, &loadErr)) {
            cache[e.wav] = { std::move(s), true };
        } else {
            cache[e.wav] = { Sample(), false };
        }
    }

    // Determine total length. bmx2wav keeps the whole tail of every sample,
    // so the render must run until the *end of the last bar* plus the longest
    // triggered sample tail (a fixed +1s clip would cut e.g. a 13s fade).
    double duration = timing.measureToTime(timing.endMeasure() + 1.0); // end of last bar
    for (const auto& e : events) {
        double eEnd = e.time;
        auto cit = cache.find(e.wav);
        if (cit != cache.end() && cit->second.second) {
            const Sample& s = cit->second.first;
            if (s.sampleRate > 0 && !s.left.empty())
                eEnd += (double)s.left.size() / (double)s.sampleRate;
        }
        duration = std::max(duration, eEnd);
    }
    // small safety margin so the very last sample is not cut off
    duration += 0.05;
    size_t totalFrames = (size_t)(duration * opt.sampleRate) + 1;

    std::vector<float> l(totalFrames, 0.f), r(totalFrames, 0.f);

    double master = opt.masterVolume > 0 ? opt.masterVolume : 1.0;

    for (const auto& e : events) {
        auto cit = cache.find(e.wav);
        if (cit == cache.end() || !cit->second.second) continue;
        const Sample& s = cit->second.first;
        std::vector<float> sl, sr;
        resampleTo(s, opt.sampleRate, sl, sr);
        size_t start = (size_t)(e.time * opt.sampleRate);
        double vol = master;
        auto wit = c.wavs.find(e.wav);
        if (wit != c.wavs.end()) vol *= wit->second.volume;
        double pan = 0.0;
        if (wit != c.wavs.end()) pan = wit->second.pan;
        if (pan > 1.0) pan = 1.0;
        if (pan < -1.0) pan = -1.0;
        float lg = (float)vol, rg = (float)vol;
        if (pan < 0) rg = (float)(vol * (1.0 + pan));
        else if (pan > 0) lg = (float)(vol * (1.0 - pan));
        for (size_t i = 0; i < sl.size() && start + i < totalFrames; ++i) {
            l[start + i] += sl[i] * lg;
            r[start + i] += sr[i] * rg;
        }
    }

    // Normalize.
    if (opt.normalize) {
        float peak = 0.f;
        for (size_t i = 0; i < totalFrames; ++i) {
            peak = std::max(peak, std::fabs(l[i]));
            peak = std::max(peak, std::fabs(r[i]));
        }
        if (peak > 0.00001f) {
            // bmx2wav PeakNormalize goes to full scale (1.0).
            float g = 1.0f / peak;
            for (size_t i = 0; i < totalFrames; ++i) { l[i] *= g; r[i] *= g; }
        }
    }

    // Interleave stereo.
    outFrames = (int)totalFrames;
    std::vector<float> interleaved;
    interleaved.reserve(totalFrames * 2);
    for (size_t i = 0; i < totalFrames; ++i) {
        interleaved.push_back(l[i]);
        interleaved.push_back(r[i]);
    }
    (void)err;
    return interleaved;
}

std::string hex2ch(int v) {
    static const char* hx = "0123456789ABCDEF";
    std::string s;
    s += hx[(v >> 4) & 0xF];
    s += hx[v & 0xF];
    return s;
}

} // namespace pms