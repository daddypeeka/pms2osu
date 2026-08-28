#pragma once
// osu.h - osu!mania .osu file builder.
#include <string>

#include "pms.h"

namespace osu {

struct Options {
    double od = 7.5;
    double hp = 7.5;
    double ar = 5.0;
    std::string creator = "PMS";
    std::string title, titleUnicode, artist, artistUnicode;
    std::string version;   // empty -> auto from genre/difficulty/playlevel
    std::string source, tags;
};

// Build a complete .osu file. If a metadata field is empty the parsed
// chart value is used.
std::string buildOsuText(const pms::Chart& chart, const pms::Timing& timing,
                         const Options& o);

} // namespace osu