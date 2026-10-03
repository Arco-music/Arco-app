#pragma once
// SCORE - the effects. Every effect is stereo, in place, with its own knobs (parameters).
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include "Param.h"

namespace score {

// a ring of the last output, for the little screen on every effect card (every 4th sample)
struct ScopeRing {
    static constexpr int N = 1024;
    std::array<float, N> d {}; std::atomic<int> w { 0 }; int dec = 0;
    void push(const float* l, const float* r, int n) {
        int ww = w.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) if (++dec >= 4) { dec = 0; d[(size_t) ww] = 0.5f * (l[i] + r[i]); ww = (ww + 1) % N; }
        w.store(ww, std::memory_order_release); }
};

class Effect : public ParamOwner {
public:
    explicit Effect(std::string t) : type(std::move(t)) {}
    ~Effect() override = default;
    std::string type;
    std::atomic<bool> enabled { true };
    double sr = 48000.0; int maxBlock = 512;

    void prepare(double s, int mb);
    bool isPrepared(double s) const { return prepared && sr == s; }
    // runs the effect with a 10 ms crossfade when it is switched on / off
    void run(float* L, float* R, int n, const TimeInfo& t);
    virtual void process(float* L, float* R, int n, const TimeInfo& t) = 0;
    virtual void reset() {}
    virtual double tailSeconds() const { return 0.05; }
    // for drawing curves (EQ, filter): response in dB at f
    virtual bool hasCurve() const { return false; }
    virtual float curveDb(double /*f*/) const { return 0.0f; }
    // live values for the card (gain reduction, a detected pitch ...)
    std::atomic<float> meter[4] { {0}, {0}, {0}, {0} };
    ScopeRing scope;
protected:
    virtual void onPrepare() {}
    std::vector<float> dryL, dryR;
private:
    float fade = 1.0f; bool prepared = false;
};

struct EffectInfo {
    const char* type; const char* name; const char* group; const char* about;
    float r, g, b;                              // card colour
    std::function<std::unique_ptr<Effect>()> make;
};
const std::vector<EffectInfo>& effectCatalog();
const EffectInfo* effectInfo(const std::string& type);
std::unique_ptr<Effect> makeEffect(const std::string& type);   // nullptr when unknown

// small helper used by many effects: dry/wet mix in place (wet already in L/R, dry in dl/dr)
inline void mixDryWet(float* L, float* R, const float* dl, const float* dr, int n, float wet, float& smooth) {
    for (int i = 0; i < n; ++i) { smooth += (wet - smooth) * 0.002f;
        L[i] = dl[i] + (L[i] - dl[i]) * smooth; R[i] = dr[i] + (R[i] - dr[i]) * smooth; }
}

} // namespace score
