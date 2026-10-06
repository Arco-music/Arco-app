#pragma once
// ARCO 1.2: STUDIO - the recorded loops laid out as a song (scene after scene), to edit and mix like in a DAW:
// clips (move, trim, split, duplicate, fade, gain), lanes (volume, pan, mute, solo, EQ, compressor), master, playback,
// export at full quality. The same code plays it and exports it, so what you hear is what you get.
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "../Engine/StripMix.h"
#include "Render.h"

namespace arco {
class AudioHost;

struct StudioSource { juce::String name; std::vector<float> l, r; long size() const { return (long) l.size(); } };   // r empty = mono; at the studio rate
struct StudioClip {
    int lane = 0, source = 0;
    long start = 0, length = 0;          // on the timeline (samples)
    long offset = 0;                     // where in the source it starts
    bool loop = true;                    // the source repeats inside the clip
    float gainDb = 0, fadeIn = 0, fadeOut = 0;   // fades in samples
};
struct StudioLane {
    juce::String name; int colour = 4;
    float vol = 0, pan = 0;              // dB, -1..1
    bool mute = false, solo = false;
    float lo = 0, mid = 0, hi = 0, midHz = 1000, comp = 0, hp = 0;   // dB / Hz / 0..100 / Hz
};
struct StudioSong {
    double sr = 48000, bpm = 120; int beats = 4;
    std::vector<StudioSource> sources;
    std::vector<StudioClip> clips;
    std::vector<StudioLane> lanes;
    float masterDb = 0, glue = 20;
    long length() const { long l = 0; for (auto& c : clips) l = std::max(l, c.start + c.length); return l; }
    double barLen() const { return 60.0 / bpm * beats * sr; }
};

class Studio {
public:
    explicit Studio(AudioHost& h) : host(h) {}
    AudioHost& host;
    StudioSong song;
    juce::CriticalSection lock;                  // the audio thread renders under it (try-lock)
    // build the song from the station: every scene with loops, `repeats` times each, in order A-D
    bool buildFromStation(int repeats, juce::String& why);
    // play
    std::atomic<bool> playing { false };
    std::atomic<long> pos { 0 };
    void renderBlock(float* l, float* r, int n);           // audio thread: adds into l / r
    // export (any thread): the whole song, mixed
    bool renderAll(juce::AudioBuffer<float>& out, std::function<bool(float)> progress);
    // undo
    void snapshot();
    bool undo();
    bool redo();
    // save / open next to the song
    bool save(const juce::File& folder);
    bool load(const juce::File& folder);
    // effects printed: a lane replaced by its sound through the station's effects
    bool printEffects(int lane, std::function<bool(float)> progress);
    std::vector<int> laneStrip;                  // which station strip each lane came from (4.. = track)
    std::vector<int> laneScene;
private:
    std::vector<StudioSong> undoStack, redoStack;
    struct LaneDsp { Biquad f[2][5]; float env = 0, gSm = 1; StudioLane cfg; bool designed = false; };
    std::vector<LaneDsp> dsp;
    float glueEnv = 0, glueG = 1;
    static void mixRange(const StudioSong& song, float* l, float* r, long start, int n, std::vector<LaneDsp>& d, float& gEnv, float& gG);
    static void design(LaneDsp& d, const StudioLane& L, double sr);
};

} // namespace arco
