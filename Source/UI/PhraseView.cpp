#include "ShellViews.h"

namespace arco {

static void say(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

PhraseView::PhraseView(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    auto& m = shell.host.model;
    auto col = [] { return Theme::pal().acc[5]; };
    rec = add<Pill>("REC", [this] { int st = shell.host.engine.phrase.stateOut.load(); return st == 1 || st == 2 ? Theme::red() : Theme::btn(); }, 11.0f, 14.0f);
    rec->icon = 3; rec->tint = true; rec->onFn = [] { return true; };
    rec->solidFn = [this] { int st = shell.host.engine.phrase.stateOut.load(); return st == 1 || st == 2; };
    rec->onClick = [&m] { m.press("st_phr", "phr_rec"); };
    play = add<Pill>("PLAY", [this] { return shell.host.engine.phrase.isPlaying.load() ? Theme::green() : Theme::btn(); }, 11.0f, 14.0f);
    play->icon = 9; play->tint = true;
    play->onClick = [&m] { m.press("st_phr", "phr_play"); };
    clear = add<Pill>("CLEAR", [] { return Theme::btn(); }, 9.0f, 14.0f);
    clear->textColour = [] { return Theme::txt(); }; clear->onClick = [&m] { m.press("st_phr", "phr_clear"); };
    build = add<Pill>("BUILD", [this] { return shell.host.engine.phrase.buildOut.load() > 0 ? Theme::amber() : Theme::btn(); }, 12.0f, 14.0f);
    build->tint = true; build->onFn = [] { return true; }; build->solidFn = [this] { return shell.host.engine.phrase.buildOut.load() > 0; };
    build->onClick = [&m] { m.press("st_phr", "phr_bgo"); };
    build->setTooltip("a crescendo: faster, louder, brighter, more space - over the bars below, then it drops back");
    src = add<Segments>(juce::StringArray { "LIVE 1", "LIVE 2", "LIVE 3", "LIVE 4" }, [&m] { return (int) m.get("phr_src") - 1; }, [&m](int i) { m.set("phr_src", i + 1); }, col);
    len = add<Segments>(juce::StringArray { juce::String::fromUTF8("½ BAR"), "1 BAR", "2 BARS", "4 BARS" }, [&m] { return (int) m.get("phr_len"); }, [&m](int i) { m.set("phr_len", i); }, col);
    rate = add<Segments>(juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" }, [&m] { return (int) m.get("phr_rate"); }, [&m](int i) { m.set("phr_rate", i); }, col);
    order = add<Segments>(juce::StringArray { "FORWARD", "BACKWARD", "PING-PONG", "RANDOM", "SPIRAL", "SKIP" }, [&m] { return (int) m.get("phr_order"); }, [&m](int i) { m.set("phr_order", i); }, col);
    auto k = [&](const juce::String& n, const std::string& id, double lo, double hi, double def, ColourFn c, std::function<juce::String(double)> f) {
        auto* kc = add<KnobCell>(n, [&m, id] { return m.get(id); }, [&m, id](double v) { m.set(id, v); }, lo, hi, def, std::move(c), std::move(f)); knobs.push_back(kc); return kc; };
    auto pct = [](double v) { return juce::String((int) std::round(v)) + " %"; };
    ColourFn teal = col, amb = [] { return Theme::amber(); }, blu = [] { return Theme::blue(); };
    k("GATE", "phr_gate", 5, 100, 85, teal, pct);
    k("PITCH", "phr_pitch", -24, 24, 0, teal, [](double v) { int s = (int) std::round(v); return (s > 0 ? "+" : "") + juce::String(s) + " st"; });
    k("OCTAVES", "phr_oct", 0, 3, 0, teal, [](double v) { int o = (int) std::round(v); return o == 0 ? juce::String("off") : "up " + juce::String(o); });
    k("REVERSE", "phr_rev", 0, 100, 0, teal, pct);
    k("SWING", "phr_swing", 50, 75, 50, teal, pct);
    k("STEPS", "phr_steps", 0, 16, 0, teal, [](double v) { int s = (int) std::round(v); return s == 0 ? juce::String("auto") : juce::String(s); });
    k("FILTER", "phr_cut", 0, 100, 100, blu, [](double v) { return v >= 99 ? juce::String("open") : juce::String((int) v); });
    k("RESO", "phr_res", 0, 100, 15, blu, pct);
    k("FILTER ENV", "phr_env", 0, 100, 30, blu, pct);
    k("SPREAD", "phr_spread", 0, 100, 30, blu, pct);
    k("SPACE", "phr_space", 0, 100, 20, blu, pct);
    k("VOLUME", "phr_vol", 0, 100, 80, [] { return Theme::txt(); }, nullptr);
    k("AMOUNT", "phr_build", 0, 100, 60, amb, pct)->setComponentID("ph:bamt");
    k("BARS", "phr_blen", 1, 16, 4, amb, [](double v) { return juce::String((int) std::round(v)) + " bars"; })->setComponentID("ph:blen");
    startTimerHz(30);
}

void PhraseView::resized() {
    int W = getWidth(), H = getHeight();
    waveR = { 18, 16, W - 36, juce::jmin(250, H / 3) };
    int y = waveR.getBottom() + 18;
    rec->setBounds(18, y, 130, 54); play->setBounds(156, y, 130, 54); clear->setBounds(294, y, 96, 54);
    if (W >= 1200) { src->setBounds(420, y + 20, 360, 34); len->setBounds(800, y + 20, 380, 34); y += 82; }
    else { src->setBounds(420, y + 20, W - 18 - 420, 34); y += 82; len->setBounds(18, y + 20, 380, 34); y += 82; }
    if (W >= 1200) { rate->setBounds(18, y + 20, 560, 38); order->setBounds(598, y + 20, W - 18 - 598, 38); y += 84; }
    else { rate->setBounds(18, y + 20, W - 36, 38); y += 84; order->setBounds(18, y + 20, W - 36, 38); y += 84; }
    buildR = { W - 18 - 380, y, 380, H - 18 - y };
    int kw = (buildR.getX() - 18 - 18) / 6, kh = juce::jmin(150, (H - 18 - y - 24) / 2);
    int n = 0;
    for (auto* kc : knobs) {
        if (kc->getComponentID().startsWith("ph:")) continue;
        kc->setBounds(18 + (n % 6) * kw, y + 20 + (n / 6) * kh, kw, juce::jmin(kh, 120)); ++n;
    }
    build->setBounds(buildR.getX() + 20, buildR.getY() + 44, buildR.getWidth() - 40, 64);
    int bx = buildR.getX() + 20, bw = (buildR.getWidth() - 40) / 2;
    for (auto* kc : knobs) { if (kc->getComponentID() == "ph:bamt") kc->setBounds(bx, buildR.getY() + 150, bw, 110); if (kc->getComponentID() == "ph:blen") kc->setBounds(bx + bw, buildR.getY() + 150, bw, 110); }
}

void PhraseView::paint(juce::Graphics& g) {
    struct ShadeAtEnd { juce::Graphics& g; juce::Component& c; ~ShadeAtEnd() { drawSoftShades(g, c); } } shadeAtEnd { g, *this };
    Theme::backdrop(g, getLocalBounds());
    auto& P = shell.host.engine.phrase;
    auto col = Theme::pal().acc[5];
    drawPanel(g, waveR.toFloat(), 18);
    auto in = waveR.reduced(22, 36);
    int st = P.stateOut.load(), n = P.slicesOut.load();
    juce::String head = st == 0 ? "EMPTY" : st == 1 ? juce::String::fromUTF8("WAITING FOR THE DOWNBEAT …") : st == 2 ? "RECORDING" : (P.isPlaying.load() ? "PLAYING" : "READY");
    say(g, Theme::keyCase(head), { waveR.getX() + 22, waveR.getY() + 10, 400, 22 }, Theme::softUI() ? 12.0f : 10.5f, st == 2 || st == 1 ? Theme::ink(Theme::red()) : Theme::txt(), 2);
    if (st == 3) say(g, juce::String(n) + (n == 1 ? " note" : " notes") + " found", { waveR.getRight() - 322, waveR.getY() + 10, 300, 22 }, 9.0f, Theme::sub(), 1, juce::Justification::centredRight);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(in.toFloat().expanded(8, 8), 12.0f);
    if (st == 0) {
        say(g, juce::String::fromUTF8("Choose the input, press REC and play a short phrase - four notes, say. It starts on the next downbeat."), in, 11.0f, Theme::sub(), 1, juce::Justification::centred);
    } else if (st == 1 || st == 2) {
        float p = P.recProgress.load();
        g.setColour(Theme::red().withAlpha(0.25f)); g.fillRoundedRectangle(in.toFloat().withWidth(in.getWidth() * p), 8.0f);
        g.setColour(Theme::red()); g.fillRect((float) in.getX() + in.getWidth() * p - 1, (float) in.getY(), 2.0f, (float) in.getHeight());
        float lv = shell.host.engine.peak("live_in" + juce::String((int) shell.host.model.get("phr_src")).toStdString());
        say(g, st == 1 ? juce::String::fromUTF8("get ready …") : juce::String::fromUTF8("play now"), in, 13.0f, Theme::txt(), 2, juce::Justification::centred);
        g.setColour(Theme::green()); g.fillRoundedRectangle((float) in.getX(), (float) in.getBottom() - 6, in.getWidth() * juce::jmin(1.0f, lv * 2.0f), 4.0f, 2.0f);
    } else {
        // the phrase, its notes in colours, the one playing lit
        int cur = P.sliceOut.load();
        for (int k = 0; k < n; ++k) {
            float a = P.sliceAt[(size_t) k].load(), b = P.sliceAt[(size_t) k + 1].load(); if (a < 0 || b < 0) continue;
            auto r = juce::Rectangle<float>(in.getX() + a * in.getWidth(), (float) in.getY(), (b - a) * in.getWidth(), (float) in.getHeight());
            auto c = Theme::pal().acc[(size_t) ((k + 5) % 8)];
            g.setColour(c.withAlpha(k == cur && P.isPlaying.load() ? 0.35f : 0.10f)); g.fillRoundedRectangle(r.reduced(1, 0), 6.0f);
            g.setColour(c); g.fillRect(r.getX(), r.getY(), 2.0f, r.getHeight());
            say(g, juce::String(k + 1), r.toNearestInt().reduced(6, 4), 9.0f, Theme::ink(c), 2, juce::Justification::topLeft);
        }
        g.setColour(col);
        for (int w = 0; w < 256; ++w) {
            float v = P.wave[(size_t) w].load(); float x = in.getX() + w * in.getWidth() / 256.0f, h = v * in.getHeight() * 0.45f;
            g.fillRect(x, in.getCentreY() - h, juce::jmax(1.0f, in.getWidth() / 256.0f - 1.0f), h * 2 + 1);
        }
    }
    // the sequence: one dot per step
    if (st == 3) {
        int step = P.stepOut.load();
        drawCaption(g, "SEQUENCE", { waveR.getX() + 22, waveR.getBottom() - 30, 100, 20 });
        int nsteps = (int) shell.host.model.get("phr_steps"); if (nsteps <= 0) nsteps = juce::jmax(1, n);
        nsteps = juce::jmin(32, nsteps);
        for (int k = 0; k < nsteps; ++k) { g.setColour(k == step ? col : Theme::line()); g.fillEllipse((float) waveR.getX() + 130 + k * 16, (float) waveR.getBottom() - 24, 9, 9); }
    }
    drawCaption(g, "INPUT", { src->getX(), src->getY() - 22, 200, 18 });
    drawCaption(g, "RECORD", { len->getX(), len->getY() - 22, 200, 18 });
    drawCaption(g, Theme::softUI() ? "HOW FAST  ·  ON THE METRONOME" : "HOW FAST (on the metronome)", { rate->getX() + (Theme::softUI() ? 2 : 0), rate->getY() - 22, 400, 18 });
    drawCaption(g, "IN WHAT ORDER", { order->getX(), order->getY() - 22, 400, 18 });
    if (!knobs.empty()) drawCaption(g, "SHAPE", { 18, knobs[0]->getY() - 22, 200, 18 });
    drawPanel(g, buildR.toFloat(), 18);
    say(g, Theme::softUI() ? "Build" : "BUILD", { buildR.getX() + 20, buildR.getY() + 14, 200, 22 }, Theme::softUI() ? 12.0f : 10.5f, Theme::txt(), 2);
    float bp = P.buildOut.load();
    auto bar = juce::Rectangle<float>((float) buildR.getX() + 20, (float) buildR.getY() + 122, (float) buildR.getWidth() - 40, 10);
    g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 5); g.setColour(Theme::amber()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * bp), 5);
    say(g, juce::String::fromUTF8("faster · louder · brighter · more space - then it drops back"), { buildR.getX() + 20, buildR.getBottom() - 40, buildR.getWidth() - 40, 20 }, 7.8f, Theme::sub(), 1);
}

} // namespace arco
