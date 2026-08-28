#pragma once
// pms.h - PMS/BMS parser, timing calculation and audio rendering.
#include <map>
#include <string>
#include <vector>

namespace pms {

struct SampleDef {
    std::string path;      // UTF-8, relative to the chart folder
    double volume = 1.0;
    double pan = 0.0;      // -1 .. 1
};

struct Chart {
    // Header metadata
    std::string title, subtitle, artist, subArtist, genre, maker, comment;
    std::string stageFile, banner, background;
    int difficulty = 0;
    int playlevel = 0;
    int player = 1;        // #PLAYER 1=single 2=couple 3=double 4=battle
    double defaultBpm = 120;
    int trackKey = 9;
    std::string trackType = "pms";

    // Definitions
    std::map<std::string, SampleDef> wavs;   // 2-char hex key
    std::map<std::string, double>     bpms;  // 2-char hex key -> BPM
    std::map<std::string, double>     stops; // 2-char hex key -> stop length (beats)
    std::string lnoBJ;                      // #LNOBJ long-note marker token

    // Per-channel event. For most channels each token is one event.
    struct Event {
        double measure = 0;      // possibly fractional
        std::string channel;     // 2-char hex
        std::string token;       // single 2-char token (or raw value for ch02)
    };
    std::vector<Event> events;

    // Time signature changes: value is beats per measure (e.g. 4, 3).
    struct TimeSig { double measure = 0; double bpm = 0; };
    std::vector<TimeSig> timeSigs;
};

// Parse a .pms file.
Chart parseFile(const std::string& path);
Chart parseText(const std::string& text, const std::string& fileName);

// Determine key channel -> (track index, player). Returns false if not a key channel.
bool keyTrackFor(const Chart& c, const std::string& channel, int& track, int& player);

// Internal: int -> 2-char uppercase hex channel string.
std::string hex2ch(int v);

// A note object used for both osu building and audio triggering.
struct NoteEvent {
    double time = 0;          // seconds
    double measure = 0;
    int track = 0;            // 0-based
    int player = 0;
    std::string wav;
    bool isLong = false;
    double endTime = 0;       // for long notes
};

// Build timing: converts measure -> seconds, handles BPM and stops.
class Timing {
public:
    struct BpmPoint {
        double timeSec = 0;   // seconds when this BPM takes effect
        double bpm = 120;
    };

    Timing() = default;
    explicit Timing(const Chart& c);
    double measureToTime(double measure) const;
    double mainBpm() const { return bpm_; }
    double mainMeter() const { return meter_; }
    double endMeasure() const { return endMeasure_; }
    const std::vector<BpmPoint>& bpmPoints() const { return bpmPoints_; }

private:
    struct Segment {
        double from = 0, to = 0;
        double secPerMeasure = 0;
        double stopSec = 0;
        double timeAtFrom = 0;
    };
    std::vector<Segment> segs_;
    std::vector<BpmPoint> bpmPoints_;
    double bpm_ = 120;
    double meter_ = 4;
    double endMeasure_ = 0;
};

// Collect playable notes (long paired when possible).
std::vector<NoteEvent> collectNotes(const Chart& c, const Timing& timing);

// Collect every sample trigger (BGM + key sounds) for audio rendering.
std::vector<NoteEvent> collectAudioEvents(const Chart& c, const Timing& timing);

// Decoded sample.
struct Sample {
    int sampleRate = 0;
    std::vector<float> left, right;   // length in frames
};

// Load a wav/ogg sample from an absolute path.
bool loadSample(const std::string& path, Sample& out, std::string* err);

// Render the whole chart to 44.1k stereo PCM.
struct RenderOptions {
    int sampleRate = 44100;
    bool normalize = true;
    double masterVolume = 1.0;
};

std::vector<float> renderStereo(const Chart& c, const RenderOptions& opt,
                                int& outSampleRate, int& outFrames,
                                std::string* err);

} // namespace pms