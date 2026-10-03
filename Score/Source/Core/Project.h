#pragma once
// SCORE - the piece: tracks with clips (audio or notes), effects, automation, and the stations of the score line.
#include <JuceHeader.h>
#include <map>
#include <memory>
#include <vector>
#include "Instruments.h"

namespace score {

using Uid = uint32_t;

// ---------------------------------------------------------------- a sound file in memory (already at the engine's sample rate)
struct AudioSource {
    juce::String file;                 // where it lives (inside the project folder: "Audio/name.wav")
    juce::String name;
    juce::AudioBuffer<float> buf;      // always 2 channels
    double sr = 48000.0;               // = the engine rate it was converted to
    double fileRate = 48000.0;         // the file's own rate
    std::vector<float> peaks;          // max |x| per 64 samples (for drawing)
    int length() const { return buf.getNumSamples(); }
    double seconds() const { return length() / sr; }
};
using SourcePtr = std::shared_ptr<const AudioSource>;

struct Note { double start = 0, len = 1; int pitch = 60; float vel = 0.8f; };

struct ClipRT { int64_t tail = 0; bool wasOn = false; };   // what the audio thread remembers about a clip (tails of its effects)

struct Clip {
    Uid id = 0;
    bool midi = false;
    juce::String name;
    juce::uint32 colour = 0;           // 0 = the track's colour
    bool muted = false;
    double start = 0;                  // beats
    double length = 4;                 // beats (for audio clips: kept in step with srcLen at the project's tempo)
    // audio
    juce::String file; SourcePtr src;
    double offset = 0;                 // seconds into the file
    double srcLen = 0;                 // seconds of the file that play
    float gainDb = 0;
    double fadeIn = 0, fadeOut = 0;    // seconds
    bool reverse = false;
    juce::String original;             // the file before a destructive process (REVERT TO ORIGINAL)
    // notes
    std::vector<Note> notes;
    // effects on this clip only
    std::vector<std::shared_ptr<Effect>> fx;
    std::shared_ptr<ClipRT> rt = std::make_shared<ClipRT>();
    double end() const { return start + length; }
};
using ClipPtr = std::shared_ptr<Clip>;

// ---------------------------------------------------------------- automation: a line of points for one knob
struct AutoPoint { double beat = 0; float value = 0; };   // value = the knob's 0..1 position
struct AutoLane { std::string path; std::vector<AutoPoint> pts; bool visible = true; float valueAt(double beat) const; };

// ---------------------------------------------------------------- a mixer channel's knobs (they can be automated / kept in stations)
class Mixer : public ParamOwner {
public:
    Mixer() {
        add("vol", "VOLUME", -60, 6, 0, Unit::dB); add("pan", "PAN", -50, 50, 0, Unit::Pan);
        add("sa", "SEND A", -60, 6, -60, Unit::dB); add("sb", "SEND B", -60, 6, -60, Unit::dB);
        addOnOff("mute", "MUTE");
        uid = newUid();
    }
    float vol() const { return params[0]->get(); }
};

enum class TrackKind { Audio, Instrument, Return, Master };
enum class InputType { None, Mono, Stereo, Track };

struct Track {
    Uid id = 0;
    TrackKind kind = TrackKind::Audio;
    juce::String name;
    juce::uint32 colour = 0xff8fa8d8;
    std::shared_ptr<Instrument> inst;
    std::vector<std::shared_ptr<Effect>> fx;
    std::shared_ptr<Mixer> mix = std::make_shared<Mixer>();
    bool solo = false, arm = false;
    int monitor = 1;                   // 0 off, 1 auto (when armed), 2 always
    InputType inType = InputType::Mono;
    int inCh = 0;                      // first device input (0-based)
    Uid inTrack = 0;                   // InputType::Track: record the output of this track
    int recMode = 0;                   // 0 = the dry sound, 1 = with this track's effects
    bool follow = false; int followOct = 0;     // an instrument that plays what the input sings
    std::vector<ClipPtr> clips;
    std::vector<AutoLane> autos;
    float height = 76;                 // in the arranger
    bool showAuto = false;
    juce::String autoShown;            // which lane is open
    bool collapsed = false;
    bool isInstrument() const { return kind == TrackKind::Instrument; }
};
using TrackPtr = std::shared_ptr<Track>;

// ---------------------------------------------------------------- a station of the score line
struct Station {
    Uid id = 0;
    juce::String name, note;
    double beat = 0;                   // where it starts in the piece
    float glide = 2.0f;                // seconds to move every knob to this station
    int curve = 1;                     // 0 linear 1 smooth 2 slow start 3 fast start
    int atEnd = 0;                     // 0 go on, 1 loop until GO, 2 wait (silence) until GO
    float autoGo = 0;                  // seconds after it starts (0 = wait for GO)
    int jump = 0;                      // GO jumps: 0 at once, 1 next beat, 2 next bar
    bool hasSnap = false;
    std::map<std::string, float> snap; // every knob of the piece (path -> value)
};

// ---------------------------------------------------------------- the piece
class Project {
public:
    Project();
    juce::String name = "Untitled";
    juce::File folder;                 // the project folder (name.score)
    double bpm = 110.0;
    int beatsPerBar = 4;
    int key = 0, scale = 0;            // for the piano roll's scale highlight
    bool loopOn = false; double loopStart = 0, loopEnd = 16;
    bool metro = false; float metroLevel = -12.0f; int countIn = 1;
    std::vector<TrackPtr> tracks;
    TrackPtr retA, retB, master;
    std::vector<Station> stations;
    double length() const;             // the end of the last clip / station (beats)

    // ---- finding things
    Track* track(Uid id) const;
    Clip* clip(Uid id, Track** owner = nullptr) const;
    std::vector<Track*> allTracks() const;                   // tracks + returns + master
    // every knob of the piece by its path "<uid>/<id>"
    std::map<std::string, Param*> paramMap() const;
    Param* param(const std::string& path) const;
    static std::string path(const ParamOwner& o, const Param& p) { return std::to_string(o.uid) + "/" + p.id; }
    juce::String describe(const std::string& path) const;   // "Track 2 · DELAY · FEEDBACK"

    // ---- making things
    TrackPtr newTrack(TrackKind k, const juce::String& name, const std::string& instType = {});
    static juce::uint32 colourFor(int i);
    ClipPtr newAudioClip(SourcePtr src, double startBeat);
    ClipPtr newMidiClip(double startBeat, double lenBeats);
    void syncAudioLengths();           // after a tempo change: audio clips keep their real length

    // ---- stations
    Station* stationAt(double beat);   // the station whose region contains beat
    int stationIndex(Uid id) const;
    void sortStations();
    std::map<std::string, float> captureKnobs() const;
    void applyKnobs(const std::map<std::string, float>& v) const;

    // ---- saving (JSON). Audio is not in the JSON, only the file names.
    juce::var toVar() const;
    // reads a piece; objects (effects, instruments, clips, sources) of `old` with the same ids are reused, so nothing restarts
    bool fromVar(const juce::var& v, const Project* old, std::function<SourcePtr(const juce::String&)> loadSource);
};

juce::var paramsToVar(const ParamOwner& o);
void paramsFromVar(ParamOwner& o, const juce::var& v);

} // namespace score
