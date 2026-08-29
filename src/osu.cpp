#include "osu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "util.h"

namespace osu {

namespace {

std::string autoVersion(const pms::Chart& c) {
    if (!c.genre.empty()) return c.genre;
    if (c.difficulty > 0) {
        switch (c.difficulty) {
            case 1: return "Beginner";
            case 2: return "Normal";
            case 3: return "Hard";
            case 4: return "Expert";
            case 5: return "Special";
            default: return "D" + std::to_string(c.difficulty);
        }
    }
    if (c.playlevel > 0) return "LV." + std::to_string(c.playlevel);
    return std::string();
}

} // namespace

std::string buildOsuText(const pms::Chart& chart, const pms::Timing& timing,
                         const Options& o) {
    std::string title = o.title.empty() ? chart.title : o.title;
    std::string titleU = o.titleUnicode.empty() ? chart.title : o.titleUnicode;
    std::string artist = o.artist.empty() ? chart.artist : o.artist;
    std::string artistU = o.artistUnicode.empty() ? chart.artist : o.artistUnicode;
    std::string creator = o.creator.empty() ? "PMS" : o.creator;
    std::string version = o.version.empty() ? autoVersion(chart) : o.version;
    std::string source = o.source.empty() ? chart.maker : o.source;
    std::string tags = o.tags;

    int keyCount = chart.trackKey > 0 ? chart.trackKey : 9;
    double bpm = timing.mainBpm();
    if (bpm <= 0) bpm = chart.defaultBpm > 0 ? chart.defaultBpm : 120.0;

    std::string out;
    out.reserve(4096);

    // ---- General ----
    out += "osu file format v14\n\n";
    out += "[General]\n";
    out += "AudioFilename: audio.ogg\n";
    out += "AudioLeadIn: 0\n";
    out += "PreviewTime: -1\n";
    out += "Countdown: 0\n";
    out += "SampleSet: Soft\n";
    out += "StackLeniency: 0.7\n";
    out += "Mode: 3\n";
    out += "LetterboxInBreaks: 0\n";
    out += "SpecialStyle: 0\n";
    out += "WidescreenStoryboard: 0\n\n";

    // ---- Editor ----
    out += "[Editor]\n";
    out += "DistanceSpacing: 1\n";
    out += "BeatDivisor: 4\n";
    out += "GridSize: 4\n";
    out += "TimelineZoom: 1\n\n";

    // ---- Metadata ----
    out += "[Metadata]\n";
    out += "Title:" + title + "\n";
    out += "TitleUnicode:" + titleU + "\n";
    out += "Artist:" + artist + "\n";
    out += "ArtistUnicode:" + artistU + "\n";
    out += "Creator:" + creator + "\n";
    out += "Version:" + version + "\n";
    out += "Source:" + source + "\n";
    out += "Tags:" + tags + "\n";
    out += "BeatmapID:0\n";
    out += "BeatmapSetID:-1\n\n";

    // ---- Difficulty ----
    out += "[Difficulty]\n";
    out += "HPDrainRate:" + util::numStrG(o.hp) + "\n";
    out += "CircleSize:" + util::numStrG((double)keyCount) + "\n";
    out += "OverallDifficulty:" + util::numStrG(o.od) + "\n";
    out += "ApproachRate:" + util::numStrG(o.ar) + "\n";
    out += "SliderMultiplier:1.4\n";
    out += "SliderTickRate:1\n\n";

    // ---- Events ----
    out += "[Events]\n";
    out += "//Background and Video events\n";
    if (!chart.background.empty()) {
        out += "0,0,\"" + chart.background + "\",0,0\n";
    }
    out += "//Break Periods\n";
    out += "//Storyboard Layer 0 (Background)\n";
    out += "//Storyboard Layer 1 (Fail)\n";
    out += "//Storyboard Layer 2 (Pass)\n";
    out += "//Storyboard Layer 3 (Foreground)\n";
    out += "//Storyboard Layer 4 (Overlay)\n";
    out += "//Storyboard Sound Samples\n\n";

    // ---- TimingPoints (base point + BPM changes) ----
    out += "[TimingPoints]\n";
    const auto& pts = timing.bpmPoints();
    if (pts.empty()) {
        out += std::to_string(0) + "," + util::numStrG(60000.0 / bpm) + "," +
               util::numStrG(timing.mainMeter()) + ",1,0,40,1,0\n";
    } else {
        for (size_t k = 0; k < pts.size(); ++k) {
            long long t = (long long)std::llround(pts[k].timeSec * 1000.0);
            out += std::to_string(t) + "," + util::numStrG(60000.0 / pts[k].bpm) + "," +
                   util::numStrG(timing.mainMeter()) + ",1,0,40,1,0\n";
        }
    }
    out += "\n";

    // ---- HitObjects ----
    out += "[HitObjects]\n";
    double colWidth = 512.0 / keyCount;
    for (const auto& n : pms::collectNotes(chart, timing)) {
        int x = (int)std::llround((n.track + 0.5) * colWidth);
        if (n.track < 0 || n.track >= keyCount) continue;
        if (n.isLong && n.endTime > n.time) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%d,192,%lld,128,0,%lld:0:0:0:0:\n",
                          x, (long long)std::llround(n.time * 1000.0),
                          (long long)std::llround(n.endTime * 1000.0));
            out += buf;
        } else {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%d,192,%lld,1,0,0:0:0:0:\n",
                          x, (long long)std::llround(n.time * 1000.0));
            out += buf;
        }
    }
    return out;
}

} // namespace osu