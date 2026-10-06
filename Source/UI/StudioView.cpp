#include "StudioView.h"
#include "MainView.h"

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void txt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

StudioView::StudioView(Canvas& c) : studio(c.host.studio), cv(c), juce::Thread("ARCO studio job") {
    setWantsKeyboardFocus(true);
    struct B { const char* t; std::function<void()> f; };
    auto& st = studio;
    std::vector<B> bs = {
        { "PLAY", [this] { studio.playing = !studio.playing.load(); grabKeyboardFocus(); } },
        { U8("⏮").toRawUTF8(), [this] { studio.pos = 0; scrollSec = 0; repaint(); } },
        { U8("−").toRawUTF8(), [this] { pxPerSec = juce::jmax(4.0, pxPerSec / 1.5); repaint(); } },
        { "+", [this] { pxPerSec = juce::jmin(800.0, pxPerSec * 1.5); repaint(); } },
        { "SPLIT", [this] { split(); } },
        { "DUPLICATE", [this] { duplicate(); } },
        { "DELETE", [this] { removeClip(); } },
        { "UNDO", [this, &st] { if (st.undo()) { selClip = -1; buildPeaks(); repaint(); } } },
        { "REDO", [this, &st] { if (st.redo()) { selClip = -1; buildPeaks(); repaint(); } } },
        { "PRINT FX", [this] { if (!isThreadRunning()) { job = JPrint; jobLane = selLane; progress = 0.01f; jobDone = false; startThread(); } } },
        { "REBUILD", [this] { juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Rebuild from the station?", "The song is laid out again from the loops as they are now (your STUDIO edits go).", "Rebuild", "Cancel", nullptr,
              juce::ModalCallbackFunction::create([this](int r) { if (r == 1) { juce::String why; if (studio.buildFromStation(4, why)) { buildPeaks(); selClip = -1; layoutInspector(); repaint(); } else cv.showMessage(why); } })); } },
        { "SAVE", [this] { if (cv.host.songFolder.isDirectory()) { cv.host.saveSong(cv.host.songFolder); cv.showMessage(studio.save(cv.host.songFolder) ? U8("STUDIO saved with the song ✓") : U8("could not save")); } else cv.showMessage("save the song on the station first (it needs a name)"); } },
        { "EXPORT", [this] { doExport(); } },
        { U8("✕ CLOSE").toRawUTF8(), [this] { studio.playing = false; if (onClose) onClose(); } },
    };
    for (auto& b : bs) {
        juce::String t = juce::String::fromUTF8(b.t);
        auto* p = add<Pill>(t, [t, this] { if (t == "PLAY") return studio.playing.load() ? Theme::green() : Theme::btn(); if (t == "EXPORT") return Theme::sel(); return Theme::btn(); }, 8.5f, 7.0f);
        if (t == "PLAY") p->dynText = [this] { return studio.playing.load() ? juce::String(U8("■ STOP")) : juce::String(U8("▶ PLAY")); };
        p->onClick = b.f;
    }
    // the inspector: the chosen lane, the chosen clip, the master
    auto lane = [this]() -> StudioLane* { auto& L = studio.song.lanes; return selLane >= 0 && selLane < (int) L.size() ? &L[(size_t) selLane] : nullptr; };
    struct K { const char* name; float StudioLane::* f; double lo, hi, def; };
    K ks[] = { { "VOLUME", &StudioLane::vol, -40, 6, 0 }, { "PAN", &StudioLane::pan, -1, 1, 0 }, { "HIGH-PASS", &StudioLane::hp, 0, 300, 0 }, { "LOW", &StudioLane::lo, -12, 12, 0 },
               { "MID", &StudioLane::mid, -12, 12, 0 }, { "MID FREQ", &StudioLane::midHz, 150, 6000, 1000 }, { "HIGH", &StudioLane::hi, -12, 12, 0 }, { "COMPRESS", &StudioLane::comp, 0, 100, 0 } };
    for (auto& k : ks) {
        auto f = k.f;
        auto* kn = new Knob([lane, f] { auto* L = lane(); return L ? (double) (L->*f) : 0.0; }, [this, lane, f](double v) { const juce::ScopedLock sl(studio.lock); if (auto* L = lane()) L->*f = (float) v; },
                            k.lo, k.hi, k.def, [] { return Theme::blue(); });
        kn->setName(k.name); laneKnobs.add(kn); addAndMakeVisible(kn);
    }
    auto clip = [this]() -> StudioClip* { auto& C = studio.song.clips; return selClip >= 0 && selClip < (int) C.size() ? &C[(size_t) selClip] : nullptr; };
    {
        auto* g = new Knob([clip] { auto* c = clip(); return c ? (double) c->gainDb : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->gainDb = (float) v; }, -24, 12, 0, [] { return Theme::amber(); });
        g->setName("CLIP GAIN"); clipKnobs.add(g); addAndMakeVisible(g);
        auto* fi = new Knob([this, clip] { auto* c = clip(); return c ? c->fadeIn / studio.song.sr * 1000.0 : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->fadeIn = (float) juce::jmin((double) c->length / 2, v / 1000.0 * studio.song.sr); }, 0, 4000, 4, [] { return Theme::amber(); });
        fi->setName("FADE IN ms"); clipKnobs.add(fi); addAndMakeVisible(fi);
        auto* fo = new Knob([this, clip] { auto* c = clip(); return c ? c->fadeOut / studio.song.sr * 1000.0 : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->fadeOut = (float) juce::jmin((double) c->length / 2, v / 1000.0 * studio.song.sr); }, 0, 4000, 4, [] { return Theme::amber(); });
        fo->setName("FADE OUT ms"); clipKnobs.add(fo); addAndMakeVisible(fo);
    }
    {
        auto* mv = new Knob([this] { return (double) studio.song.masterDb; }, [this](double v) { studio.song.masterDb = (float) v; }, -24, 12, 0, [] { return Theme::txt(); });
        mv->setName("MASTER"); masterKnobs.add(mv); addAndMakeVisible(mv);
        auto* gl = new Knob([this] { return (double) studio.song.glue; }, [this](double v) { studio.song.glue = (float) v; }, 0, 100, 20, [] { return Theme::amber(); });
        gl->setName("GLUE"); masterKnobs.add(gl); addAndMakeVisible(gl);
    }
    startTimerHz(30);
}
StudioView::~StudioView() { stopThread(8000); }

void StudioView::open() {
    if (studio.song.clips.empty()) {
        bool loaded = cv.host.songFolder.isDirectory() && studio.load(cv.host.songFolder);
        juce::String why;
        if (!loaded && !studio.buildFromStation(4, why)) { cv.showMessage(why); }
    }
    buildPeaks(); layoutInspector(); grabKeyboardFocus(); repaint();
}

void StudioView::buildPeaks() {
    const juce::ScopedLock sl(studio.lock);
    peaks.clear();
    for (auto& s : studio.song.sources) {
        std::vector<float> p((size_t) (s.size() / 256 + 1), 0.0f);
        for (long i = 0; i < s.size(); ++i) { float v = std::abs(s.l[(size_t) i]); if (!s.r.empty()) v = juce::jmax(v, std::abs(s.r[(size_t) i])); auto& q = p[(size_t) (i / 256)]; q = juce::jmax(q, v); }
        peaks.push_back(std::move(p));
    }
}

juce::Rectangle<int> StudioView::timeline() const {
    return { (int) headW, (int) (topH + rulerH), getWidth() - (int) headW, getHeight() - (int) (topH + rulerH + inspH) };
}

void StudioView::resized() {
    int x = 120;
    for (int i = 0; i < kids.size(); ++i) {
        auto* p = kids[i];
        int w = i == 0 ? 92 : (i <= 3 ? 34 : 84);
        if (i == kids.size() - 1) { p->setBounds(getWidth() - 104, 14, 92, 30); continue; }
        p->setBounds(x, 14, w, 30); x += w + 6;
        if (i == 3) x += 14;
    }
    layoutInspector();
}

void StudioView::layoutInspector() {
    int y = getHeight() - (int) inspH + 40;
    for (int i = 0; i < laneKnobs.size(); ++i) laneKnobs[i]->setBounds(24 + i * 76, y, 44, 44);
    bool hasClip = selClip >= 0 && selClip < (int) studio.song.clips.size();
    for (int i = 0; i < clipKnobs.size(); ++i) { clipKnobs[i]->setBounds(24 + 8 * 76 + 40 + i * 76, y, 44, 44); clipKnobs[i]->setVisible(hasClip); }
    for (int i = 0; i < masterKnobs.size(); ++i) masterKnobs[i]->setBounds(getWidth() - 190 + i * 86, y, 44, 44);
}

long StudioView::snapTo(long s, bool fine) const {
    double beat = 60.0 / studio.song.bpm * studio.song.sr;
    double grid = fine ? beat / 4 : beat;
    if (pxPerSec < 20) grid = beat * studio.song.beats;
    return (long) std::llround(std::round(s / grid) * grid);
}

void StudioView::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    auto& S = studio.song;
    const juce::ScopedLock sl(studio.lock);
    txt(g, "STUDIO", { 20, 12, 100, 34 }, 17.0f, Theme::txt(), 2);
    // ruler
    auto tl = timeline();
    double barSec = 60.0 / S.bpm * S.beats;
    g.setColour(Theme::card()); g.fillRect(0, (int) topH, getWidth(), (int) rulerH);
    int every = pxPerSec * barSec < 30 ? 4 : 1;
    for (int b = (int) std::floor(scrollSec / barSec); ; ++b) {
        float x = xAt(b * barSec); if (x > getWidth()) break; if (x < tl.getX()) continue;
        g.setColour(Theme::line()); g.fillRect(x, (float) tl.getY(), 1.0f, (float) tl.getHeight());
        if (b % every == 0) txt(g, juce::String(b + 1), { (int) x + 3, (int) topH + 4, 40, 18 }, 8.0f, Theme::sub(), 1);
    }
    // lanes
    for (int li = 0; li < (int) S.lanes.size(); ++li) {
        auto& L = S.lanes[(size_t) li];
        float y = tl.getY() + li * laneH;
        if (y > tl.getBottom()) break;
        g.setColour(li == selLane ? Theme::card2() : Theme::card()); g.fillRect(0.0f, y, headW - 2, laneH - 2);
        g.setColour(Theme::strip(L.colour)); g.fillRect(0.0f, y, 5.0f, laneH - 2);
        txt(g, L.name, { 14, (int) y + 6, (int) headW - 100, 20 }, 10.0f, Theme::txt(), 2);
        auto pill = [&](juce::Rectangle<int> r, const juce::String& t, bool on, juce::Colour c) { g.setColour(on ? c : Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 5.0f); txt(g, t, r, 8.0f, Theme::textOn(on ? c : Theme::btn()), 2, juce::Justification::centred); };
        pill({ (int) headW - 80, (int) y + 6, 32, 20 }, "M", L.mute, Theme::red());
        pill({ (int) headW - 44, (int) y + 6, 32, 20 }, "S", L.solo, Theme::blue());
        // volume bar
        auto vb = juce::Rectangle<float>(14, y + 34, headW - 30, 10);
        g.setColour(Theme::scr()); g.fillRoundedRectangle(vb, 5.0f);
        g.setColour(Theme::strip(L.colour)); g.fillRoundedRectangle(vb.withWidth(vb.getWidth() * (L.vol + 40.0f) / 46.0f), 5.0f);
        txt(g, juce::String(L.vol, 1) + " dB" + (std::abs(L.pan) > 0.02f ? (L.pan < 0 ? "  L" : "  R") + juce::String((int) std::abs(L.pan * 100)) : juce::String()), { 14, (int) y + 48, (int) headW - 30, 16 }, 7.5f, Theme::sub());
        g.setColour(li % 2 ? Theme::bg() : Theme::bg().brighter(0.02f)); g.fillRect((float) tl.getX(), y, (float) tl.getWidth(), laneH - 2);
    }
    // clips
    for (int ci = 0; ci < (int) S.clips.size(); ++ci) {
        auto& c = S.clips[(size_t) ci];
        float y = tl.getY() + c.lane * laneH + 3;
        float x0 = xAt(c.start / S.sr), x1 = xAt((c.start + c.length) / S.sr);
        if (x1 < tl.getX() || x0 > getWidth()) continue;
        auto r = juce::Rectangle<float>(juce::jmax(x0, (float) tl.getX()), y, juce::jmin(x1, (float) getWidth()) - juce::jmax(x0, (float) tl.getX()), laneH - 8);
        auto col = Theme::strip(S.lanes[(size_t) juce::jlimit(0, (int) S.lanes.size() - 1, c.lane)].colour);
        g.setColour(col.withAlpha(ci == selClip ? 0.55f : 0.32f)); g.fillRoundedRectangle(r, 6.0f);
        if (ci == selClip) { g.setColour(Theme::txt()); g.drawRoundedRectangle(r, 6.0f, 1.5f); }
        // the wave
        if (c.source >= 0 && c.source < (int) peaks.size()) {
            auto& pk = peaks[(size_t) c.source]; long sl2 = S.sources[(size_t) c.source].size();
            float g0 = std::pow(10.0f, c.gainDb / 20.0f);
            g.setColour(Theme::arc(col));
            for (float x = r.getX(); x < r.getRight(); x += 1.0f) {
                long smp = (long) ((secAt(x) * S.sr) - c.start) + c.offset;
                long si = c.loop ? (sl2 > 0 ? smp % sl2 : 0) : smp;
                if (si < 0 || si >= sl2) continue;
                float v = juce::jmin(1.0f, pk[(size_t) (si / 256)] * g0);
                float h = v * (r.getHeight() - 18) * 0.5f;
                g.fillRect(x, r.getCentreY() + 6 - h, 1.0f, juce::jmax(1.0f, 2 * h));
            }
            // loop seams
            if (c.loop && sl2 > 0) for (long k = sl2 - c.offset; k < c.length; k += sl2) { float x = xAt((c.start + k) / S.sr); if (x > r.getX() && x < r.getRight()) { g.setColour(Theme::txt().withAlpha(0.18f)); g.fillRect(x, r.getY() + 16, 1.0f, r.getHeight() - 16); } }
        }
        // fades
        g.setColour(Theme::txt().withAlpha(0.6f));
        if (c.fadeIn > 1) g.drawLine(x0, r.getBottom(), xAt((c.start + c.fadeIn) / S.sr), r.getY() + 2, 1.2f);
        if (c.fadeOut > 1) g.drawLine(xAt((c.start + c.length - c.fadeOut) / S.sr), r.getY() + 2, x1, r.getBottom(), 1.2f);
        txt(g, S.sources[(size_t) juce::jlimit(0, (int) S.sources.size() - 1, c.source)].name + (std::abs(c.gainDb) > 0.05f ? "  " + juce::String(c.gainDb, 1) + " dB" : juce::String()),
            { (int) r.getX() + 6, (int) r.getY() + 2, (int) r.getWidth() - 10, 14 }, 7.5f, Theme::txt(), 1);
    }
    // playhead
    float px = xAt(studio.pos.load() / S.sr);
    if (px >= tl.getX()) { g.setColour(Theme::red()); g.fillRect(px - 1, (float) topH, 2.0f, (float) (tl.getBottom() - topH)); }
    // inspector
    auto ins = juce::Rectangle<int>(0, getHeight() - (int) inspH, getWidth(), (int) inspH);
    g.setColour(Theme::card()); g.fillRect(ins);
    auto lab = [&](Knob* k) { auto b = k->getBounds(); txt(g, k->getName(), { b.getX() - 18, b.getBottom() + 2, b.getWidth() + 36, 14 }, 7.5f, Theme::sub(), 1, juce::Justification::centred); };
    if (selLane >= 0 && selLane < (int) S.lanes.size()) txt(g, S.lanes[(size_t) selLane].name + U8("  ·  the lane"), { 24, ins.getY() + 10, 400, 18 }, 9.5f, Theme::txt(), 2);
    for (auto* k : laneKnobs) lab(k);
    if (selClip >= 0 && selClip < (int) S.clips.size()) { txt(g, U8("the clip  ·  drag its edges to trim, its top corners for fades, S splits, ⌘D duplicates"), { 24 + 8 * 76 + 40, ins.getY() + 10, 640, 18 }, 9.0f, Theme::txt(), 2); for (auto* k : clipKnobs) lab(k); }
    txt(g, "MASTER", { getWidth() - 190, ins.getY() + 10, 160, 18 }, 9.5f, Theme::txt(), 2);
    for (auto* k : masterKnobs) lab(k);
    double sec = studio.pos.load() / S.sr; double bars = sec / barSec;
    txt(g, "bar " + juce::String((int) bars + 1) + "." + juce::String((int) ((bars - std::floor(bars)) * S.beats) + 1) + U8("  ·  ") + juce::String((int) sec / 60) + ":" + juce::String((int) sec % 60).paddedLeft('0', 2)
        + U8("  ·  ") + juce::String(S.bpm, 1) + " BPM", { getWidth() - 470, 18, 250, 22 }, 9.0f, Theme::sub(), 1, juce::Justification::centredRight);
    if (isThreadRunning() || (progress > 0 && progress < 1)) {
        auto bar = juce::Rectangle<float>(20, topH - 6, getWidth() - 40.0f, 4);
        g.setColour(Theme::card2()); g.fillRect(bar); g.setColour(Theme::green()); g.fillRect(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())));
    }
    if (report.isNotEmpty()) txt(g, report, { 24, ins.getBottom() - 26, getWidth() - 260, 18 }, 8.0f, Theme::green(), 1);
    if (S.clips.empty()) txt(g, U8("Nothing here yet: record loops on the station, then open STUDIO again (or press REBUILD). Drop audio files on a lane to add them."), tl.reduced(40), 11.0f, Theme::sub(), 0, juce::Justification::centred);
}

void StudioView::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto p = e.position; downAt = p; drag = None;
    auto& S = studio.song;
    auto tl = timeline();
    if (p.y >= topH && p.y < topH + rulerH && p.x >= tl.getX()) { drag = Seek; studio.pos = (long) juce::jmax(0.0, secAt(p.x) * S.sr); repaint(); return; }
    int li = laneAt(p.y);
    if (p.x < headW && li >= 0 && li < (int) S.lanes.size()) {
        selLane = li; auto& L = S.lanes[(size_t) li];
        float y = tl.getY() + li * laneH;
        if (juce::Rectangle<float>(headW - 80, y + 6, 32, 20).contains(p)) L.mute = !L.mute;
        else if (juce::Rectangle<float>(headW - 44, y + 6, 32, 20).contains(p)) L.solo = !L.solo;
        else if (p.y > y + 30 && p.y < y + 48) { const juce::ScopedLock sl(studio.lock); L.vol = juce::jlimit(-40.0f, 6.0f, (p.x - 14) / (headW - 30) * 46.0f - 40.0f); drag = Gain; dragLane = li; }
        repaint(); return;
    }
    if (!tl.contains(p.toInt())) return;
    // a clip?
    selClip = -1;
    for (int ci = (int) S.clips.size(); --ci >= 0;) {
        auto& c = S.clips[(size_t) ci];
        if (c.lane != li) continue;
        float x0 = xAt(c.start / S.sr), x1 = xAt((c.start + c.length) / S.sr);
        if (p.x < x0 - 4 || p.x > x1 + 4) continue;
        selClip = ci; selLane = li;
        studio.snapshot();
        float y = tl.getY() + li * laneH + 3;
        bool top = p.y < y + 14;
        dragStart = c.start; dragLen = c.length; dragOff = c.offset; dragLane = c.lane;
        if (top && p.x < x0 + 12) { drag = FadeIn; dragFade = c.fadeIn; }
        else if (top && p.x > x1 - 12) { drag = FadeOut; dragFade = c.fadeOut; }
        else if (p.x < x0 + 6) drag = TrimL;
        else if (p.x > x1 - 6) drag = TrimR;
        else drag = Move;
        break;
    }
    if (selClip < 0 && li >= 0 && li < (int) S.lanes.size()) { selLane = li; studio.pos = (long) juce::jmax(0.0, secAt(p.x) * S.sr); }
    layoutInspector(); repaint();
}

void StudioView::mouseDrag(const juce::MouseEvent& e) {
    auto& S = studio.song;
    double dsec = (e.position.x - downAt.x) / pxPerSec;
    long d = (long) (dsec * S.sr);
    bool fine = e.mods.isShiftDown();
    const juce::ScopedLock sl(studio.lock);
    if (drag == Seek) { studio.pos = (long) juce::jmax(0.0, secAt(e.position.x) * S.sr); }
    else if (drag == Gain && dragLane < (int) S.lanes.size()) S.lanes[(size_t) dragLane].vol = juce::jlimit(-40.0f, 6.0f, (e.position.x - 14) / (headW - 30) * 46.0f - 40.0f);
    else if (selClip >= 0 && selClip < (int) S.clips.size()) {
        auto& c = S.clips[(size_t) selClip];
        if (drag == Move) {
            c.start = juce::jmax(0L, e.mods.isAltDown() ? dragStart + d : snapTo(dragStart + d, fine));
            int li = laneAt(e.position.y); if (li >= 0 && li < (int) S.lanes.size()) c.lane = li;
        } else if (drag == TrimL) {
            long ns = juce::jlimit(0L, dragStart + dragLen - 256, e.mods.isAltDown() ? dragStart + d : snapTo(dragStart + d, fine));
            long delta = ns - dragStart; c.start = ns; c.length = dragLen - delta; c.offset = juce::jmax(0L, dragOff + delta);
            if (!c.loop && c.source < (int) S.sources.size()) c.length = juce::jmin(c.length, S.sources[(size_t) c.source].size() - c.offset);
        } else if (drag == TrimR) {
            long ne = juce::jmax(dragStart + 256, e.mods.isAltDown() ? dragStart + dragLen + d : snapTo(dragStart + dragLen + d, fine));
            c.length = ne - dragStart;
            if (!c.loop && c.source < (int) S.sources.size()) c.length = juce::jmin(c.length, S.sources[(size_t) c.source].size() - c.offset);
        } else if (drag == FadeIn) c.fadeIn = (float) juce::jlimit(0.0, (double) c.length * 0.5, (double) dragFade + d);
        else if (drag == FadeOut) c.fadeOut = (float) juce::jlimit(0.0, (double) c.length * 0.5, (double) dragFade - d);
    }
    repaint();
}
void StudioView::mouseUp(const juce::MouseEvent&) { drag = None; }
void StudioView::mouseDoubleClick(const juce::MouseEvent& e) {
    // double-click a clip: loop on / off (a loop repeats; off plays the take once)
    if (selClip >= 0 && selClip < (int) studio.song.clips.size() && timeline().contains(e.getPosition())) {
        studio.snapshot(); const juce::ScopedLock sl(studio.lock); auto& c = studio.song.clips[(size_t) selClip]; c.loop = !c.loop;
        cv.showMessage(c.loop ? "clip: loops" : "clip: plays once"); repaint();
    }
}
void StudioView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { pxPerSec = juce::jlimit(4.0, 800.0, pxPerSec * (w.deltaY > 0 ? 1.15 : 1 / 1.15)); }
    else scrollSec = juce::jmax(0.0, scrollSec - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 600.0 / pxPerSec);
    repaint();
}

bool StudioView::keyPressed(const juce::KeyPress& k) {
    auto c = k.getKeyCode(); bool cmd = k.getModifiers().isCommandDown();
    if (c == juce::KeyPress::spaceKey) { studio.playing = !studio.playing.load(); return true; }
    if (c == 'S' && !cmd) { split(); return true; }
    if (c == 'D' && cmd) { duplicate(); return true; }
    if (c == 'Z' && cmd && k.getModifiers().isShiftDown()) { if (studio.redo()) { selClip = -1; buildPeaks(); repaint(); } return true; }
    if (c == 'Z' && cmd) { if (studio.undo()) { selClip = -1; buildPeaks(); repaint(); } return true; }
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) { removeClip(); return true; }
    if (c == juce::KeyPress::homeKey || c == juce::KeyPress::returnKey) { studio.pos = 0; scrollSec = 0; repaint(); return true; }
    if (c == juce::KeyPress::escapeKey) { studio.playing = false; if (onClose) onClose(); return true; }
    if (c == '=' || c == '+') { pxPerSec = juce::jmin(800.0, pxPerSec * 1.5); repaint(); return true; }
    if (c == '-') { pxPerSec = juce::jmax(4.0, pxPerSec / 1.5); repaint(); return true; }
    return false;
}

void StudioView::split() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) { cv.showMessage("choose a clip first"); return; }
    long at = studio.pos.load();
    auto c = S.clips[(size_t) selClip];
    if (at <= c.start + 64 || at >= c.start + c.length - 64) { cv.showMessage("put the playhead inside the clip (click the ruler), then SPLIT"); return; }
    studio.snapshot();
    const juce::ScopedLock sl(studio.lock);
    auto& a = S.clips[(size_t) selClip];
    StudioClip b = a;
    long firstLen = at - a.start;
    a.length = firstLen; a.fadeOut = (float) (0.002 * S.sr);
    b.start = at; b.length = c.length - firstLen; b.offset = c.offset + firstLen; b.fadeIn = (float) (0.002 * S.sr);
    S.clips.push_back(b);
    repaint();
}
void StudioView::duplicate() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) return;
    studio.snapshot();
    const juce::ScopedLock sl(studio.lock);
    StudioClip b = S.clips[(size_t) selClip]; b.start += b.length;
    S.clips.push_back(b); selClip = (int) S.clips.size() - 1;
    repaint();
}
void StudioView::removeClip() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) return;
    studio.snapshot();
    const juce::ScopedLock sl(studio.lock);
    S.clips.erase(S.clips.begin() + selClip); selClip = -1;
    layoutInspector(); repaint();
}

void StudioView::filesDropped(const juce::StringArray& files, int x, int y) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    int li = juce::jlimit(0, juce::jmax(0, (int) studio.song.lanes.size() - 1), laneAt((float) y));
    long at = juce::jmax(0L, snapTo((long) (secAt((float) x) * studio.song.sr), false));
    studio.snapshot();
    for (auto& f : files) {
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(f)));
        if (!r) continue;
        juce::AudioBuffer<float> b((int) juce::jmin(2u, r->numChannels), (int) juce::jmin((juce::int64) (r->sampleRate * 600), r->lengthInSamples));
        r->read(&b, 0, b.getNumSamples(), 0, true, true);
        auto rb = analysis::resample(b, r->sampleRate, studio.song.sr);
        StudioSource S; S.name = juce::File(f).getFileNameWithoutExtension();
        S.l.assign(rb.getReadPointer(0), rb.getReadPointer(0) + rb.getNumSamples());
        if (rb.getNumChannels() > 1) S.r.assign(rb.getReadPointer(1), rb.getReadPointer(1) + rb.getNumSamples());
        const juce::ScopedLock sl(studio.lock);
        if (studio.song.lanes.empty()) { StudioLane L; L.name = "AUDIO"; studio.song.lanes.push_back(L); studio.laneStrip.push_back(-1); studio.laneScene.push_back(1); }
        studio.song.sources.push_back(std::move(S));
        StudioClip c; c.lane = li; c.source = (int) studio.song.sources.size() - 1; c.start = at; c.length = (long) studio.song.sources.back().size(); c.loop = false;
        c.fadeIn = c.fadeOut = (float) (0.004 * studio.song.sr);
        studio.song.clips.push_back(c); at += c.length;
    }
    buildPeaks(); repaint();
}

void StudioView::doExport() {
    if (isThreadRunning()) return;
    juce::PopupMenu pm;
    pm.addItem(1, U8("WAV 24-bit 48 kHz  ·  streaming −14 LUFS"));
    pm.addItem(2, U8("WAV 24-bit 48 kHz  ·  as it is (peaks at −1 dB)"));
    pm.addItem(3, U8("FLAC 24-bit 48 kHz  ·  streaming −14 LUFS"));
    pm.addItem(4, U8("WAV 16-bit 44.1 kHz  ·  CD, −14 LUFS"));
    pm.addItem(5, U8("WAV 32-bit float 96 kHz  ·  archive, as it is"));
    pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(kids[12]), [this](int r) {
        if (r <= 0) return;
        ExportOptions o; o.bits = r == 4 ? 16 : (r == 5 ? 32 : 24); o.flac = r == 3; o.sampleRate = r == 4 ? 44100 : (r == 5 ? 96000 : 48000); o.loudness = (r == 2 || r == 5) ? 0 : 1;
        exportOpt = o;
        juce::String name = (cv.host.songFolder.isDirectory() ? cv.host.songFolder.getFileName() : juce::String("ARCO studio")) + " (studio)";
        juce::String ext = o.flac ? ".flac" : ".wav";
       #if JUCE_IOS
        auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Exports"); d.createDirectory();
        exportFile = d.getChildFile(name + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ext);
        job = JExport; progress = 0.01f; jobDone = false; startThread();
       #else
        chooser = std::make_unique<juce::FileChooser>("Export the STUDIO mix as", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(name + ext), "*" + ext);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | juce::FileBrowserComponent::canSelectFiles,
            [this, ext](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f == juce::File()) return; exportFile = f.withFileExtension(ext.substring(1)); job = JExport; progress = 0.01f; jobDone = false; startThread(); });
       #endif
    });
}

void StudioView::run() {
    if (job == JExport) {
        juce::AudioBuffer<float> b;
        if (!studio.renderAll(b, [this](float p) { progress = p * 0.8f; return !threadShouldExit(); })) { report = "export cancelled"; jobDone = true; return; }
        juce::String rep;
        bool ok = Renderer::write(b, studio.song.sr, exportOpt, exportFile, rep);
        report = ok ? U8("✓ ") + exportFile.getFileName() + "   " + rep : rep;
        progress = 1.0f;
    } else if (job == JPrint) {
        bool ok = studio.printEffects(jobLane, [this](float p) { progress = p; return !threadShouldExit(); });
        report = ok ? U8("✓ the lane now plays with the station's effects (UNDO brings back the dry sound)") : U8("this lane has no track on the station to print");
        progress = 1.0f;
    }
    jobDone = true;
}

void StudioView::timerCallback() {
    if (jobDone.exchange(false)) {
        if (job == JPrint) buildPeaks();
        if (job == JExport && exportFile.existsAsFile()) {
           #if JUCE_IOS
            juce::ContentSharer::shareFilesScoped({ juce::URL(exportFile) }, [](bool, const juce::String&) {});
           #else
            exportFile.revealToUser();
           #endif
        }
        job = JNone;
    }
    if (!isShowing()) return;
    // follow the playhead
    if (studio.playing.load()) {
        double sec = studio.pos.load() / studio.song.sr, visible = timeline().getWidth() / pxPerSec;
        if (sec > scrollSec + visible * 0.9 || sec < scrollSec) scrollSec = juce::jmax(0.0, sec - visible * 0.1);
    }
    repaint();
}

} // namespace arco
