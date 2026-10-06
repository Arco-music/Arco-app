#include "Editors.h"

namespace score {

static Project& P() { return *host().project; }

// ======================================================================== scales
const std::vector<std::pair<juce::String, std::vector<int>>>& Scales::all() {
    static const std::vector<std::pair<juce::String, std::vector<int>>> s = {
        { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } }, { "Major", { 0, 2, 4, 5, 7, 9, 11 } }, { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } }, { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } }, { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } }, { "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } }, { "Melodic minor", { 0, 2, 3, 5, 7, 9, 11 } },
        { "Pentatonic major", { 0, 2, 4, 7, 9 } }, { "Pentatonic minor", { 0, 3, 5, 7, 10 } }, { "Blues", { 0, 3, 5, 6, 7, 10 } },
        { "Whole tone", { 0, 2, 4, 6, 8, 10 } }, { "Hirajoshi", { 0, 2, 3, 7, 8 } }, { "Double harmonic", { 0, 1, 4, 5, 7, 8, 11 } } };
    return s;
}
bool Scales::inScale(int pitch, int key, int scale) {
    auto& s = all()[(size_t) juce::jlimit(0, (int) all().size() - 1, scale)].second;
    int d = ((pitch - key) % 12 + 12) % 12;
    return std::find(s.begin(), s.end(), d) != s.end();
}
int Scales::snapToScale(int pitch, int key, int scale) {
    for (int d = 0; d < 12; ++d) { if (inScale(pitch - d, key, scale)) return pitch - d; if (inScale(pitch + d, key, scale)) return pitch + d; }
    return pitch;
}
juce::String Scales::keyName(int k) { static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return N[((k % 12) + 12) % 12]; }

static bool isBlack(int p) { int n = p % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

// ======================================================================== piano roll
PianoRoll::PianoRoll() { setWantsKeyboardFocus(true); onLong = [this](juce::Point<int>) { drawMode = !drawMode; host().say(drawMode ? "DRAW: click to add notes" : "SELECT: drag a box around notes"); repaint(); }; }

Clip* PianoRoll::clip() const { Clip* c = P().clip(clipId); return c && c->midi ? c : nullptr; }
Track* PianoRoll::owner() const { Track* t = nullptr; P().clip(clipId, &t); return t; }
bool PianoRoll::drums() const { auto* t = owner(); return t && t->inst && t->inst->type == "drm"; }

void PianoRoll::setClip(Uid id) {
    if (id != clipId) { clipId = id; sel.clear(); zoomToFit(); }
    repaint();
}

void PianoRoll::zoomToFit() {
    auto* c = clip(); if (!c || getWidth() <= keysW) return;
    ppb = juce::jlimit(8.0, 600.0, (getWidth() - keysW - 30) / std::max(1.0, c->length));
    scrollBeat = 0;
    if (!drums() && !fold) {
        int lo = 127, hi = 0; for (auto& n : c->notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
        int rows = (int) ((getHeight() - rulerH - velH) / rowH);
        if (lo > hi) { lo = 55; hi = 76; }
        scrollPitch = (float) juce::jlimit(rows - 1, 127, (hi + lo) / 2 + rows / 2);
    }
}

std::vector<int> PianoRoll::rowsShown() const {
    std::vector<int> r;
    if (drums()) { for (int p = 51; p >= 36; --p) r.push_back(p); return r; }
    if (fold) { std::set<int> s; if (auto* c = clip()) for (auto& n : c->notes) s.insert(n.pitch);
        if (s.empty()) return r; for (auto it = s.rbegin(); it != s.rend(); ++it) r.push_back(*it); return r; }
    return r;
}
int PianoRoll::pitchAtY(float y) const {
    auto rows = rowsShown();
    float rh = rows.empty() ? rowH : juce::jmax(10.0f, (float) (getHeight() - rulerH - velH) / (float) rows.size());
    int idx = (int) std::floor((y - rulerH) / rh);
    if (!rows.empty()) return idx >= 0 && idx < (int) rows.size() ? rows[(size_t) idx] : -1;
    return juce::jlimit(0, 127, (int) scrollPitch - idx);
}
float PianoRoll::yOfPitch(int p) const {
    auto rows = rowsShown();
    if (!rows.empty()) { float rh = juce::jmax(10.0f, (float) (getHeight() - rulerH - velH) / (float) rows.size());
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i] == p) return rulerH + rh * (float) i; return -1000.0f; }
    return rulerH + (scrollPitch - (float) p) * rowH;
}
juce::Rectangle<float> PianoRoll::noteRect(const Note& n) const {
    auto rows = rowsShown();
    float rh = rows.empty() ? rowH : juce::jmax(10.0f, (float) (getHeight() - rulerH - velH) / (float) rows.size());
    float x0 = xOf(n.start), x1 = xOf(n.start + n.len);
    return { x0, yOfPitch(n.pitch) + 1, juce::jmax(3.0f, x1 - x0 - 1), rh - 2 };
}
int PianoRoll::hitNote(juce::Point<float> p, int& part) const {
    auto* c = clip(); if (!c) return -1;
    for (int i = (int) c->notes.size() - 1; i >= 0; --i) {
        auto r = noteRect(c->notes[(size_t) i]);
        if (r.expanded(0, 0.5f).contains(p)) { float e = juce::jmin(7.0f, r.getWidth() / 3);
            part = p.x > r.getRight() - e ? 1 : (p.x < r.getX() + e * 0.7f && r.getWidth() > 14 ? 2 : 0); return i; }
    }
    return -1;
}

void PianoRoll::resized() {}

void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(Theme::scr());
    auto* c = clip();
    if (!c) { label(g, "select a notes clip (or double-click an instrument track's lane to make one)", getLocalBounds(), 10.0f, 1, Theme::sub(), juce::Justification::centred); return; }
    auto* t = owner(); auto col = Theme::argb(c->colour ? c->colour : (t ? t->colour : 0xff8fa8d8));
    auto& p = P(); bool dr = drums(); auto rows = rowsShown(); bool listed = !rows.empty() || dr || fold;
    float rh = listed && !rows.empty() ? juce::jmax(10.0f, (float) (getHeight() - rulerH - velH) / (float) rows.size()) : rowH;
    int noteBottom = getHeight() - velH;
    // rows
    auto drawRow = [&](int pitch, float y) {
        bool black = isBlack(pitch), inSc = Scales::inScale(pitch, p.key, p.scale);
        juce::Colour rc = black ? Theme::bg() : Theme::panel();
        if (p.scale > 0 && !dr) rc = inSc ? rc.brighter(0.04f) : rc.darker(0.25f);
        g.setColour(rc); g.fillRect((float) keysW, y, (float) getWidth() - keysW, rh);
        if (pitch % 12 == p.key % 12 && p.scale > 0 && !dr) { g.setColour(Theme::gold().withAlpha(0.06f)); g.fillRect((float) keysW, y, (float) getWidth() - keysW, rh); }
        g.setColour(Theme::line().withAlpha(pitch % 12 == 0 ? 0.7f : 0.25f)); g.drawHorizontalLine((int) (y + rh), (float) keysW, (float) getWidth());
    };
    if (listed) { for (size_t i = 0; i < rows.size(); ++i) drawRow(rows[i], rulerH + rh * (float) i); }
    else for (int pt = (int) scrollPitch; pt >= 0; --pt) { float y = yOfPitch(pt); if (y > noteBottom) break; drawRow(pt, y); }
    // beats
    double gg = triplet ? grid * 2.0 / 3.0 : grid; double bpb = p.beatsPerBar;
    double b0 = std::floor(scrollBeat);
    for (double b = b0; xOf(b) < getWidth(); b += (gg * ppb >= 6 ? gg : 1.0)) {
        float x = xOf(b); if (x < keysW) continue;
        bool bar = std::fmod(b + 1e-9, bpb) < 1e-6, beat = std::fmod(b + 1e-9, 1.0) < 1e-6;
        g.setColour(Theme::line().withAlpha(bar ? 0.9f : beat ? 0.45f : 0.18f)); g.drawVerticalLine((int) x, (float) rulerH, (float) noteBottom);
    }
    // outside the clip
    float xe = xOf(c->length);
    if (xe < getWidth()) { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillRect(xe, (float) rulerH, getWidth() - xe, (float) noteBottom - rulerH); }
    // notes
    g.saveState();
    g.reduceClipRegion(keysW, rulerH, getWidth() - keysW, noteBottom - rulerH);
    for (int i = 0; i < (int) c->notes.size(); ++i) {
        auto& n = c->notes[(size_t) i]; auto r = noteRect(n);
        if (r.getBottom() < rulerH || r.getY() > noteBottom || r.getRight() < keysW || r.getX() > getWidth()) continue;
        bool s = sel.count(i) > 0;
        g.setColour(col.withMultipliedBrightness(0.55f + 0.5f * n.vel).withAlpha(n.start >= c->length ? 0.3f : 1.0f)); g.fillRoundedRectangle(r, 2.5f);
        if (s) { g.setColour(Theme::txt()); g.drawRoundedRectangle(r.reduced(0.5f), 2.5f, 1.4f); }
        if (r.getWidth() > 26 && rh >= 11 && !dr) label(g, juce::MidiMessage::getMidiNoteName(n.pitch, true, true, 4), r.toNearestInt().reduced(3, 0), 7.0f, 1, juce::Colours::black.withAlpha(0.7f));
    }
    if (!band.isEmpty()) { g.setColour(Theme::blue().withAlpha(0.15f)); g.fillRect(band); g.setColour(Theme::blue()); g.drawRect(band, 1.0f); }
    // playhead
    double pos = host().engine.position() - c->start;
    if (pos >= 0 && pos <= c->length) { g.setColour(Theme::green()); g.fillRect(xOf(pos), (float) rulerH, 1.2f, (float) getHeight()); }
    g.restoreState();
    g.saveState();
    // ruler
    g.setColour(Theme::panel()); g.fillRect(keysW, 0, getWidth() - keysW, rulerH);
    for (double b = b0; xOf(b) < getWidth(); b += 1.0) {
        float x = xOf(b); if (x < keysW) continue;
        bool bar = std::fmod(b + 1e-9, bpb) < 1e-6;
        if (bar) label(g, juce::String((int) (b / bpb) + 1), { (int) x + 3, 2, 30, 14 }, 8.0f, 2, Theme::txt());
        g.setColour(Theme::line()); g.drawVerticalLine((int) x, bar ? 4.0f : 12.0f, (float) rulerH);
    }
    { float x = xOf(c->length); juce::Path tri; tri.addTriangle(x - 7, 2, x, 2, x, (float) rulerH - 2); g.setColour(Theme::gold()); g.fillPath(tri); g.fillRect(x - 0.5f, 0.0f, 1.5f, (float) rulerH); }
    // keys
    g.setColour(Theme::card()); g.fillRect(0, 0, keysW, getHeight());
    auto drawKey = [&](int pitch, float y) {
        auto r = juce::Rectangle<float>(0, y, (float) keysW - 2, rh);
        bool held = t && t->inst && t->inst->held[(size_t) pitch].load();
        if (dr) {
            int vi = t && t->inst ? t->inst->find("p" + std::to_string(pitch - 36) + "v")->geti() : 0;
            g.setColour(held ? col : Theme::card2()); g.fillRect(r.reduced(1, 0.5f));
            label(g, Drums::padName(pitch - 36, vi), r.toNearestInt().reduced(4, 0), 7.5f, 1, held ? juce::Colours::white : Theme::txt());
            return;
        }
        g.setColour(held ? col : (isBlack(pitch) ? Theme::bg().darker(0.3f) : Theme::txt().withAlpha(Theme::lightMode() ? 0.15f : 0.85f)));
        g.fillRect(isBlack(pitch) ? r.withTrimmedRight(18) : r.reduced(0, 0.5f));
        if (pitch % 12 == 0 || listed) label(g, juce::MidiMessage::getMidiNoteName(pitch, true, true, 4), r.toNearestInt().withTrimmedLeft(keysW - 30), 7.0f, 2, Theme::lightMode() || isBlack(pitch) ? Theme::sub() : juce::Colours::black.withAlpha(0.6f));
    };
    g.reduceClipRegion(0, rulerH, keysW, noteBottom - rulerH);
    if (listed) { for (size_t i = 0; i < rows.size(); ++i) drawKey(rows[i], rulerH + rh * (float) i); }
    else for (int pt = (int) scrollPitch; pt >= 0; --pt) { float y = yOfPitch(pt); if (y > noteBottom) break; drawKey(pt, y); }
    g.restoreState();
    if (listed && rows.empty()) label(g, "FOLD shows only the notes in the clip: it has none yet", { keysW + 10, rulerH + 10, 400, 16 }, 9.0f, 1);
    // velocity lane
    auto va = velArea();
    g.setColour(Theme::card()); g.fillRect(va); g.setColour(Theme::line()); g.drawHorizontalLine(va.getY(), 0, (float) getWidth());
    label(g, "VELOCITY", { 6, va.getY() + 4, keysW - 8, 12 }, 7.0f, 2);
    for (int i = 0; i < (int) c->notes.size(); ++i) {
        auto& n = c->notes[(size_t) i]; float x = xOf(n.start); if (x < keysW - 2 || x > getWidth()) continue;
        float h = (va.getHeight() - 8) * n.vel;
        g.setColour(sel.count(i) ? Theme::txt() : col); g.fillRect(x, (float) va.getBottom() - 4 - h, 3.0f, h);
        g.fillEllipse(x - 1.5f, (float) va.getBottom() - 4 - h - 3, 6, 6);
    }
    if (!drawMode) label(g, "SELECT", { getWidth() - 60, rulerH + 4, 54, 12 }, 7.5f, 2, Theme::blue(), juce::Justification::centredRight);
}

void PianoRoll::mouseMove(const juce::MouseEvent& e) {
    int part = 0; int h = hitNote(e.position, part);
    setMouseCursor(e.x < keysW ? juce::MouseCursor::PointingHandCursor : h >= 0 ? (part ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                   : (drawMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
}

void PianoRoll::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto* c = clip(); if (!c) return;
    down = e.position; downBeat = beatAt(e.position.x); changed = false; hit = -1; mode = None;
    if (e.x < keysW && e.y > rulerH && e.y < getHeight() - velH) {
        int pt = pitchAtY(e.position.y); if (pt >= 0) { lastKey = pt; host().engine.keysTarget.store(owner() ? owner()->id : 0); host().noteOn(pt, 0.8f); mode = Keys; }
        return;
    }
    if (e.y < rulerH) {
        if (std::abs(e.position.x - xOf(c->length)) < 9) { mode = LoopEnd; host().beginGesture(); return; }
        host().locate(c->start + std::max(0.0, snapB(beatAt(e.position.x), false))); return;
    }
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m; m.addItem(1, drawMode ? "Select tool" : "Draw tool"); m.addItem(2, "Select all"); m.addItem(3, "Delete selected", !sel.empty());
        m.addItem(4, "Quantize"); m.addItem(5, "Legato"); m.addItem(6, "Duplicate the selection after itself", !sel.empty());
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r == 1) drawMode = !drawMode; if (r == 2) selectAll(); if (r == 3) deleteSelected(); if (r == 4) op("quantize"); if (r == 5) op("legato"); if (r == 6) op("dup"); repaint(); });
        return;
    }
    lpDown(e);
    if (velArea().contains(e.getPosition())) { mode = Vel; host().beginGesture(); mouseDrag(e); return; }
    int part = 0; int h = hitNote(e.position, part);
    if (h >= 0) {
        if (e.mods.isShiftDown()) { if (sel.count(h)) sel.erase(h); else sel.insert(h); }
        else if (!sel.count(h)) { sel.clear(); sel.insert(h); }
        host().beginGesture();
        if (e.mods.isAltDown()) {                 // a copy of the selection, which then moves
            std::set<int> ns; for (int i : sel) { c->notes.push_back(c->notes[(size_t) i]); ns.insert((int) c->notes.size() - 1); if (i == h) h = (int) c->notes.size() - 1; }
            sel = ns; changed = true;
        }
        hit = h; mode = part == 1 ? ResizeR : part == 2 ? ResizeL : Move;
        orig = c->notes; downPitch = c->notes[(size_t) h].pitch;
        auto& n = c->notes[(size_t) h];
        if (owner() && mode == Move) { host().engine.keysTarget.store(owner()->id); int pt = n.pitch; host().noteOn(pt, n.vel); juce::Timer::callAfterDelay(160, [pt] { host().noteOff(pt); }); }
        repaint(); return;
    }
    if (drawMode && !e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        int pt = pitchAtY(e.position.y); if (pt < 0) return;
        auto& pr = P();
        if (scaleSnap && !drums()) pt = Scales::snapToScale(pt, pr.key, pr.scale);
        double gg = triplet ? grid * 2.0 / 3.0 : grid;
        double st = gg > 0 ? std::floor(downBeat / gg) * gg : downBeat; if (st < 0) return;
        host().beginGesture();
        std::vector<int> add { pt };
        if (chord > 0 && !drums()) {
            // chords built in the key (or plain chords when there is no scale)
            auto stack = [&](int root, std::vector<int> steps) {
                std::vector<int> out; auto& sc = Scales::all()[(size_t) pr.scale].second;
                if (pr.scale == 0) { for (int s : steps) out.push_back(root + s); return out; }
                // in the scale: count scale degrees up from the root
                std::vector<int> deg; for (int o = -1; o < 4; ++o) for (int d : sc) deg.push_back(pr.key + d + 12 * o);
                std::sort(deg.begin(), deg.end());
                auto it = std::lower_bound(deg.begin(), deg.end(), root); int i0 = (int) (it - deg.begin());
                for (int s : steps) { int k = i0 + s; while (k >= (int) deg.size()) k -= (int) sc.size(); out.push_back(deg[(size_t) k] + (i0 + s >= (int) deg.size() ? 12 : 0)); }
                return out;
            };
            if (pr.scale == 0) {
                static const std::vector<std::vector<int>> C = { {}, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10 }, { 0, 5, 7 }, { 0, 7 }, { 0, 4, 7, 14 } };
                add = stack(pt, C[(size_t) juce::jlimit(0, (int) C.size() - 1, chord)]);
            } else {
                static const std::vector<std::vector<int>> D = { {}, { 0, 2, 4 }, { 0, 2, 4 }, { 0, 2, 4, 6 }, { 0, 2, 4, 6 }, { 0, 2, 4, 6 }, { 0, 3, 4 }, { 0, 4 }, { 0, 2, 4, 8 } };
                add = stack(pt, D[(size_t) juce::jlimit(0, (int) D.size() - 1, chord)]);
            }
        }
        sel.clear();
        for (int a : add) { if (a < 0 || a > 127) continue; Note n; n.start = st; n.len = noteLen; n.pitch = a; n.vel = 0.8f; c->notes.push_back(n); sel.insert((int) c->notes.size() - 1); }
        hit = (int) c->notes.size() - 1; mode = ResizeR; orig = c->notes; changed = true;
        if (owner()) { host().engine.keysTarget.store(owner()->id); for (int a : add) { host().noteOn(a, 0.8f); juce::Timer::callAfterDelay(160, [a] { host().noteOff(a); }); } }
        host().rebuild(); repaint(); return;
    }
    if (!e.mods.isShiftDown()) sel.clear();
    mode = Band; band = {}; repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e) {
    lpDrag(e); if (fired) return;
    auto* c = clip(); if (!c) return;
    bool noSnap = e.mods.isCommandDown();
    if (mode == Keys) { int pt = pitchAtY(e.position.y); if (pt >= 0 && pt != lastKey) { host().noteOff(lastKey); host().noteOn(pt, 0.8f); lastKey = pt; } return; }
    if (mode == LoopEnd) { c->length = std::max(0.25, snapB(beatAt(e.position.x), noSnap)); changed = true; host().rebuild(); repaint(); if (onChanged) onChanged(); return; }
    if (mode == Vel) {
        auto va = velArea(); float v = juce::jlimit(0.02f, 1.0f, (float) (va.getBottom() - 4 - e.y) / (float) (va.getHeight() - 8));
        int best = -1; float bd = 8;
        for (int i = 0; i < (int) c->notes.size(); ++i) { float d = std::abs(xOf(c->notes[(size_t) i].start) - e.position.x); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) { if (sel.count(best) && sel.size() > 1) { float d = v - c->notes[(size_t) best].vel; for (int i : sel) c->notes[(size_t) i].vel = juce::jlimit(0.02f, 1.0f, c->notes[(size_t) i].vel + d); }
            else c->notes[(size_t) best].vel = v; changed = true; host().rebuild(); }
        repaint(); return;
    }
    if (mode == Band) {
        band = juce::Rectangle<float>(down, e.position);
        sel.clear(); for (int i = 0; i < (int) c->notes.size(); ++i) if (noteRect(c->notes[(size_t) i]).intersects(band)) sel.insert(i);
        repaint(); return;
    }
    if (hit < 0 || hit >= (int) orig.size()) return;
    if (e.getDistanceFromDragStart() < 2 && mode == Move) return;
    double d = beatAt(e.position.x) - downBeat;
    auto& h0 = orig[(size_t) hit];
    if (mode == Move) {
        double ns = snapB(h0.start + d, noSnap); double db = ns - h0.start;
        int pt = pitchAtY(e.position.y); int dp = pt >= 0 ? pt - downPitch : 0;
        auto rows = rowsShown();
        for (int i : sel) { if (i >= (int) orig.size()) continue; auto& o = orig[(size_t) i]; auto& n = c->notes[(size_t) i];
            n.start = std::max(-o.len + 0.01, o.start + db);
            int np = juce::jlimit(0, 127, o.pitch + dp);
            if (!rows.empty()) { auto it = std::find(rows.begin(), rows.end(), o.pitch); auto jt = std::find(rows.begin(), rows.end(), downPitch), kt = std::find(rows.begin(), rows.end(), pt);
                if (it != rows.end() && jt != rows.end() && kt != rows.end()) { int k = (int) (it - rows.begin()) + (int) (kt - jt); np = rows[(size_t) juce::jlimit(0, (int) rows.size() - 1, k)]; } }
            if (scaleSnap && !drums()) np = Scales::snapToScale(np, P().key, P().scale);
            n.pitch = np; }
        if (dp != 0 && owner() && pt >= 0 && pt != lastKey) { int pp = c->notes[(size_t) hit].pitch; host().noteOn(pp, 0.7f); juce::Timer::callAfterDelay(120, [pp] { host().noteOff(pp); }); lastKey = pt; }
    } else if (mode == ResizeR) {
        double ne = snapB(h0.start + h0.len + d, noSnap); double nl = std::max(triplet ? grid * 2 / 3.0 : (grid > 0 ? grid : 0.05), ne - h0.start);
        if (noSnap) nl = std::max(0.02, ne - h0.start);
        double k = nl - h0.len;
        for (int i : sel) if (i < (int) orig.size()) c->notes[(size_t) i].len = std::max(0.02, orig[(size_t) i].len + k);
        noteLen = c->notes[(size_t) hit].len;
    } else if (mode == ResizeL) {
        double ns = snapB(h0.start + d, noSnap); double k = std::min(ns - h0.start, h0.len - 0.02);
        for (int i : sel) if (i < (int) orig.size()) { auto& o = orig[(size_t) i]; c->notes[(size_t) i].start = o.start + k; c->notes[(size_t) i].len = std::max(0.02, o.len - k); }
    }
    changed = true; host().rebuild(); repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent& e) {
    lpUp();
    if (mode == Keys) { host().noteOff(lastKey); lastKey = -1; mode = None; return; }
    if (mode == Move || mode == ResizeL || mode == ResizeR || mode == Vel || mode == LoopEnd) {
        if (auto* c = clip()) if (mode != Vel && mode != LoopEnd) {
            // keep notes sorted; the selection follows them
            std::vector<std::pair<Note, bool>> v; for (int i = 0; i < (int) c->notes.size(); ++i) v.push_back({ c->notes[(size_t) i], sel.count(i) > 0 });
            std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first.start < b.first.start; });
            c->notes.clear(); sel.clear(); for (auto& x : v) { c->notes.push_back(x.first); if (x.second) sel.insert((int) c->notes.size() - 1); }
        }
        host().endGesture(mode == Vel ? "velocity" : mode == LoopEnd ? "clip length" : mode == Move ? "move notes" : "note length", changed);
        if (onChanged) onChanged();
    }
    if (mode == Band && e.getDistanceFromDragStart() < 3 && !drawMode) {}
    mode = None; band = {}; hit = -1; repaint();
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& e) {
    int part = 0; int h = hitNote(e.position, part);
    if (h >= 0) { sel.clear(); sel.insert(h); deleteSelected(); }
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { double b = beatAt(e.position.x); ppb = juce::jlimit(8.0, 800.0, ppb * (w.deltaY > 0 ? 1.15 : 1 / 1.15)); scrollBeat = std::max(0.0, b - (e.position.x - keysW) / ppb); repaint(); return; }
    if (e.mods.isAltDown()) { rowH = juce::jlimit(6.0f, 30.0f, rowH * (w.deltaY > 0 ? 1.1f : 0.9f)); repaint(); return; }
    double dx = w.deltaX != 0 ? -w.deltaX : (e.mods.isShiftDown() ? -w.deltaY : 0.0);
    scrollBeat = std::max(0.0, scrollBeat + dx * 300.0 / ppb);
    if (!e.mods.isShiftDown()) scrollPitch = juce::jlimit(10.0f, 127.0f, scrollPitch + w.deltaY * 30.0f);
    repaint();
}
void PianoRoll::mouseMagnify(const juce::MouseEvent& e, float s) { double b = beatAt(e.position.x); ppb = juce::jlimit(8.0, 800.0, ppb * s); scrollBeat = std::max(0.0, b - (e.position.x - keysW) / ppb); repaint(); }

bool PianoRoll::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (k == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) { selectAll(); return true; }
    if (k == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) { op("dup"); return true; }
    if (k.getKeyCode() == juce::KeyPress::upKey) { op(k.getModifiers().isShiftDown() ? "+12" : "+1"); return true; }
    if (k.getKeyCode() == juce::KeyPress::downKey) { op(k.getModifiers().isShiftDown() ? "-12" : "-1"); return true; }
    if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) { op(k.getKeyCode() == juce::KeyPress::leftKey ? "left" : "right"); return true; }
    if (k == juce::KeyPress('q')) { op("quantize"); return true; }
    if (k == juce::KeyPress('b')) { drawMode = !drawMode; repaint(); return true; }
    return false;
}

void PianoRoll::deleteSelected() {
    auto* c = clip(); if (!c || sel.empty()) return;
    auto s = sel; Uid id = clipId;
    host().edit("delete notes", [id, s] { if (Clip* cc = P().clip(id)) { std::vector<Note> keep; for (int i = 0; i < (int) cc->notes.size(); ++i) if (!s.count(i)) keep.push_back(cc->notes[(size_t) i]); cc->notes = keep; } });
    sel.clear(); repaint(); if (onChanged) onChanged();
}

void PianoRoll::op(const juce::String& what) {
    auto* c = clip(); if (!c) return;
    std::set<int> s = sel; if (s.empty()) for (int i = 0; i < (int) c->notes.size(); ++i) s.insert(i);
    Uid id = clipId; double gg = triplet ? grid * 2.0 / 3.0 : grid; if (gg <= 0) gg = 0.25;
    double bpb = P().beatsPerBar;
    host().edit(what == "quantize" ? "quantize" : "notes", [&, id] {
        Clip* cc = P().clip(id); if (!cc) return; auto& N = cc->notes;
        if (what == "quantize") for (int i : s) { auto& n = N[(size_t) i]; double q = std::round(n.start / gg) * gg; n.start = q; n.len = std::max(gg * 0.5, std::round(n.len / gg) * gg); }
        else if (what == "legato") { std::vector<int> v(s.begin(), s.end()); std::sort(v.begin(), v.end(), [&](int a, int b) { return N[(size_t) a].start < N[(size_t) b].start; });
            for (size_t k = 0; k < v.size(); ++k) { auto& n = N[(size_t) v[k]]; double next = cc->length; for (size_t j = k + 1; j < v.size(); ++j) if (N[(size_t) v[j]].start > n.start + 1e-6) { next = N[(size_t) v[j]].start; break; } n.len = std::max(0.05, next - n.start); } }
        else if (what == "humanize") { juce::Random r; for (int i : s) { auto& n = N[(size_t) i]; n.start = std::max(0.0, n.start + (r.nextDouble() - 0.5) * 0.04); n.vel = juce::jlimit(0.05f, 1.0f, n.vel + (r.nextFloat() - 0.5f) * 0.2f); } }
        else if (what == "+1" || what == "-1" || what == "+12" || what == "-12") { int d = what.getIntValue(); for (int i : s) N[(size_t) i].pitch = juce::jlimit(0, 127, N[(size_t) i].pitch + d); }
        else if (what == "left" || what == "right") { double d = what == "left" ? -gg : gg; for (int i : s) N[(size_t) i].start = std::max(0.0, N[(size_t) i].start + d); }
        else if (what == "x2" || what == "/2") { double f = what == "x2" ? 2.0 : 0.5; double a = 1e9; for (int i : s) a = std::min(a, N[(size_t) i].start);
            for (int i : s) { auto& n = N[(size_t) i]; n.start = a + (n.start - a) * f; n.len *= f; } }
        else if (what == "reverse") { double a = 1e9, b = -1e9; for (int i : s) { a = std::min(a, N[(size_t) i].start); b = std::max(b, N[(size_t) i].start + N[(size_t) i].len); }
            for (int i : s) { auto& n = N[(size_t) i]; n.start = a + (b - (n.start + n.len) - a) + 0.0; } }
        else if (what == "dup") { double a = 1e9, b = -1e9; for (int i : s) { a = std::min(a, N[(size_t) i].start); b = std::max(b, N[(size_t) i].start + N[(size_t) i].len); }
            double len = std::ceil((b - a) / bpb - 1e-6) * bpb; if (len <= 0) len = bpb; if (sel.size() > 0 && sel.size() < N.size()) len = std::ceil((b - a) / gg - 1e-6) * gg;
            std::vector<Note> add; for (int i : s) { Note n = N[(size_t) i]; n.start += len; add.push_back(n); }
            for (auto& n : add) N.push_back(n); cc->length = std::max(cc->length, std::ceil((b + len) / bpb - 1e-6) * bpb); }
        else if (what == "fit") { double b = 0; for (auto& n : N) b = std::max(b, n.start + n.len); cc->length = std::max(bpb, std::ceil(b / bpb - 1e-6) * bpb); }
        else if (what == "scale") { for (int i : s) N[(size_t) i].pitch = Scales::snapToScale(N[(size_t) i].pitch, P().key, P().scale); }
        else if (what == "vel+" || what == "vel-") { for (int i : s) N[(size_t) i].vel = juce::jlimit(0.05f, 1.0f, N[(size_t) i].vel + (what == "vel+" ? 0.1f : -0.1f)); }
    });
    if (what == "dup") sel.clear();
    repaint(); if (onChanged) onChanged();
}

// ======================================================================== the audio editor
AudioEditor::AudioEditor() {}
Clip* AudioEditor::clip() const { Clip* c = P().clip(clipId); return c && !c->midi ? c : nullptr; }
float AudioEditor::xOfSec(double s) const { auto* c = clip(); auto w = wave(); double total = c && c->src ? c->src->seconds() : 1.0; return w.getX() + (float) (s / std::max(0.001, total) * w.getWidth()); }
double AudioEditor::secAt(float x) const { auto* c = clip(); auto w = wave(); double total = c && c->src ? c->src->seconds() : 1.0; return juce::jlimit(0.0, total, (x - w.getX()) / (double) w.getWidth() * total); }

void AudioEditor::resized() {}

void AudioEditor::paint(juce::Graphics& g) {
    g.fillAll(Theme::scr());
    auto* c = clip();
    if (!c || !c->src) { label(g, "select an audio clip: drop a sound file on a track, or record one", getLocalBounds(), 10.0f, 1, Theme::sub(), juce::Justification::centred); return; }
    Track* t = nullptr; P().clip(clipId, &t);
    auto col = Theme::argb(c->colour ? c->colour : (t ? t->colour : 0xff8fa8d8));
    auto w = wave(); auto& src = *c->src;
    double total = src.seconds();
    label(g, c->src->name + U("   ·   ") + juce::String(total, 2) + " s in the file   ·   " + juce::String(c->srcLen, 2) + " s play" + (c->reverse ? U("   ·   reversed") : juce::String()),
          { 14, 6, getWidth() - 28, 20 }, 9.0f, 1);
    g.setColour(Theme::bg()); g.fillRoundedRectangle(w.toFloat(), 6);
    float a = xOfSec(c->offset), b = xOfSec(c->offset + c->srcLen);
    g.setColour(col.withAlpha(0.12f)); g.fillRect(a, (float) w.getY(), b - a, (float) w.getHeight());
    // the waveform (peaks of 64 samples)
    int npk = (int) src.peaks.size(); float gain = dbToGain(c->gainDb);
    for (int x = w.getX(); x < w.getRight(); ++x) {
        double s0 = secAt((float) x), s1 = secAt((float) x + 1);
        int i0 = (int) (s0 * src.sr / 64), i1 = std::max(i0 + 1, (int) (s1 * src.sr / 64));
        float pk = 0; for (int i = i0; i < i1 && i < npk; ++i) pk = std::max(pk, src.peaks[(size_t) i]);
        bool in = x >= a && x <= b;
        float fg = 1.0f;
        if (in) { double ts = s0 - c->offset; if (c->fadeIn > 0 && ts < c->fadeIn) fg = (float) (ts / c->fadeIn); double te = c->offset + c->srcLen - s0; if (c->fadeOut > 0 && te < c->fadeOut) fg = std::min(fg, (float) (te / c->fadeOut)); }
        float h = juce::jmin(1.0f, pk * (in ? gain * fg : 1.0f)) * w.getHeight() * 0.48f;
        g.setColour(in ? col : Theme::off()); g.fillRect((float) x, w.getCentreY() - h, 1.0f, h * 2 + 1);
    }
    // handles: start / end, fades
    g.setColour(Theme::gold()); g.fillRect(a - 1, (float) w.getY(), 2.0f, (float) w.getHeight()); g.fillRect(b - 1, (float) w.getY(), 2.0f, (float) w.getHeight());
    g.fillRoundedRectangle(a - 1, (float) w.getY(), 9, 16, 2); g.fillRoundedRectangle(b - 8, (float) w.getY(), 9, 16, 2);
    float fi = xOfSec(c->offset + c->fadeIn), fo = xOfSec(c->offset + c->srcLen - c->fadeOut);
    juce::Path fp; fp.startNewSubPath(a, (float) w.getBottom()); fp.lineTo(fi, (float) w.getY() + 2); fp.lineTo(fo, (float) w.getY() + 2); fp.lineTo(b, (float) w.getBottom());
    g.setColour(Theme::txt().withAlpha(0.6f)); g.strokePath(fp, juce::PathStrokeType(1.2f));
    g.setColour(Theme::txt()); g.fillEllipse(fi - 4, (float) w.getY() - 2, 8, 8); g.fillEllipse(fo - 4, (float) w.getY() - 2, 8, 8);
    // where it plays now
    double pos = host().engine.position();
    if (pos >= c->start && pos <= c->end()) { double s = c->offset + (pos - c->start) * 60.0 / P().bpm; if (c->reverse) s = c->offset + c->srcLen - (s - c->offset);
        g.setColour(Theme::green()); g.fillRect(xOfSec(s), (float) w.getY(), 1.2f, (float) w.getHeight()); }
    label(g, "gain " + juce::String(c->gainDb, 1) + " dB  (drag up/down in the sound)", { w.getX() + 6, w.getBottom() - 16, 300, 12 }, 7.5f, 1);
}

void AudioEditor::mouseMove(const juce::MouseEvent& e) {
    auto* c = clip(); if (!c) return;
    float a = xOfSec(c->offset), b = xOfSec(c->offset + c->srcLen);
    setMouseCursor(std::abs(e.position.x - a) < 6 || std::abs(e.position.x - b) < 6 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void AudioEditor::mouseDown(const juce::MouseEvent& e) {
    auto* c = clip(); if (!c) return;
    float a = xOfSec(c->offset), b = xOfSec(c->offset + c->srcLen), fi = xOfSec(c->offset + c->fadeIn), fo = xOfSec(c->offset + c->srcLen - c->fadeOut);
    auto w = wave(); changed = false;
    o0 = c->offset; l0 = c->srcLen; g0 = c->gainDb; f0 = c->start;
    if (e.position.y < w.getY() + 10 && std::abs(e.position.x - fi) < 7) mode = 3;
    else if (e.position.y < w.getY() + 10 && std::abs(e.position.x - fo) < 7) mode = 4;
    else if (std::abs(e.position.x - a) < 7) mode = 1;
    else if (std::abs(e.position.x - b) < 7) mode = 2;
    else if (e.position.x > a && e.position.x < b) { mode = 5; double s = secAt(e.position.x); host().locate(c->start + (s - c->offset) * P().bpm / 60.0); }
    else mode = 0;
    if (mode) host().beginGesture();
}
void AudioEditor::mouseDrag(const juce::MouseEvent& e) {
    auto* c = clip(); if (!c || !c->src) return;
    double spb = 60.0 / P().bpm, s = secAt(e.position.x), total = c->src->seconds();
    if (mode == 1) { double no = juce::jlimit(0.0, o0 + l0 - 0.02, s); c->offset = no; c->srcLen = o0 + l0 - no; c->start = std::max(0.0, f0 + (no - o0) / spb); c->length = c->srcLen / spb; }
    else if (mode == 2) { c->srcLen = juce::jlimit(0.02, total - c->offset, s - c->offset); c->length = c->srcLen / spb; }
    else if (mode == 3) c->fadeIn = juce::jlimit(0.0, c->srcLen - c->fadeOut, s - c->offset);
    else if (mode == 4) c->fadeOut = juce::jlimit(0.0, c->srcLen - c->fadeIn, c->offset + c->srcLen - s);
    else if (mode == 5) { if (std::abs(e.getDistanceFromDragStartY()) < 4) return; c->gainDb = juce::jlimit(-48.0f, 24.0f, g0 - e.getDistanceFromDragStartY() * 0.15f); }
    else return;
    changed = true; host().rebuild(); repaint();
}
void AudioEditor::mouseUp(const juce::MouseEvent&) { if (mode) host().endGesture(mode == 5 ? "clip gain" : mode >= 3 ? "fade" : "trim", changed); mode = 0; }

// ======================================================================== the editor panel
ClipEditor::ClipEditor() { addChildComponent(roll); addChildComponent(audio); startTimerHz(30); }

void ClipEditor::buildTools() {
    tools.clear();
    auto pill = [this](const juce::String& t, std::function<void()> f, const juce::String& tip = {}) {
        auto* p = new Pill(t); p->fsize = 8.5f; p->radius = 5.0f; p->onClick = std::move(f); if (tip.isNotEmpty()) p->setTooltip(tip); tools.add(p); addAndMakeVisible(p); return p; };
    auto busy = [](const juce::String& op, double amt) { auto id = host().selClip; juce::MouseCursor::showWaitCursor(); auto e = host().processClip(id, op, amt); juce::MouseCursor::hideWaitCursor(); if (e.isNotEmpty()) host().say(e); };
    if (midi) {
        auto* d = pill("DRAW", [this] { roll.drawMode = !roll.drawMode; roll.repaint(); }, U("DRAW: click adds notes · off: drag a box to select (B)"));
        d->lit = [this] { return roll.drawMode; };
        auto* gr = pill("", nullptr, "the grid notes snap to (cmd-drag: no snap)");
        gr->dynText = [this] { double g = roll.grid; return juce::String("GRID ") + (g >= 1 ? "1/4" : g >= 0.5 ? "1/8" : g >= 0.25 ? "1/16" : g >= 0.125 ? "1/32" : "OFF") + (roll.triplet ? "T" : ""); };
        gr->onClick = [this, gr] { juce::PopupMenu m; const double G[5] = { 1, 0.5, 0.25, 0.125, 0 }; const char* N[5] = { "1/4", "1/8", "1/16", "1/32", "Off" };
            for (int i = 0; i < 5; ++i) m.addItem(i + 1, N[i], true, std::abs(roll.grid - G[i]) < 1e-9); m.addSeparator(); m.addItem(10, "Triplets", true, roll.triplet);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(gr), [this](int r) { const double G[5] = { 1, 0.5, 0.25, 0.125, 0 }; if (r >= 1 && r <= 5) roll.grid = G[r - 1]; if (r == 10) roll.triplet = !roll.triplet; if (r > 0) { roll.noteLen = roll.grid > 0 ? roll.grid : 0.25; repaint(); roll.repaint(); } }); };
        auto* sc = pill("", nullptr, "the key and scale: rows outside it are darker");
        sc->dynText = [] { auto& p = P(); return p.scale == 0 ? juce::String("SCALE OFF") : Scales::keyName(p.key) + " " + Scales::all()[(size_t) p.scale].first.toUpperCase(); };
        sc->onClick = [this, sc] { juce::PopupMenu m, k, s; for (int i = 0; i < 12; ++i) k.addItem(100 + i, Scales::keyName(i), true, P().key == i);
            for (int i = 0; i < (int) Scales::all().size(); ++i) s.addItem(200 + i, Scales::all()[(size_t) i].first, true, P().scale == i);
            m.addSubMenu("Key", k); m.addSubMenu("Scale", s); m.addSeparator(); m.addItem(1, "Snap drawn notes to the scale", true, roll.scaleSnap); m.addItem(2, "Move the selected notes into the scale");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(sc), [this](int r) { if (r >= 100 && r < 112) host().edit("key", [r] { P().key = r - 100; }, false);
                if (r >= 200) host().edit("scale", [r] { P().scale = r - 200; }, false); if (r == 1) roll.scaleSnap = !roll.scaleSnap; if (r == 2) roll.op("scale"); roll.repaint(); repaint(); }); };
        auto* ch = pill("", nullptr, "a click draws a whole chord (in the key when a scale is on)");
        ch->dynText = [this] { static const char* N[9] = { "CHORD OFF", "MAJOR", "MINOR", "MAJ 7", "MIN 7", "DOM 7", "SUS 4", "POWER", "ADD 9" }; return juce::String(N[roll.chord]); };
        ch->lit = [this] { return roll.chord > 0; }; ch->litColour = [] { return Theme::violet(); };
        ch->onClick = [this, ch] { juce::PopupMenu m; const char* N[9] = { "Single notes", "Triad (major)", "Triad (minor)", "Major 7", "Minor 7", "Dominant 7", "Sus 4", "Power (5th)", "Add 9" };
            for (int i = 0; i < 9; ++i) m.addItem(i + 1, i > 0 && P().scale > 0 ? juce::String(N[i]) + U(" · in the key") : juce::String(N[i]), true, roll.chord == i);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(ch), [this](int r) { if (r > 0) roll.chord = r - 1; repaint(); }); };
        auto* f = pill("FOLD", [this] { roll.fold = !roll.fold; roll.repaint(); }, "only the rows with notes"); f->lit = [this] { return roll.fold; };
        pill("QUANTIZE", [this] { roll.op("quantize"); }, "to the grid (Q)");
        pill("LEGATO", [this] { roll.op("legato"); });
        pill("HUMANIZE", [this] { roll.op("humanize"); }, "small random timing and velocity");
        pill("-12", [this] { roll.op("-12"); }); pill("-1", [this] { roll.op("-1"); }); pill("+1", [this] { roll.op("+1"); }); pill("+12", [this] { roll.op("+12"); });
        pill(U("×2"), [this] { roll.op("x2"); }, "twice as long"); pill(U("÷2"), [this] { roll.op("/2"); }, "twice as fast");
        pill("REVERSE", [this] { roll.op("reverse"); });
        pill("DUPLICATE", [this] { roll.op("dup"); }, "the selection (or the whole clip) after itself (cmd-D)");
        pill("FIT", [this] { roll.zoomToFit(); roll.repaint(); }, "zoom to the clip");
    } else {
        auto* gainBox = new ValueBox([] { auto* c = host().selectedClip(); return c ? (double) c->gainDb : 0.0; },
                                     [](double v) { if (auto* c = host().selectedClip()) { host().knobTouched(); c->gainDb = (float) v; host().rebuild(); } }, -48, 24, 1, 0.5);
        gainBox->format = [](double v) { return "GAIN " + juce::String(v, 1) + " dB"; }; gainBox->fsize = 8.5f; tools.add(gainBox); addAndMakeVisible(gainBox);
        pill("NORMALIZE", [busy] { busy("normalize", 0); });
        pill("REVERSE", [busy] { busy("reverse", 0); });
        auto* pt = pill("PITCH", nullptr, "change the pitch, same length");
        pt->onClick = [pt, busy] { juce::PopupMenu m; for (int s : { -12, -7, -5, -2, -1, 1, 2, 5, 7, 12 }) m.addItem(100 + s, (s > 0 ? "+" : "") + juce::String(s) + " semitones");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pt), [busy](int r) { if (r) busy("pitch", r - 100); }); };
        auto* st = pill("STRETCH", nullptr, "change the length, same pitch");
        st->onClick = [st, busy] { juce::PopupMenu m; for (auto r : { 0.5, 0.75, 0.9, 1.1, 1.25, 1.5, 2.0, 4.0 }) m.addItem((int) (r * 1000), "x" + juce::String(r, 2));
            m.addItem(1, "Fit to whole bars");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(st), [busy](int r) { if (r == 1) busy("fitbars", 0); else if (r > 1) busy("stretch", r / 1000.0); }); };
        pill("HALF SPEED", [busy] { busy("speed", 0.5); }); pill("DOUBLE SPEED", [busy] { busy("speed", 2.0); });
        pill("SPLIT", [] { host().splitClip(host().selClip, host().engine.position()); }, "at the playhead");
        pill("STRIP SILENCE", [busy] { busy("silence", -45); });
        pill("APPLY CLIP FX", [busy] { busy("bake", 0); }, "the clip's own effects become part of the sound");
        pill("TO SAMPLER", [] { host().clipToSampler(host().selClip); }, "play this sound on the keys");
        pill("REVERT", [busy] { busy("revert", 0); }, "back to the original recording");
    }
    auto* fx = pill("CLIP FX", [this] { if (onClipFx) onClipFx(); }, "effects on this clip only");
    fx->bg = [] { return Theme::violet().withAlpha(0.7f); };
}

void ClipEditor::update() {
    Clip* c = host().selectedClip();
    bool m = c && c->midi;
    if (c && (m != midi || tools.isEmpty())) { midi = m; buildTools(); resized(); }
    roll.setVisible(c && m); audio.setVisible(c && !m);
    if (c) { if (m) roll.setClip(c->id); else audio.setClip(c->id); }
    for (auto* t : tools) t->setVisible(c != nullptr);
    seen = c ? c->id : 0; seenStructure = host().structure;
    repaint();
}

void ClipEditor::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).reduced(8, 5);
    top.removeFromLeft(180);
    int x = top.getX();
    for (auto* t : tools) {
        int w = 46;
        if (auto* p = dynamic_cast<Pill*>(t)) { auto txt = p->dynText ? p->dynText() : p->text; w = juce::jmax(28, (int) (Theme::font(8.5f, 2).getStringWidthFloat(txt) + 16)); if (p->dynText) w = juce::jmax(w, 78); }
        else w = 96;
        if (x + w > getWidth() - 8) { t->setBounds(0, 0, 0, 0); continue; }
        t->setBounds(x, top.getY(), w, top.getHeight()); x += w + 4;
    }
    roll.setBounds(r); audio.setBounds(r);
    if (roll.isVisible() && roll.clipId) roll.repaint();
}

void ClipEditor::paint(juce::Graphics& g) {
    g.fillAll(Theme::panel());
    Clip* c = host().selectedClip(); Track* t = nullptr; if (c) P().clip(c->id, &t);
    if (c) {
        auto col = Theme::argb(c->colour ? c->colour : (t ? t->colour : 0xff8fa8d8));
        g.setColour(col); g.fillRoundedRectangle(8, 9, 4, 13, 2);
        label(g, c->name, { 18, 5, 160, 20 }, 10.0f, 2, Theme::txt());
    } else label(g, "EDITOR", { 12, 5, 160, 20 }, 9.0f, 2);
    if (!c) label(g, U("select a clip in the arranger: notes open the piano roll, sounds open the audio editor"), getLocalBounds().withTrimmedTop(30), 10.0f, 1, Theme::sub(), juce::Justification::centred);
}

void ClipEditor::timerCallback() {
    if (host().selClip != seen || host().structure != seenStructure) { update(); resized(); }
    if (host().engine.isPlaying()) { if (roll.isVisible()) roll.repaint(); if (audio.isVisible()) audio.repaint(); }
    static int k = 0; if (++k % 8 == 0) for (auto* t : tools) t->repaint();
}

} // namespace score
