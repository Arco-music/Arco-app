#pragma once
// SCORE - a small demo piece (for the first look, the self-test and the screenshots):
// drums, a bass, chords on NOVA, a recorded-like sound, three stations with different sounds.
#include "Host.h"

namespace score {

inline void makeDemo(Host& h) {
    h.newProject(false);
    auto& p = *h.project;
    p.bpm = 118; p.key = 9; p.scale = 2;                       // A minor
    auto addNotes = [](Clip& c, std::initializer_list<std::array<double, 4>> ns) { for (auto& n : ns) { Note x; x.start = n[0]; x.len = n[1]; x.pitch = (int) n[2]; x.vel = (float) n[3]; c.notes.push_back(x); } };
    // drums
    auto dr = p.newTrack(TrackKind::Instrument, "Beat", "drm");
    for (int bar = 0; bar < 16; bar += 4) {
        auto c = p.newMidiClip(bar * 4.0, 16); c->name = "Beat";
        for (int b = 0; b < 4; ++b) {
            double o = b * 4.0;
            addNotes(*c, { { o + 0, 0.2, 36, 1.0 }, { o + 2, 0.2, 36, 0.9 }, { o + 2.75, 0.2, 36, 0.6 }, { o + 1, 0.2, 37, 0.95 }, { o + 3, 0.2, 37, 0.95 } });
            for (int k = 0; k < 8; ++k) addNotes(*c, { { o + k * 0.5, 0.1, 39, k % 2 ? 0.55 : 0.8 } });
        }
        dr->clips.push_back(c);
    }
    p.tracks.push_back(dr);
    // bass
    auto bass = p.newTrack(TrackKind::Instrument, "Bass", "nova");
    if (auto* n = dynamic_cast<Nova*>(bass->inst.get())) for (auto& pr : novaPresets()) if (pr.name == "Pluck Bass") applyNovaPreset(*n, pr);
    for (int bar = 0; bar < 16; bar += 4) {
        auto c = p.newMidiClip(bar * 4.0, 16); c->name = "Bass line";
        int roots[4] = { 45, 41, 48, 43 };
        for (int b = 0; b < 4; ++b) for (int k = 0; k < 4; ++k) addNotes(*c, { { b * 4.0 + k, k == 3 ? 0.4 : 0.8, (double) (roots[b] - 12 + (k == 2 ? 12 : 0)), 0.85 } });
        bass->clips.push_back(c);
    }
    p.tracks.push_back(bass);
    // chords
    auto keys = p.newTrack(TrackKind::Instrument, "Chords", "nova");
    if (auto* n = dynamic_cast<Nova*>(keys->inst.get())) for (auto& pr : novaPresets()) if (pr.name == "Halo Pad") applyNovaPreset(*n, pr);
    {
        auto c = p.newMidiClip(16, 32); c->name = "Chords";
        int ch[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
        for (int rep = 0; rep < 2; ++rep) for (int b = 0; b < 4; ++b) for (int k = 0; k < 3; ++k) addNotes(*c, { { rep * 16.0 + b * 4.0, 3.8, (double) ch[b][k], 0.7 } });
        keys->clips.push_back(c);
    }
    keys->fx.push_back(std::shared_ptr<Effect>(makeEffect("dly")));
    keys->fx.push_back(std::shared_ptr<Effect>(makeEffect("cho")));
    p.tracks.push_back(keys);
    // a "recording": a cello-like glide made here, so the demo needs no files
    {
        const double sr = h.engine.sr; int n = (int) (sr * 6.0);
        std::vector<float> L((size_t) n), R((size_t) n);
        double ph = 0;
        for (int i = 0; i < n; ++i) {
            double t = i / sr, f = 110.0 * std::pow(2.0, (t < 3 ? 0.0 : 7.0 / 12.0)) * (1 + 0.004 * std::sin(t * 5.5 * 6.283));
            ph += f / sr; double v = 0;
            for (int k = 1; k < 12; ++k) v += std::sin(ph * 6.283 * k) / (k * 1.2);
            double env = std::min(1.0, t / 0.4) * std::min(1.0, (6.0 - t) / 0.8) * (0.75 + 0.25 * std::sin(t * 1.3));
            L[(size_t) i] = (float) (v * env * 0.22); R[(size_t) i] = L[(size_t) i];
        }
        auto src = h.writeSource("Cello take", L, R);
        auto au = p.newTrack(TrackKind::Audio, "Cello");
        au->inType = InputType::Mono;
        if (src) { auto c = p.newAudioClip(src, 32); c->fadeIn = 0.3; c->fadeOut = 0.8; au->clips.push_back(c);
                   auto c2 = p.newAudioClip(src, 48); c2->fx.push_back(std::shared_ptr<Effect>(makeEffect("halo"))); au->clips.push_back(c2); }
        au->fx.push_back(std::shared_ptr<Effect>(makeEffect("rev")));
        au->mix->find("sa")->set(-12);
        p.tracks.push_back(au);
    }
    // stations: the same tracks, three different sounds
    auto* chordsDelay = keys->fx[0].get();
    auto* cutoff = keys->inst->find("f1c");
    auto snap = [&](const juce::String& name, double beat, float glide, int atEnd) {
        Station s; s.id = newUid(); s.name = name; s.beat = beat; s.glide = glide; s.atEnd = atEnd; s.snap = p.captureKnobs();
        for (auto* t : p.allTracks()) for (auto& e : t->fx) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
        s.hasSnap = true; p.stations.push_back(s); };
    chordsDelay->enabled.store(false); cutoff->set(900);
    snap("Groove", 0, 0.0f, 0);
    chordsDelay->enabled.store(true); cutoff->set(3500);
    snap("Open up", 16, 4.0f, 1);
    cutoff->set(6000); chordsDelay->find("fb")->set(70);
    snap("Cello and echoes", 32, 6.0f, 0);
    // back to the first station's sound
    chordsDelay->enabled.store(false); cutoff->set(900); chordsDelay->find("fb")->set(40);
    p.loopOn = false; p.loopStart = 0; p.loopEnd = 16;
    h.selTrack = keys->id; h.engine.keysTarget.store(keys->id);
    h.stations.current = 0;
    h.rebuild(); ++h.structure; h.dirty = false;
    h.say(juce::String::fromUTF8("the demo piece · press play, or open the SCORE LINE and press GO"));
}

} // namespace score
