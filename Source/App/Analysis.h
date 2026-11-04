#pragma once
// ARCO 1.2: listening to audio. Tempo, beat and downbeat, key, onsets - and high-quality time-stretch / pitch-shift
// (Signalsmith Stretch). Used by the smart file import (match the piece) and by AUTO MIX.
#include <JuceHeader.h>
#include <vector>

namespace arco {
namespace analysis {

struct Tempo {
    double bpm = 0;            // 0 = no clear beat
    double confidence = 0;     // 0..1
    double beatOffset = 0;     // samples: the first beat
    double downbeatOffset = 0; // samples: the first bar line
    bool wholeBars = false;    // the file length is a whole number of bars at this tempo (a loop)
    int bars = 0;
};
struct Key {
    int tonic = -1;            // 0 = C ... 11 = B, -1 = no clear key
    bool minor = false;
    double confidence = 0;     // 0..1
    int majorEquivalent() const { return tonic < 0 ? -1 : (minor ? (tonic + 3) % 12 : tonic); }
    juce::String name() const;
};
struct Onset { double pos; float strength; };

// the onset strength envelope (spectral flux), one value per hop
struct Envelope { std::vector<float> v, low; double hop = 0, sr = 0; };
Envelope onsetEnvelope(const float* x, int n, double sr);

Tempo tempo(const float* x, int n, double sr, int beatsPerBar = 4, double preferBpm = 0);
Key key(const float* x, int n, double sr);
std::vector<Onset> onsets(const float* x, int n, double sr, float sensitivity = 1.0f, bool loop = false);

// time-stretch (outLength samples) and transpose (semitones), offline, best quality. Mono or stereo.
juce::AudioBuffer<float> stretch(const juce::AudioBuffer<float>& in, double sr, int outLength, double semitones);
// a high-quality sample-rate change
juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& in, double from, double to);

const char* noteName(int pc);

// the average spectrum in 1/3-octave bands, 25 Hz ... 16 kHz (29 bands), in dB (power)
constexpr int NBANDS = 29;
double bandFreq(int b);                       // centre frequency of band b
std::vector<double> bands(const float* x, int n, double sr);
// the exact start of the attack near an onset (searches +-15 ms)
double attackStart(const float* x, int n, double sr, double around);
}

// ------------------------------------------------------------------ the smart import: keep a file as it is, or fit it to the piece
struct PieceInfo {
    bool hasLoops = false;        // anything recorded in any scene
    double bpm = 120;             // the piece tempo
    int beats = 4;                // beats per bar
    double beatLen = 0;           // samples at the engine rate
    long loopLen = 0;             // the current scene's loop (0 = empty)
    analysis::Key key;            // the piece key (from its loops, or HALO)
    double sr = 48000;
    int scene = 1;
};
struct ImportPlan {
    juce::File file;
    double fileSr = 0; double seconds = 0;
    juce::AudioBuffer<float> audio;          // the file, at the engine rate
    analysis::Tempo tempo;                   // the file's
    analysis::Key key;
    double fileBpm = 0;                      // can be corrected by the player (half / double / typed)
    bool matchTempo = true, matchKey = true;
    int semitones = 0;                       // the key move
    int outBars = 0;                         // after stretching
    bool cut = false;                        // the file was longer than a track (only its first 42 s are used)
    juce::String summary() const;
    juce::String fitText(const struct PieceInfo& piece) const;
};
// analyse the file (on any thread)
bool prepareImport(const juce::File& f, double engineSr, ImportPlan& plan, juce::AudioFormatManager& fm, juce::String& error, int beatsPerBar = 4);
// the key move and the bar count, from the current choices
void planImport(ImportPlan& plan, const PieceInfo& piece);
// make the fitted audio: from the first bar line, stretched + transposed, exactly whole bars (tiled to the loop if it fits)
juce::AudioBuffer<float> renderImport(const ImportPlan& plan, const PieceInfo& piece, std::function<bool(float)> progress = nullptr);

} // namespace arco
