#include "Render.h"
#include "AudioHost.h"

namespace arco {

static const char* RS[15] = { "rs_bl", "rs_bts", "rs_len0", "rs_len1", "rs_len2", "rs_len3", "rs_mask0", "rs_mask1", "rs_mask2", "rs_mask3",
                              "rs_spd0", "rs_spd1", "rs_spd2", "rs_spd3", "rs_ntr" };
static const char* SV[15] = { "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
                              "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "sv_ntr" };

RenderSnapshot Renderer::snapshot() {
    RenderSnapshot s;
    s.song = host.model.songState(); s.setup = host.model.setupState();
    for (int i = 0; i < 15; ++i) s.sv[i] = host.engine.snap(SV[i]).v.load();
    s.smask = (int) host.engine.snap("smask").v.load(); s.sceneNow = host.model.sceneNow; s.sr = host.engine.sr;
    return s;
}

bool Renderer::renderMix(const ExportOptions& o, juce::AudioBuffer<float>& out, double& rate, int solo, std::function<bool(float)> progress) {
    return renderMix(snapshot(), o, out, rate, solo, std::move(progress));
}

bool Renderer::renderMix(const RenderSnapshot& snp, const ExportOptions& o, juce::AudioBuffer<float>& out, double& rate, int solo, std::function<bool(float)> progress) {
    auto& src = host.engine;
    const double sr = snp.sr; rate = sr;
    int smask = snp.smask;
    if (smask == 0) return false;
    auto sv = [&](int i) { return snp.sv[i]; };
    // which scenes, in which order
    juce::Array<int> scenes;
    if (o.what == ExportOptions::CurrentScene) { int sc = juce::jlimit(1, 4, snp.sceneNow); if ((smask >> (sc - 1)) & 1) scenes.add(sc); }
    else for (int s = 1; s <= 4; ++s) if ((smask >> (s - 1)) & 1) scenes.add(s);
    if (scenes.isEmpty()) return false;
    // a private engine with the same song
    auto e2 = std::make_unique<Engine>();
    e2->prepare(sr);
    Model m2(*e2);
    m2.loadSongState(snp.song);
    m2.loadSetupState(snp.setup);
    m2.set("countin", 0); m2.set("clickall", 0); m2.set("clk_room", 0);
    m2.set("amb_on", 0);                      // an export is stereo: the sounds placed in the space come back into the stereo mix
    for (int j = 0; j < 4; ++j) e2->inputChannel[j].store(-1);
    e2->synthInto.store(0);
    for (const char* L : { "A", "B", "C", "D" }) {
        for (auto* nm : { "st_loops", "st_pitch" }) {
            auto& a = src.buffer(std::string(nm) + L); auto& b = e2->buffer(std::string(nm) + L);
            if (a.data.size() == b.data.size()) std::copy(a.data.begin(), a.data.end(), b.data.begin());
        }
    }
    // recording the main mix
    std::vector<float> L, R; L.reserve((size_t) (sr * 120)); R.reserve((size_t) (sr * 120));
    bool recording = false;
    e2->recorder = [&](const float* l, const float* r, int n) { if (recording) { L.insert(L.end(), l, l + n); R.insert(R.end(), r, r + n); } };
    const int N = 512;
    std::vector<float> o0((size_t) N), o1((size_t) N); float* outs[2] = { o0.data(), o1.data() };
    auto block = [&] { e2->process(nullptr, 0, outs, 2, N); m2.tick(); };
    for (int i = 0; i < 4; ++i) block();                        // the looper starts (it notes the commands as they are now)
    for (int i = 0; i < 15; ++i) e2->set("st_looper", RS[i], sv(i));
    m2.press("st_looper", "rs_go");                             // ... then the session is restored
    for (int i = 0; i < 20; ++i) block();
    auto applySolo = [&] { if (solo >= 0) for (int s = 0; s < 12; ++s) m2.set("solo" + std::to_string(s), s == solo ? 1 : 0); };
    double barLen = sv(0) * juce::jmax(1.0f, sv(1));
    double total = 0;
    for (int sc : scenes) total += sv(2 + sc - 1) * o.repeats;
    total += o.tailSeconds * sr;
    // first scene, then play
    m2.press("st_looper", "scene", 8, scenes[0]);
    for (int i = 0; i < 20; ++i) block();
    applySolo();
    for (int i = 0; i < 4; ++i) block();
    m2.press("st_looper", "playall");
    recording = true;
    double done = 0; int k = 0;
    for (int si = 0; si < scenes.size(); ++si) {
        double len = sv(2 + scenes[si] - 1) * o.repeats;
        double start = done;
        bool pressed = false;
        while (done - start < len) {
            // ask for the next scene half a bar before this one ends: it changes on the bar line
            if (!pressed && si + 1 < scenes.size() && done - start > len - barLen * 0.5) { m2.press("st_looper", "scene", 8, scenes[si + 1]); pressed = true; }
            block(); done += N;
            if (pressed && m2.sceneNow == scenes[si + 1]) applySolo();
            if (++k % 64 == 0 && progress && !progress((float) (done / total))) return false;
        }
    }
    m2.press("st_looper", "stopall");                          // the loops stop, the effects ring out
    double tailEnd = done + o.tailSeconds * sr;
    while (done < tailEnd) { block(); done += N; if (++k % 64 == 0 && progress && !progress((float) (done / total))) return false; }
    recording = false;
    out.setSize(2, (int) L.size());
    std::copy(L.begin(), L.end(), out.getWritePointer(0));
    std::copy(R.begin(), R.end(), out.getWritePointer(1));
    return out.getNumSamples() > 0;
}

bool Renderer::dryStems(int scene, juce::OwnedArray<juce::AudioBuffer<float>>& out, juce::StringArray& names, double& rate) {
    auto& src = host.engine; rate = src.sr;
    scene = juce::jlimit(1, 4, scene);
    const char* LL[4] = { "A", "B", "C", "D" };
    long len = (long) src.snap(SV[2 + scene - 1]).v.load();
    int mask = (int) src.snap(SV[6 + scene - 1]).v.load();
    if (len <= 0) return false;
    auto& b = src.buffer(std::string("st_loops") + LL[scene - 1]);
    for (int t = 0; t < 8; ++t) {
        if (!((mask >> t) & 1)) continue;
        auto* buf = new juce::AudioBuffer<float>(1, (int) juce::jmin(len, b.len));
        std::copy(b.chan(t), b.chan(t) + buf->getNumSamples(), buf->getWritePointer(0));
        out.add(buf); names.add("Track " + juce::String(t + 1));
    }
    return !out.isEmpty();
}

double Renderer::resampleTo(juce::AudioBuffer<float>& b, double from, double to) {
    if (std::abs(from - to) < 0.5) return to;
    double ratio = from / to;
    int outN = (int) std::ceil(b.getNumSamples() / ratio);
    juce::AudioBuffer<float> o(b.getNumChannels(), outN);
    for (int c = 0; c < b.getNumChannels(); ++c) {
        juce::WindowedSincInterpolator ip;
        ip.process(ratio, b.getReadPointer(c), o.getWritePointer(c), outN, b.getNumSamples(), 0);
    }
    b = std::move(o);
    return to;
}

bool Renderer::write(juce::AudioBuffer<float> buf, double rate, const ExportOptions& o, const juce::File& f, juce::String& report, bool normalise) {
    if (buf.getNumSamples() == 0) { report = "nothing to export"; return false; }
    rate = resampleTo(buf, rate, o.sampleRate);
    double before = loud::integrated(buf, rate);
    if (normalise && o.loudness > 0 && before > -69.0) {
        static const double T[4] = { 0, -14.0, -10.0, -23.0 };
        buf.applyGain((float) juce::Decibels::decibelsToGain(T[o.loudness] - before));
    }
    loud::limit(buf, rate, -1.0f);                             // never above -1 dBTP: safe on every platform
    double after = loud::integrated(buf, rate);
    if (o.bits == 16) {                                        // TPDF dither: clean fades at 16 bit
        juce::Random r(1234);
        float lsb = 1.0f / 32768.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c) { auto* d = buf.getWritePointer(c); for (int i = 0; i < buf.getNumSamples(); ++i) d[i] += (r.nextFloat() - r.nextFloat()) * lsb; }
    }
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
    if (os == nullptr) { report = "could not write " + f.getFileName(); return false; }
    std::unique_ptr<juce::AudioFormat> fmt;
    if (o.flac) fmt = std::make_unique<juce::FlacAudioFormat>(); else fmt = std::make_unique<juce::WavAudioFormat>();
    int bits = o.flac ? juce::jmin(24, o.bits) : o.bits;
    std::unique_ptr<juce::AudioFormatWriter> w(fmt->createWriterFor(os.get(), rate, (unsigned int) buf.getNumChannels(), bits, {}, 0));
    if (w == nullptr) { report = "this format / sample rate combination is not possible"; return false; }
    os.release();
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    w.reset();
    int secs = (int) (buf.getNumSamples() / rate);
    report = juce::String(after, 1) + " LUFS  ·  peak <= -1 dBTP  ·  " + juce::String(secs / 60) + ":" + juce::String(secs % 60).paddedLeft('0', 2)
             + "  ·  " + juce::String((int) rate) + " Hz / " + juce::String(bits) + (bits == 32 ? "-bit float" : "-bit") + (o.flac ? " FLAC" : " WAV");
    return true;
}

} // namespace arco
