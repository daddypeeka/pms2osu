#pragma once
// ogg.h - minimal Ogg Vorbis encoder wrapper (file or in-memory output).
#include <string>
#include <vector>

namespace vorbis_wrap {

// Encode interleaved float samples (-1..1) to an Ogg Vorbis file using a
// *managed* bitrate (default 192 kbps). Pass bitrateBps <= 0 to fall back to a
// VBR quality encode. Returns true on success.
bool writeOgg(const std::string& path,
              const std::vector<float>& interleaved,
              int sampleRate, int channels, int nFrames,
              int bitrateBps,   // e.g. 192000, <= 0 => quality mode
              float quality,    // -0.1 .. 1.0, used only if bitrateBps <= 0
              std::string* err);

// Same as writeOgg but writes straight into an in-memory std::string, avoiding
// the temp-file round-trip (the .osz pipeline uses this). On success *out is
// filled with the complete encoded stream and true is returned.
bool writeOggMem(const std::vector<float>& interleaved,
                 int sampleRate, int channels, int nFrames,
                 int bitrateBps, float quality,
                 std::string* out, std::string* err);

} // namespace vorbis_wrap
