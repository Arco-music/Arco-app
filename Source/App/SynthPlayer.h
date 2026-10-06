#pragma once
// Plays the SYNTH's 8 voices from notes (on-screen keyboard, computer keys, MIDI keyboard).
// Each voice has a note (sn), a velocity (sv, 0 = released) and a trigger counter (sg: a new number starts the note).
// Message thread only (the engine's command queue has one writer).
#include <JuceHeader.h>
#include <vector>
#include "../Engine/Engine.h"

namespace arco {

class SynthPlayer {
public:
    explicit SynthPlayer(Engine& e) : eng(e) {}
    std::function<void()> onFirstNote;            // e.g. send the synth into LIVE 2 when nobody chose where it plays

    void noteOn(int note, float vel01) {
        if (onFirstNote) onFirstNote();
        int vel = juce::jlimit(1, 127, (int) std::round(vel01 * 127.0f));
        if (mono()) {
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            bool legato = !held.empty();
            held.push_back(note);
            setVoice(0, note, vel, !legato || glide() < 0.5);   // with GLIDE, notes tied together slide instead of starting again
            return;
        }
        int v = -1;
        for (int k = 0; k < NV; ++k) if (voice[k].note == note && voice[k].vel > 0) v = k;   // same note again
        if (v < 0) {                                                                         // a free voice, longest released
            long best = -1;
            for (int k = 0; k < NV; ++k) if (voice[k].vel == 0 && (best < 0 || voice[k].age < voice[best].age)) best = k;
            v = (int) best;
        }
        if (v < 0) {                                                                         // all busy: the oldest one
            long best = 0; for (int k = 1; k < NV; ++k) if (voice[k].age < voice[best].age) best = k;
            v = (int) best;
        }
        setVoice(v, note, vel, true);
    }
    void noteOff(int note) {
        if (sustain) { sustained.push_back(note); return; }
        if (mono()) {
            held.erase(std::remove(held.begin(), held.end(), note), held.end());
            if (held.empty()) release(0);
            else if (voice[0].note == note) setVoice(0, held.back(), voice[0].vel, false);
            return;
        }
        for (int k = 0; k < NV; ++k) if (voice[k].note == note && voice[k].vel > 0) release(k);
    }
    void setSustain(bool on) {
        sustain = on;
        if (!on) { auto s = sustained; sustained.clear(); for (int n : s) noteOff(n); }
    }
    void pitchBend(float semis) {                // -2 .. +2: every sounding voice bends
        bend = semis;
        for (int k = 0; k < NV; ++k) if (voice[k].vel > 0) eng.set("st_syn", "sn" + std::to_string(k), voice[k].note + bend);
    }
    // the slide pad: one voice whose pitch glides freely (a float note), louder or softer while it sounds
    void padOn(float pitch, float vel01) {
        if (onFirstNote) onFirstNote();
        padOff();
        long best = -1; for (int k = 0; k < NV; ++k) if (voice[k].vel == 0 && (best < 0 || voice[k].age < voice[best].age)) best = k;
        padVoice = best < 0 ? 0 : (int) best;
        auto& v = voice[padVoice]; v.note = (int) std::round(pitch); v.vel = juce::jlimit(1, 127, (int) std::round(vel01 * 127)); v.age = ++clock;
        eng.set("st_syn", "sn" + std::to_string(padVoice), pitch);
        eng.set("st_syn", "sv" + std::to_string(padVoice), v.vel);
        eng.set("st_syn", "sg" + std::to_string(padVoice), ++v.trig);
    }
    void padMove(float pitch, float expr01) {
        if (padVoice < 0) return;
        eng.set("st_syn", "sn" + std::to_string(padVoice), pitch);
        setExpression(expr01);
    }
    void padOff() { if (padVoice >= 0) { release(padVoice); padVoice = -1; } }
    void setExpression(float v01) { expr = v01; eng.set("st_syn", "expr", v01); }   // the slide pad's up / down (volume while playing)
    float expression() const { return expr; }
    void allOff() {
        held.clear(); sustained.clear(); sustain = false;
        for (int k = 0; k < NV; ++k) release(k);
    }
    int sounding() const { int c = 0; for (auto& v : voice) c += v.vel > 0; return c; }
    static constexpr int NV = 8;
private:
    struct Voice { int note = 60, vel = 0, trig = 0; long age = 0; };
    Voice voice[NV];
    long clock = 0;
    std::vector<int> held, sustained;
    bool sustain = false;
    float bend = 0, expr = 1;
    int padVoice = -1;
    Engine& eng;
    bool mono() const { return paramValue("mono") > 0.5; }
    double glide() const { return paramValue("s_glide"); }
    std::function<double(const char*)> param;
public:
    void setParamSource(std::function<double(const char*)> p) { param = std::move(p); }
private:
    double paramValue(const char* n) const { return param ? param(n) : 0.0; }
    void setVoice(int k, int note, int vel, bool retrigger) {
        auto& v = voice[k];
        v.note = note; v.vel = vel; v.age = ++clock;
        eng.set("st_syn", "sn" + std::to_string(k), note + bend);
        eng.set("st_syn", "sv" + std::to_string(k), vel);
        if (retrigger) eng.set("st_syn", "sg" + std::to_string(k), ++v.trig);
    }
    void release(int k) {
        if (voice[k].vel == 0) return;
        voice[k].vel = 0; voice[k].age = ++clock;
        eng.set("st_syn", "sv" + std::to_string(k), 0);
    }
};

} // namespace arco
