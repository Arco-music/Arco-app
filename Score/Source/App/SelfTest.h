#pragma once
// SCORE --selftest: the whole app without a sound card: the demo piece plays, stations glide, recording works,
// the piece saves and opens again, undo, clip tools, export.
#include "Host.h"
#include "Demo.h"

namespace score {

inline int runSelfTest(Host& h) {
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); std::fflush(stdout); if (!ok) ++fails; };
    auto& e = h.engine;
    const int B = 256;
    std::vector<float> inL(B), inR(B), oL(B), oR(B);
    double ph = 0;
    auto run = [&](double seconds, bool tone) -> float {          // the "sound card": returns the loudest output
        float pk = 0; int blocks = (int) (seconds * e.sr / B);
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < B; ++i) { float v = tone ? 0.3f * (float) std::sin(ph) : 0.0f; ph += 2 * juce::MathConstants<double>::pi * 220.0 / e.sr; inL[(size_t) i] = v; inR[(size_t) i] = v; }
            const float* in[2] = { inL.data(), inR.data() }; float* out[2] = { oL.data(), oR.data() };
            e.process(in, 2, out, 2, B);
            for (int i = 0; i < B; ++i) pk = std::max(pk, std::max(std::abs(oL[(size_t) i]), std::abs(oR[(size_t) i])));
        }
        return pk;
    };

    // 1. the demo piece plays, every track makes sound
    makeDemo(h);
    auto& p = *h.project;
    check(p.tracks.size() == 4 && p.stations.size() == 3, "demo piece: 4 tracks, 3 stations");
    e.locate(0); e.play();
    float pk = run(2.0, false);
    check(pk > 0.02f && pk < 1.0f, "the demo plays (peak " + juce::String(pk, 3) + ")");
    check(e.position() > 3.0, "the playhead moves (" + juce::String(e.position(), 2) + " beats)");
    e.stop(); run(0.05, false);
    for (auto& t : p.tracks) {
        std::vector<float> L(4096), R(4096); float tp = 0;
        e.resetAll(); e.locate(t->clips.empty() ? 0 : t->clips.front()->start); e.play(); run(0.02, false);
        for (int k = 0; k < 40; ++k) { e.renderBlock(L.data(), R.data(), 4096, t->id); for (auto v : L) tp = std::max(tp, std::abs(v)); }
        e.stop(); run(0.02, false);
        check(tp > 0.005f, "track \"" + t->name + "\" sounds (peak " + juce::String(tp, 3) + ")");
    }

    // 2. stations: their knobs come back
    auto* chords = p.tracks[2].get(); auto* cut = chords->inst->find("f1c"); auto* dly = chords->fx[0].get();
    h.stations.perform = false;
    h.stations.select(1); h.stations.tick();
    check(std::abs(cut->get() - 3500) < 1 && dly->enabled.load(), "station 2: cutoff 3500 Hz, delay on (" + juce::String(cut->get(), 0) + ")");
    h.stations.select(0); h.stations.tick();
    check(std::abs(cut->get() - 900) < 1 && !dly->enabled.load(), "station 1: back to 900 Hz, delay off");
    // knobs turned in a station stay in that station
    h.stations.select(2); h.stations.tick();
    cut->set(1234); h.stations.storeQuiet();
    h.stations.select(0); h.stations.tick();
    h.stations.select(2); h.stations.tick();
    check(std::abs(cut->get() - 1234) < 1, "a knob turned in station 3 is kept there");
    // glide (PERFORM): halfway there after half the time
    h.stations.perform = true;
    p.stations[1].glide = 0.4f; p.stations[1].curve = 0;
    h.stations.select(0); h.stations.tick(); h.stations.perform = true;
    h.stations.goTo(1); juce::Thread::sleep(200); h.stations.tick();
    float mid = cut->get();
    check(mid > 1200 && mid < 3200, "a glide moves the knob gradually (" + juce::String(mid, 0) + " Hz at half time)");
    juce::Thread::sleep(260); h.stations.tick();
    check(std::abs(cut->get() - 3500) < 1, "and arrives");
    e.stop(); h.stations.stopTransportGuards(); h.stations.perform = false; run(0.05, false);

    // 3. recording from the input into the audio track
    Track* cello = p.tracks[3].get();
    for (auto& t : p.tracks) t->arm = false;
    cello->arm = true; cello->inType = InputType::Mono; cello->inCh = 0; h.rebuild();
    size_t before = cello->clips.size();
    p.countIn = 0; e.locate(64);
    h.record(); run(2.0, true); h.record();
    check(cello->clips.size() == before + 1, "recording makes a clip on the armed track");
    if (cello->clips.size() == before + 1) { auto& c = *cello->clips.back(); check(c.src && c.srcLen > 1.5 && std::abs(c.start - 64) < 0.01, "the take: " + juce::String(c.srcLen, 2) + " s at beat " + juce::String(c.start, 2)); }
    cello->arm = false; e.stop(); run(0.05, false);
    // and from the keyboard into an instrument
    Track* bass = p.tracks[1].get(); bass->arm = true; h.selectTrack(bass->id); h.rebuild();
    e.locate(80); h.record(); run(0.2, false);
    e.liveNote(bass->id, { 0, NoteEv::On, 50, 0.8f }); run(0.5, false); e.liveNote(bass->id, { 0, NoteEv::Off, 50, 0 }); run(0.3, false);
    h.record(); bass->arm = false;
    bool gotNotes = false; for (auto& c : bass->clips) for (auto& n : c->notes) if (n.pitch == 50 && c->start >= 79.9) gotNotes = true;
    check(gotNotes, "playing an instrument while recording writes notes");
    e.stop(); run(0.05, false);

    // 4. undo
    size_t nt = p.tracks.size();
    h.edit("new track", [&] { p.tracks.push_back(p.newTrack(TrackKind::Audio, "x")); });
    h.undo();
    check(h.project->tracks.size() == nt, "undo removes the new track");

    // 5. clip tools
    {
        auto& pr = *h.project; Clip* c = nullptr; for (auto& t : pr.tracks) for (auto& cc : t->clips) if (!cc->midi && cc->src) c = cc.get();
        if (c) {
            Uid id = c->id; double len = c->srcLen;
            auto err = h.processClip(id, "stretch", 2.0);
            Clip* c2 = h.project->clip(id);
            check(err.isEmpty() && c2 && std::abs(c2->srcLen - len * 2) < 0.1, "stretch x2: " + juce::String(len, 2) + " s -> " + juce::String(c2 ? c2->srcLen : 0, 2) + " s");
            err = h.processClip(id, "pitch", 7); c2 = h.project->clip(id);
            check(err.isEmpty() && c2 && std::abs(c2->srcLen - len * 2) < 0.1, "pitch +7 keeps the length");
            err = h.processClip(id, "revert", 0); c2 = h.project->clip(id);
            check(c2 && std::abs(c2->srcLen - len) < 0.1, "revert brings the original back");
        } else check(false, "an audio clip for the clip tools");
    }

    // 6. save and open again
    h.saveProjectAs("SCORE selftest " + juce::String(juce::Random::getSystemRandom().nextInt(100000)));
    auto folder = h.project->folder;
    int notes = 0; for (auto& t : h.project->tracks) for (auto& c : t->clips) notes += (int) c->notes.size();
    float cutNow = h.project->tracks[2]->inst->find("f1c")->get();
    h.newProject(false);
    bool opened = h.openProject(folder);
    int notes2 = 0; for (auto& t : h.project->tracks) for (auto& c : t->clips) notes2 += (int) c->notes.size();
    check(opened && h.project->tracks.size() == 4 && h.project->stations.size() == 3 && notes == notes2, "save + open: tracks, stations, " + juce::String(notes2) + " notes");
    check(h.project->tracks.size() == 4 && std::abs(h.project->tracks[2]->inst->find("f1c")->get() - cutNow) < 1, "knob values survive");
    bool audioOk = true; for (auto& t : h.project->tracks) for (auto& c : t->clips) if (!c->midi && !c->src) audioOk = false;
    check(audioOk, "every sound file is found again");

    // 7. export
    {
        Host::ExportOptions o; o.range = 2; o.stationIdx = 0; o.tail = 0.5;
        auto f = h.project->folder.getChildFile("Exports").getChildFile("test.wav"); f.getParentDirectory().createDirectory();
        bool finished = false;
        h.exportAudio(o, f, [&](bool, juce::String) { finished = true; });
        for (int i = 0; i < 600 && (h.exportProgress.load() >= 0 || !f.existsAsFile()); ++i) juce::Thread::sleep(20);
        juce::Thread::sleep(100);
        std::unique_ptr<juce::AudioFormatReader> r(h.formats.createReaderFor(f));
        double secs = r ? r->lengthInSamples / r->sampleRate : 0;
        double expect = 16 * 60.0 / h.project->bpm + 0.5;
        check(r != nullptr && std::abs(secs - expect) < 0.2, "export station 1: " + juce::String(secs, 2) + " s (expected " + juce::String(expect, 2) + ")");
        if (r) { juce::AudioBuffer<float> b(2, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); check(b.getMagnitude(0, b.getNumSamples()) > 0.02f, "the export has sound"); }
        juce::ignoreUnused(finished);
    }
    folder.deleteRecursively();
    std::printf("\n%s (%d failed)\n", fails == 0 ? "ALL GOOD" : "SOMETHING FAILED", fails);
    return fails;
}

} // namespace score
