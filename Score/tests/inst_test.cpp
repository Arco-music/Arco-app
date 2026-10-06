#include "../Source/Core/Instruments.h"
#include <cstdio>
#include <chrono>
using namespace score;
static float hzOf(const std::vector<float>& x, double sr) {        // zero crossings
    int z = 0; for (size_t i = 1; i < x.size(); ++i) if (x[i - 1] <= 0 && x[i] > 0) ++z; return (float) (z * sr / x.size()); }
int main() {
    int fails = 0; auto check = [&](bool ok, const char* w, double v = 0) { if (!ok) ++fails; std::printf("%s %-50s %g\n", ok ? "ok  " : "FAIL", w, v); };
    const double SR = 48000; TimeInfo t; t.sr = SR; t.bpm = 120;
    auto nova = std::make_unique<Nova>(); nova->prepare(SR, 512);
    check(nova->params[Nova::MS0]->id == "ms0s", "NOVA parameter order matches its enum");
    check(nova->find("arp") != nullptr && nova->find("fxrev") != nullptr, "NOVA has ARP and FX switches");
    // a plain note: pitch
    nova->find("o1pos")->set(0);  nova->find("f1on")->set(0); nova->find("ms0s")->set(0);
    std::vector<float> L(48000), R(48000);
    NoteEv on { 0, NoteEv::On, 69, 0.9f };
    nova->render(L.data(), R.data(), 512, &on, 1, t);
    nova->render(L.data() + 512, R.data() + 512, 48000 - 512, nullptr, 0, t);
    std::vector<float> tail(L.begin() + 9600, L.end());
    float hz = hzOf(tail, SR); check(std::abs(hz - 440) < 3, "A4 sine = 440 Hz", hz);
    float pk = 0; for (float v : L) pk = std::max(pk, std::abs(v)); check(pk > 0.05f && pk < 1.5f, "level is sane", pk);
    NoteEv off { 0, NoteEv::Off, 69, 0 }; std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f);
    nova->render(L.data(), R.data(), 48000, &off, 1, t);
    float last = 0; for (int i = 40000; i < 48000; ++i) last = std::max(last, std::abs(L[(size_t) i]));
    check(nova->activeVoices() == 0 && last < 1e-4f, "released: silent, voice free", last);
    // every preset: 8 notes, 2 seconds, no NaN, sane level
    for (auto& p : novaPresets()) {
        applyNovaPreset(*nova, p);
        NoteEv ev[4] = { { 0, NoteEv::On, 48, 0.8f }, { 0, NoteEv::On, 55, 0.8f }, { 0, NoteEv::On, 60, 0.8f }, { 0, NoteEv::On, 64, 0.8f } };
        float mx = 0; bool bad = false; t.playing = true; t.beat = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 200; ++b) {
            std::vector<float> l(480, 0.0f), r(480, 0.0f);
            NoteEv offs[4] = { { 0, NoteEv::Off, 48, 0 }, { 0, NoteEv::Off, 55, 0 }, { 0, NoteEv::Off, 60, 0 }, { 0, NoteEv::Off, 64, 0 } };
            nova->render(l.data(), r.data(), 480, b == 0 ? ev : (b == 120 ? offs : nullptr), (b == 0 || b == 120) ? 4 : 0, t);
            t.beat += 480 / SR * 2;
            for (int i = 0; i < 480; ++i) { if (!std::isfinite(l[(size_t) i])) bad = true; mx = std::max(mx, std::abs(l[(size_t) i])); }
        }
        double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 2.0 * 100.0;
        NoteEv all { 0, NoteEv::AllOff, 1, 0 }; std::vector<float> l(64), r(64); nova->render(l.data(), r.data(), 64, &all, 1, t);
        bool ok = !bad && mx > 0.01f && mx < 3.0f;
        if (!ok) ++fails;
        std::printf("%s preset %-18s %-9s peak %.2f   cpu %.1f%% (4 notes)\n", ok ? "ok  " : "FAIL", p.name.c_str(), p.group.c_str(), mx, cpu);
    }
    // arpeggiator: 3 held notes, 1/16 at 120 bpm = 8 notes per second
    {
        Nova n2; n2.prepare(SR, 512); applyNovaPreset(n2, novaPresets()[0]); n2.find("arp")->set(1); n2.find("arpr")->set(12);
        TimeInfo tt; tt.sr = SR; tt.bpm = 120; tt.playing = true; tt.beat = 0;
        NoteEv ev[3] = { { 0, NoteEv::On, 60, 0.8f }, { 0, NoteEv::On, 64, 0.8f }, { 0, NoteEv::On, 67, 0.8f } };
        int changes = 0, lastN = -1;
        for (int b = 0; b < 100; ++b) { std::vector<float> l(480), r(480); n2.render(l.data(), r.data(), 480, b == 0 ? ev : nullptr, b == 0 ? 3 : 0, tt); tt.beat += 480 / SR * 2;
            int ln = n2.lastNote.load(); (void) ln;
            int cur = -1; for (auto& v : std::vector<int> { 60, 64, 67 }) (void) v;
            cur = n2.activeVoices(); if (cur != lastN) { ++changes; lastN = cur; } }
        check(n2.arpSteps.load() >= 7 && n2.arpSteps.load() <= 9, "ARP: 1/16 at 120 bpm = 8 notes a second", n2.arpSteps.load()); (void) changes;
    }
    // sampler: a 1 s 440 Hz sample, root A4, play A5 -> 880 Hz
    {
        auto s = std::make_shared<SampleData>(); s->sr = SR; s->L.resize(48000); s->R.resize(48000);
        for (int i = 0; i < 48000; ++i) s->L[(size_t) i] = s->R[(size_t) i] = 0.5f * (float) std::sin(2 * kPi * 440 * i / SR);
        Sampler sm; sm.prepare(SR, 512); sm.setSample(s); sm.find("root")->set(69);
        std::vector<float> l(24000), r(24000); NoteEv e { 0, NoteEv::On, 81, 1.0f };
        sm.render(l.data(), r.data(), 64, nullptr, 0, t);                         // the sample arrives
        sm.render(l.data(), r.data(), 24000, &e, 1, t);
        std::vector<float> seg(l.begin() + 2000, l.begin() + 20000);
        float h2 = hzOf(seg, SR); check(std::abs(h2 - 880) < 4, "SAMPLER plays an octave up", h2);
        s->slices = findOnsets(*s, 16); check(s->slices.size() >= 1, "onsets found", (double) s->slices.size());
    }
    // drums: every pad makes a sound
    {
        Drums d; d.prepare(SR, 512); int silent = 0;
        for (int p = 0; p < 16; ++p) { std::vector<float> l(24000), r(24000); NoteEv e { 0, NoteEv::On, 36 + p, 1.0f }; d.render(l.data(), r.data(), 24000, &e, 1, t);
            float m = 0; for (float v : l) m = std::max(m, std::abs(v)); if (m < 0.02f || m > 3) { ++silent; std::printf("   pad %d peak %.3f\n", p, m); } }
        check(silent == 0, "DRUMS: all 16 pads sound", silent);
    }
    // wavetable from audio
    {
        std::vector<float> x(48000); for (int i = 0; i < 48000; ++i) x[(size_t) i] = (float) (std::sin(2 * kPi * 220 * i / SR) + 0.3 * std::sin(2 * kPi * 660 * i / SR));
        auto wt = wavetableFromAudio(x.data(), 48000, SR, "test");
        check(wt != nullptr && wt->frames == 16, "a wavetable from a recording");
    }
    std::printf("%d failed\n", fails);
    return fails;
}
