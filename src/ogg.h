#pragma once
// ogg.h - minimal Ogg Vorbis encoder wrapper for the pms2ogg192 tool.
#include <string>
#include <vector>

namespace vorbis_wrap {

// Write interleaved stereo float samples (-1..1) to an Ogg Vorbis file
// using a *managed* bitrate (default 192 kbps). Pass bitrateBps <= 0 to
// fall back to a VBR quality encode.
bool writeOgg(const std::string& path,
              const std::vector<float>& interleaved,
              int sampleRate, int channels, int nFrames,
              int bitrateBps,   // e.g. 192000, <= 0 => quality mode
              float quality,    // -0.1 .. 1.0, used only if bitrateBps <= 0
              std::string* err);

} // namespace vorbis_wrap
