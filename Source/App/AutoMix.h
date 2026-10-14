#pragma once
// ARCO 1.2: AUTO MIX. Listens to every recorded track (and what the live inputs recorded), understands what each one is
// (bass, drums, lead, pad, texture), and mixes the piece: levels, high-pass and EQ against mud / harshness / masking,
// compression, panning, effect sends that suit each part, effect speeds snapped to the tempo, gentle timing correction
// towards the beat (keeping the groove), and a master level ready for streaming. Everything can be compared (A/B) and undone.
#include <JuceHeader.h>
#include "Render.h"

namespace arco {

class AudioHost;

struct AutoMixOptions {
    bool levels = true, eq = true, dynamics = true, pan = true, effects = true, timing = true, master = true;
    float timingAmount = 0.3f;       // 0..1 of the way to the grid
    int style = 1;                   // 0 natural, 1 balanced, 2 punchy
    bool allScenes = false;
    // 1.5: what you want
    int focus = 0;                   // 0 TOGETHER (every voice heard), 1 MELODY IN FRONT, 2 GROOVE (bass and drums carry)
    int space = 1;                   // 0 DRY, 1 ROOM, 2 HALL
    int tone = 0;                    // -1 warmer, 0, +1 brighter
    int loud = 0;                    // -1 quieter master, 0, +1 louder
    int width = 0;                   // -1 narrower, 0, +1 wider
    std::array<float, 8> trackDb {}; // your nudges per track (dB)
    std::array<float, 7> roleDb {};  // and per role (bass, drums, rhythm, melody, second melody, chords, texture)
};
// "the cello louder, warm and spacious, less reverb": what it understood is listed in 'understood'
void parseWish(const juce::String& wish, AutoMixOptions& o, juce::StringArray& understood);

struct AutoMixJob {
    AutoMixOptions opt;
    double sr = 48000, beatLen = 24000; int beats = 4;
    struct Scene { int index = 1; long len = 0; int mask = 0, speeds = 0; std::array<std::vector<float>, 8> audio; };
    std::vector<Scene> scenes;
    std::map<std::string, double> songVals;                    // song settings now
    std::array<std::map<std::string, double>, 4> sceneVals;    // scene settings now
    juce::StringArray visible;                                  // the effect cards on the main window
    int src[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };                   // which LIVE input recorded each track (per the current scene)
    int nLive = 1;
    RenderSnapshot snap;                                        // for measuring the master
    AudioHost* host = nullptr;
};

struct AutoMixResult {
    struct Change { int scene; std::string id; double before, after; };      // scene 0 = a song setting
    struct AudioEdit { int scene, track; std::vector<float> before, after; };
    std::vector<Change> changes;
    std::vector<AudioEdit> audio;
    struct Line { juce::String who, role, what; int strip = -1; int scene = 1; };
    // the balance: how loud each part sounds while it plays (LU, the loudest = 0), before and after
    struct Level { int scene = 1, track = 0; juce::String role; double before = 0, after = 0; };
    std::vector<Level> levels;
    juce::StringArray understood;           // what it understood of your wish
    std::vector<Line> report;
    juce::String headline;
    double lufsBefore = -70, lufsAfter = -70;
    bool ok = false;
    juce::String error;
};

namespace automix {
AutoMixJob prepare(AudioHost& h, const AutoMixOptions& o);                 // message thread
AutoMixResult run(AutoMixJob& job, std::function<bool(float)> progress);   // any thread
void apply(AudioHost& h, const AutoMixResult& r, bool after);              // message thread: after = the mix, false = as before
// the timing correction on its own (also used by STUDIO): returns how many notes moved and the average move (ms)
std::vector<float> tighten(const std::vector<float>& x, long len, double sr, double beatLen, float amount, int& moved, double& avgMs);
}

} // namespace arco
