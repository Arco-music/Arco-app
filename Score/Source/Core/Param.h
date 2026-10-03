#pragma once
// SCORE - a parameter: a knob's value, shared between the interface (message thread) and the sound (audio thread).
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdio>
#include <functional>
#include "Dsp.h"

namespace score {

enum class Unit { None, Percent, dB, Hz, Ms, Sec, Semi, Cents, Ratio, Choice, OnOff, Pan, Oct, Mult, Note };

struct Param {
    std::string id, name;
    float min = 0, max = 1, def = 0;
    Unit unit = Unit::None;
    bool log = false;                     // knob travels logarithmically (frequencies, times)
    std::vector<std::string> choices;
    std::atomic<float> v { 0 };
    bool modulatable = true;              // can be automated / stored in stations

    Param(std::string i, std::string n, float lo, float hi, float d, Unit u, bool lg = false, std::vector<std::string> ch = {})
        : id(std::move(i)), name(std::move(n)), min(lo), max(hi), def(d), unit(u), log(lg), choices(std::move(ch)) { v.store(d); }

    float get() const { return v.load(std::memory_order_relaxed); }
    int geti() const { return (int) std::lround(get()); }
    bool on() const { return get() > 0.5f; }
    void set(float x) {
        if (!std::isfinite(x)) return;
        x = x < min ? min : (x > max ? max : x);
        if (isDiscrete()) x = std::round(x);
        v.store(x, std::memory_order_relaxed);
    }
    bool isDiscrete() const { return unit == Unit::Choice || unit == Unit::OnOff || unit == Unit::Semi || unit == Unit::Note || unit == Unit::Oct; }
    // 0..1 position of the knob
    float toNorm(float x) const {
        if (max <= min) return 0;
        if (log && min > 0) return (float) (std::log(x / min) / std::log(max / min));
        return (x - min) / (max - min);
    }
    float fromNorm(float t) const {
        t = clampf(t, 0, 1);
        if (log && min > 0) return min * std::pow(max / min, t);
        return min + (max - min) * t;
    }
    float norm() const { return toNorm(get()); }

    std::string text() const { return format(get()); }
    std::string format(float x) const {
        char b[64];
        switch (unit) {
            case Unit::Percent: std::snprintf(b, sizeof b, "%.0f%%", x); break;
            case Unit::dB: if (x <= min + 0.01f && min <= -60) std::snprintf(b, sizeof b, "-inf"); else std::snprintf(b, sizeof b, "%+.1f dB", x); break;
            case Unit::Hz: if (x >= 1000) std::snprintf(b, sizeof b, "%.2f kHz", x / 1000); else if (x < 10) std::snprintf(b, sizeof b, "%.2f Hz", x); else std::snprintf(b, sizeof b, "%.0f Hz", x); break;
            case Unit::Ms: if (x >= 1000) std::snprintf(b, sizeof b, "%.2f s", x / 1000); else if (x < 10) std::snprintf(b, sizeof b, "%.1f ms", x); else std::snprintf(b, sizeof b, "%.0f ms", x); break;
            case Unit::Sec: if (x < 10) std::snprintf(b, sizeof b, "%.2f s", x); else std::snprintf(b, sizeof b, "%.1f s", x); break;
            case Unit::Semi: std::snprintf(b, sizeof b, "%+d st", (int) std::lround(x)); break;
            case Unit::Cents: std::snprintf(b, sizeof b, "%+.0f ct", x); break;
            case Unit::Ratio: std::snprintf(b, sizeof b, x >= 20 ? "inf:1" : "%.1f:1", x); break;
            case Unit::Choice: { int i = (int) std::lround(x); return i >= 0 && i < (int) choices.size() ? choices[(size_t) i] : std::to_string(i); }
            case Unit::OnOff: return x > 0.5f ? "ON" : "OFF";
            case Unit::Pan: { int p = (int) std::lround(x); if (p == 0) return "C"; std::snprintf(b, sizeof b, "%s%d", p < 0 ? "L" : "R", std::abs(p)); break; }
            case Unit::Oct: std::snprintf(b, sizeof b, "%+d oct", (int) std::lround(x)); break;
            case Unit::Mult: std::snprintf(b, sizeof b, "x%.2f", x); break;
            case Unit::Note: { static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                int n = (int) std::lround(x); std::snprintf(b, sizeof b, "%s%d", N[((n % 12) + 12) % 12], n / 12 - 2); break; }
            default: if (std::abs(max - min) <= 2.0f) std::snprintf(b, sizeof b, "%.2f", x); else std::snprintf(b, sizeof b, "%.0f", x); break;
        }
        return b;
    }
};

// something that owns parameters (an effect, an instrument, a mixer channel)
class ParamOwner {
public:
    virtual ~ParamOwner() = default;
    std::vector<std::unique_ptr<Param>> params;
    uint32_t uid = 0;                    // unique in the project: parameters are addressed as "<uid>/<id>"
    Param* find(const std::string& id) const { for (auto& p : params) if (p->id == id) return p.get(); return nullptr; }
    Param& operator[](int i) { return *params[(size_t) i]; }
    float val(int i) const { return params[(size_t) i]->get(); }
    void resetToDefaults() { for (auto& p : params) p->set(p->def); }
protected:
    Param* add(const std::string& id, const std::string& name, float lo, float hi, float def, Unit u, bool lg = false, std::vector<std::string> ch = {}) {
        params.push_back(std::make_unique<Param>(id, name, lo, hi, def, u, lg, std::move(ch)));
        return params.back().get();
    }
    Param* addChoice(const std::string& id, const std::string& name, std::vector<std::string> ch, int def = 0) {
        float hi = (float) std::max<size_t>(1, ch.size()) - 1.0f;
        return add(id, name, 0, hi, (float) def, Unit::Choice, false, std::move(ch));
    }
    Param* addOnOff(const std::string& id, const std::string& name, bool def = false) { return add(id, name, 0, 1, def ? 1.0f : 0.0f, Unit::OnOff); }
};

// a new unique id for anything in the project
uint32_t newUid();
void bumpUid(uint32_t seen);             // after loading a project: new ids start above everything that was read

} // namespace score
