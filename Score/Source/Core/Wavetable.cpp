#include "Wavetable.h"
#include "Dsp.h"
#include <cmath>
#include <mutex>

namespace score {

void fft(std::vector<float>& re, std::vector<float>& im, bool inverse) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * kPi / (double) len * (inverse ? 1 : -1);
        double wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k) {
                size_t a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
                re[b] = (float) (re[a] - xr); im[b] = (float) (im[a] - xi);
                re[a] = (float) (re[a] + xr); im[a] = (float) (im[a] + xi);
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

std::shared_ptr<Wavetable> wavetableFromSpectra(const std::string& name, const std::vector<std::vector<float>>& amps,
                                               const std::vector<std::vector<float>>* phases) {
    auto t = std::make_shared<Wavetable>();
    t->name = name; t->frames = (int) std::min<size_t>(Wavetable::FRAMES, std::max<size_t>(1, amps.size()));
    const int N = Wavetable::SIZE;
    t->data.assign((size_t) t->frames * Wavetable::LEVELS * (N + 1), 0.0f);
    std::vector<float> re(N), im(N);
    for (int f = 0; f < t->frames; ++f) {
        const auto& a = amps[(size_t) f];
        float norm = 1.0f;
        for (int l = 0; l < Wavetable::LEVELS; ++l) {
            int H = std::min(N / 2 - 1, 1024 >> l);
            std::fill(re.begin(), re.end(), 0.0f); std::fill(im.begin(), im.end(), 0.0f);
            for (int h = 1; h <= H && h < (int) a.size(); ++h) {
                float ph = phases ? (*phases)[(size_t) f][(size_t) h] : 0.0f;
                // a*sin(2*pi*h*t + ph): bin h = a/2 * (sin ph - i cos ph), bin N-h = its conjugate
                re[(size_t) h] = 0.5f * a[(size_t) h] * std::sin(ph); im[(size_t) h] = -0.5f * a[(size_t) h] * std::cos(ph);
                re[(size_t) (N - h)] = re[(size_t) h]; im[(size_t) (N - h)] = -im[(size_t) h];
            }
            fft(re, im, true);
            if (l == 0) { float pk = 0; for (int i = 0; i < N; ++i) pk = std::max(pk, std::abs(re[(size_t) i])); norm = pk > 1e-9f ? 0.95f / pk : 1.0f; }
            float* d = t->tableW(f, l);
            for (int i = 0; i < N; ++i) d[i] = re[(size_t) i] * norm;
            d[N] = d[0];
        }
    }
    return t;
}

// from single-cycle waveforms (time domain, SIZE samples each): their spectra, then band-limited as above
static std::shared_ptr<Wavetable> fromCycles(const std::string& name, const std::vector<std::vector<float>>& cycles) {
    const int N = Wavetable::SIZE;
    std::vector<std::vector<float>> amps, phases;
    std::vector<float> re(N), im(N);
    for (auto& c : cycles) {
        for (int i = 0; i < N; ++i) { re[(size_t) i] = c[(size_t) i]; im[(size_t) i] = 0; }
        double mean = 0; for (float v : re) mean += v; mean /= N; for (auto& v : re) v -= (float) mean;
        fft(re, im, false);
        std::vector<float> a(1025, 0.0f), p(1025, 0.0f);
        for (int h = 1; h <= 1024; ++h) {
            double r = re[(size_t) h], ii = im[(size_t) h];
            a[(size_t) h] = (float) (2.0 * std::sqrt(r * r + ii * ii) / N);
            // X = N*a/2*(sin ph - i cos ph)  ->  ph = atan2(r, -i)
            p[(size_t) h] = (float) std::atan2(r, -ii);
        }
        amps.push_back(a); phases.push_back(p);
    }
    return wavetableFromSpectra(name, amps, &phases);
}

std::vector<float> Wavetable::preview(float position, int points) const {
    std::vector<float> out((size_t) points);
    float fp = std::max(0.0f, std::min(1.0f, position)) * (float) (frames - 1);
    int f0 = (int) fp, f1 = std::min(frames - 1, f0 + 1); float ft = fp - (float) f0;
    const float* a = table(f0, 3); const float* b = table(f1, 3);
    for (int i = 0; i < points; ++i) { int k = i * SIZE / points; out[(size_t) i] = a[k] + (b[k] - a[k]) * ft; }
    return out;
}

static std::vector<float> spec() { return std::vector<float>(1025, 0.0f); }
// interpolate keyframe spectra over 16 frames
static std::vector<std::vector<float>> morph(const std::vector<std::vector<float>>& keys) {
    std::vector<std::vector<float>> out;
    for (int f = 0; f < Wavetable::FRAMES; ++f) {
        float p = (float) f / (Wavetable::FRAMES - 1) * (float) (keys.size() - 1);
        int a = (int) p, b = std::min((int) keys.size() - 1, a + 1); float t = p - (float) a;
        auto s = spec(); for (int h = 0; h <= 1024; ++h) s[(size_t) h] = keys[(size_t) a][(size_t) h] * (1 - t) + keys[(size_t) b][(size_t) h] * t;
        out.push_back(s);
    }
    return out;
}

static std::vector<std::shared_ptr<Wavetable>> build() {
    std::vector<std::shared_ptr<Wavetable>> v;
    const double PI = kPi;
    // 1 BASIC: sine > triangle > saw > square > narrow pulse
    {
        auto sine = spec(); sine[1] = 1;
        auto tri = spec(), saw = spec(), sq = spec(), pul = spec();
        for (int h = 1; h <= 1024; ++h) {
            if (h & 1) { tri[(size_t) h] = (float) (((h - 1) / 2 % 2 == 0 ? 1.0 : -1.0) / (h * h)); sq[(size_t) h] = 1.0f / (float) h; }
            saw[(size_t) h] = 1.0f / (float) h;
            pul[(size_t) h] = (float) (std::sin(PI * h * 0.12) / h);
        }
        v.push_back(wavetableFromSpectra("Basic shapes", morph({ sine, tri, saw, sq, pul })));
    }
    // 2 PULSE WIDTH
    {
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < 16; ++f) { double d = 0.5 - 0.47 * f / 15.0; auto s = spec(); for (int h = 1; h <= 1024; ++h) s[(size_t) h] = (float) (std::sin(PI * h * d) / h); fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Pulse width", fr));
    }
    // 3 HARMONIC SWEEP: brighter frame by frame
    {
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < 16; ++f) { double nh = std::pow(2.0, f * 10.0 / 15.0); auto s = spec();
            for (int h = 1; h <= 1024; ++h) { double w = h <= nh ? 1.0 : std::exp(-(h - nh) / std::max(1.0, nh * 0.3)); s[(size_t) h] = (float) (w / h); } fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Harmonic sweep", fr));
    }
    // 4 SYNC: hard-synced saw, ratio 1 -> 8
    {
        std::vector<std::vector<float>> cyc;
        for (int f = 0; f < 16; ++f) { double r = 1.0 + 7.0 * f / 15.0; std::vector<float> c(Wavetable::SIZE);
            for (int i = 0; i < Wavetable::SIZE; ++i) { double ph = std::fmod((double) i / Wavetable::SIZE * r, 1.0); c[(size_t) i] = (float) (2 * ph - 1); }
            cyc.push_back(c); }
        v.push_back(fromCycles("Sync sweep", cyc));
    }
    // 5 VOWELS: A E I O U
    {
        struct F { double f[4], bw[4], g[4]; };
        const F V[5] = { { { 730, 1090, 2440, 3400 }, { 90, 110, 160, 250 }, { 1, 0.5, 0.25, 0.1 } },
                         { { 530, 1840, 2480, 3500 }, { 70, 100, 140, 250 }, { 1, 0.6, 0.3, 0.12 } },
                         { { 270, 2290, 3010, 3600 }, { 50, 100, 150, 250 }, { 1, 0.35, 0.3, 0.12 } },
                         { { 570, 840, 2410, 3400 }, { 80, 90, 150, 250 }, { 1, 0.6, 0.15, 0.08 } },
                         { { 300, 870, 2240, 3300 }, { 60, 90, 140, 250 }, { 1, 0.4, 0.1, 0.06 } } };
        std::vector<std::vector<float>> keys;
        for (auto& vw : V) { auto s = spec(); for (int h = 1; h <= 1024; ++h) { double hz = h * 110.0, env = 0.02;
                for (int k = 0; k < 4; ++k) env += vw.g[k] / (1.0 + std::pow((hz - vw.f[k]) / vw.bw[k], 2.0));
                s[(size_t) h] = (float) (env / std::sqrt((double) h)); } keys.push_back(s); }
        v.push_back(wavetableFromSpectra("Vowels", morph(keys)));
    }
    // 6 ORGAN: drawbar registrations
    {
        const float DB[6][9] = { { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 0, 8, 0, 0, 8, 0, 0, 8 },
                                 { 8, 6, 8, 8, 6, 8, 0, 6, 8 }, { 0, 0, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
        const int H[9] = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };   // 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' (relative to 16' = harmonic 1)
        std::vector<std::vector<float>> keys;
        for (auto& r : DB) { auto s = spec(); for (int k = 0; k < 9; ++k) s[(size_t) H[k]] += r[k] / 8.0f; keys.push_back(s); }
        v.push_back(wavetableFromSpectra("Organ", morph(keys)));
    }
    // 7 BOWED: a bowed string through a wooden body, sul tasto -> sul ponticello
    {
        std::vector<std::vector<float>> fr;
        const double body[5] = { 280, 460, 1250, 2600, 4200 }, bg[5] = { 1.0, 0.7, 0.5, 0.35, 0.15 }, bw[5] = { 60, 90, 300, 500, 800 };
        for (int f = 0; f < 16; ++f) { double roll = 1.7 - 1.05 * f / 15.0; auto s = spec();
            for (int h = 1; h <= 1024; ++h) { double hz = h * 130.8, env = 0.25; for (int k = 0; k < 5; ++k) env += bg[k] / (1 + std::pow((hz - body[k]) / bw[k], 2));
                s[(size_t) h] = (float) (env / std::pow((double) h, roll)); }
            fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Bowed string", fr));
    }
    // 8 HOLLOW: odd harmonics, the even ones fade in
    {
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < 16; ++f) { auto s = spec(); double e = f / 15.0;
            for (int h = 1; h <= 1024; ++h) s[(size_t) h] = (float) ((h & 1 ? 1.0 : e) / h); fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Hollow", fr));
    }
    // 9 WARM PAD: soft, slowly brightening, a gentle resonance moving up
    {
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < 16; ++f) { auto s = spec(); double k = 2.0 + 10.0 * f / 15.0, pk = 3 + f * 1.5;
            for (int h = 1; h <= 1024; ++h) s[(size_t) h] = (float) (std::exp(-h / k) + 0.4 * std::exp(-std::pow((h - pk) / 2.0, 2.0))); fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Warm pad", fr));
    }
    // 10 METAL: sparse, bright partials
    {
        std::vector<std::vector<float>> fr; Rng rng(1234);
        std::vector<float> base(1025, 0.0f); for (int h = 1; h <= 1024; ++h) base[(size_t) h] = rng.uni();
        for (int f = 0; f < 16; ++f) { auto s = spec(); double th = 0.97 - 0.5 * f / 15.0;
            for (int h = 1; h <= 1024; ++h) s[(size_t) h] = base[(size_t) h] > th || h == 1 ? (float) (1.0 / std::pow(h, 0.7)) : 0.0f; fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Metal", fr));
    }
    // 11 DIGITAL: bit patterns
    {
        std::vector<std::vector<float>> cyc;
        for (int f = 0; f < 16; ++f) { std::vector<float> c(Wavetable::SIZE); int bits = 1 + f;
            for (int i = 0; i < Wavetable::SIZE; ++i) { int x = (i * bits * 37 / Wavetable::SIZE) ^ (i * 5 / Wavetable::SIZE * (f + 3));
                c[(size_t) i] = ((x >> 1) & 1 ? 0.8f : -0.8f) * (0.6f + 0.4f * (float) ((x & 3) / 3.0)); }
            cyc.push_back(c); }
        v.push_back(fromCycles("Digital", cyc));
    }
    // 12 NOISY: random spectra (with random phases) - airy, breathy
    {
        std::vector<std::vector<float>> fr, ph; Rng rng(77);
        for (int f = 0; f < 16; ++f) { auto s = spec(), p = spec(); double tilt = 0.3 + 1.2 * (15 - f) / 15.0;
            for (int h = 1; h <= 1024; ++h) { s[(size_t) h] = (float) (rng.uni() / std::pow(h, tilt)); p[(size_t) h] = rng.uni() * kTwoPiF; }
            fr.push_back(s); ph.push_back(p); }
        v.push_back(wavetableFromSpectra("Noisy", fr, &ph));
    }
    // 13 GLASS: wide, bell-like spacing
    {
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < 16; ++f) { auto s = spec(); int step = 2 + f / 2;
            for (int h = 1; h <= 1024; h += step) s[(size_t) h] = (float) (1.0 / std::pow(h, 0.55)); s[1] = 1; fr.push_back(s); }
        v.push_back(wavetableFromSpectra("Glass", fr));
    }
    return v;
}

const std::vector<std::shared_ptr<Wavetable>>& factoryTables() {
    static std::vector<std::shared_ptr<Wavetable>> t = build();
    return t;
}
std::vector<std::string> factoryTableNames() { std::vector<std::string> n; for (auto& t : factoryTables()) n.push_back(t->name); n.push_back("User (drop a sound)"); return n; }

std::shared_ptr<Wavetable> wavetableFromAudio(const float* x, int n, double sr, const std::string& name) {
    const int N = Wavetable::SIZE;
    if (x == nullptr || n < 64) return nullptr;
    // the period of the sound (YIN over the first good part); if it has no pitch, frames are plain 2048-sample windows
    PitchTracker pt; pt.prepare(sr);
    std::vector<float> per;
    for (int i = 0; i + 512 <= n; i += 512) { pt.push(x + i, 512); if (pt.period > 0) per.push_back(pt.period); }
    float period = 0;
    if (per.size() > 4) { std::sort(per.begin(), per.end()); period = per[per.size() / 2]; }
    std::vector<std::vector<float>> cycles;
    for (int f = 0; f < Wavetable::FRAMES; ++f) {
        std::vector<float> c((size_t) N);
        double len = period > 0 ? period : std::min(n, N);
        double start = (double) f / Wavetable::FRAMES * std::max(0.0, n - len - 2);
        for (int i = 0; i < N; ++i) {
            double p = start + len * i / N; int i0 = (int) p; double fr = p - i0;
            float a = x[std::min(n - 1, i0)], b = x[std::min(n - 1, i0 + 1)];
            float w = period > 0 ? 1.0f : (float) (0.5 - 0.5 * std::cos(2 * kPi * i / N));     // windowed when it has no pitch
            c[(size_t) i] = (float) (a + (b - a) * fr) * w;
        }
        cycles.push_back(c);
    }
    return fromCycles(name, cycles);
}

} // namespace score
