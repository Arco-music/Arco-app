#include "../Source/Engine/Engine.h"
#include <chrono>
#include <cstdio>
#include <cmath>
using namespace arco;
int main() {
    Engine e; e.prepare(48000);
    int N = 512; std::vector<float> in1(N), o[34]; for (auto& v : o) v.assign(N, 0);
    float* outs[34]; for (int i = 0; i < 34; ++i) outs[i] = o[i].data();
    const float* ins[4] = { in1.data(), nullptr, nullptr, nullptr };
    // a busy station: halo, shadow, synth chord, drums, delay, space, 8 speakers
    e.set("st_halo", "halo0", 80); e.set("st_ch", "shd0", 60); e.set("st_ch", "dly0", 40); e.set("st_ch", "spc0", 50);
    e.set("st_drm", "drm_on", 1); e.set("st_room", "spk_n", 8);
    for (int k = 0; k < 4; ++k) { e.set("st_syn", "sn" + std::to_string(k), 48 + k * 4); e.set("st_syn", "sv" + std::to_string(k), 100); e.set("st_syn", "sg" + std::to_string(k), 1); }
    e.set("st_syn", "a_uni", 5); e.set("st_syn", "b_lvl", 60); e.synthInto = 1;
    e.set("st_ch", "vol1", 71); e.set("st_ch", "rec1", 1);
    double ph = 0; int blocks = 48000 * 20 / N;
    auto t0 = std::chrono::steady_clock::now();
    float pk = 0; bool nan = false;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < N; ++i) { in1[i] = 0.3f * std::sin(ph); ph += 2 * M_PI * 146.83 / 48000; }
        e.process(ins, 4, outs, 34, N);
        for (int i = 0; i < N; ++i) { pk = std::max(pk, std::abs(o[0][i])); if (std::isnan(o[0][i])) nan = true; }
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("20 s of audio in %.2f s  (%.0f%% of one core), peak %.2f nan %d, pitch %.2f, halo voices %.0f, synth voices %.0f\n",
           el, el / 20 * 100, pk, nan, e.snap("pitch").v.load(), e.snap("halo_n").v.load(), e.snap("syn_n").v.load());
}
