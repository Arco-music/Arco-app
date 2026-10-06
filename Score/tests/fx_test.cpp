// quick standalone check of every effect: compiles without JUCE, runs noise + tones through extreme settings
#include "../Source/Core/Effects.h"
#include <cstdio>
using namespace score;
int main() {
    int fails = 0; Rng rng(7);
    for (auto& info : effectCatalog()) {
        auto e = makeEffect(info.type);
        e->prepare(48000, 512);
        TimeInfo t; t.bpm = 120; t.playing = true;
        std::vector<float> L(512), R(512);
        double ph = 0; float peak = 0; bool bad = false; double sumIn = 0, sumOut = 0;
        for (int pass = 0; pass < 4; ++pass) {
            for (auto& p : e->params) {
                float v = pass == 0 ? p->def : pass == 1 ? p->max : pass == 2 ? p->min : p->fromNorm(rng.uni());
                p->set(v);
            }
            if (pass == 0 && e->find("hold")) e->find("hold")->set(1);
            for (int b = 0; b < 200; ++b) {
                for (int i = 0; i < 512; ++i) { float x = 0.3f * std::sin((float) ph) + 0.05f * rng.bi(); ph += 2 * kPi * 220 / 48000; L[i] = x; R[i] = x * 0.9f; sumIn += x * x; }
                e->run(L.data(), R.data(), 512, t); t.beat += 512 / 48000.0 * 2;
                for (int i = 0; i < 512; ++i) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true; peak = std::max(peak, std::max(std::abs(L[i]), std::abs(R[i]))); sumOut += L[i] * L[i]; }
            }
        }
        bool ok = !bad && peak < 8.0f && sumOut > 1e-3;
        if (!ok) ++fails;
        std::printf("%s %-6s %-12s params %2d  peak %6.2f  out/in %.2f\n", ok ? "ok  " : "FAIL", info.type, info.name, (int) e->params.size(), peak, std::sqrt(sumOut / sumIn));
    }
    std::printf("%d failed\n", fails);
    return fails;
}
