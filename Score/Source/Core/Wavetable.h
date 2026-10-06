#pragma once
// SCORE - wavetables for the NOVA synth: band-limited single cycles (one copy per octave, so high notes never alias),
// up to 16 frames per table that the POSITION knob morphs through.
#include <memory>
#include <string>
#include <vector>

namespace score {

struct Wavetable {
    static constexpr int SIZE = 2048;          // samples per cycle
    static constexpr int LEVELS = 11;          // level L keeps 1024 >> L harmonics
    static constexpr int FRAMES = 16;
    std::string name;
    int frames = FRAMES;
    std::vector<float> data;                   // [frame][level][SIZE + 1]  (the extra sample = the first one, for interpolation)
    const float* table(int frame, int level) const { return data.data() + ((size_t) frame * LEVELS + (size_t) level) * (SIZE + 1); }
    float* tableW(int frame, int level) { return data.data() + ((size_t) frame * LEVELS + (size_t) level) * (SIZE + 1); }
    // which level to use for a fundamental of f Hz at sample rate sr
    static int levelFor(double f, double sr) {
        double maxH = (sr * 0.45) / std::max(1.0, f);
        int l = 0; while (l < LEVELS - 1 && (1024 >> l) > maxH) ++l;
        return l;
    }
    // a frame drawn as 128 points (for the screen)
    std::vector<float> preview(float position, int points = 128) const;
};

// the factory tables, built once
const std::vector<std::shared_ptr<Wavetable>>& factoryTables();
std::vector<std::string> factoryTableNames();
// a table made from any sound (a recording, a sample): 16 frames across the file, each one cycle long
std::shared_ptr<Wavetable> wavetableFromAudio(const float* x, int n, double sr, const std::string& name);
// a table from harmonic spectra: spectra[frame][harmonic] (amplitudes, harmonic 1 = index 1)
std::shared_ptr<Wavetable> wavetableFromSpectra(const std::string& name, const std::vector<std::vector<float>>& amps,
                                               const std::vector<std::vector<float>>* phases = nullptr);

// a small FFT (radix 2, in place), used for band-limiting
void fft(std::vector<float>& re, std::vector<float>& im, bool inverse);

} // namespace score
