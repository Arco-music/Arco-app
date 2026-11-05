#pragma once
// 1.4: the ways to play on a screen, shared by the SYNTH and the KEYBOARD windows:
// SLIDE PAD (the SYNTH only), CHORDS (8 pads, each set note by note or by type), HARMONY (circles where neighbours always
// sound good together), and the dots that show which notes HOLD keeps on.
#include <JuceHeader.h>
#include <set>
#include <map>
#include <functional>
#include "MainView.h"

namespace arco {
inline void psText(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, 0)); g.drawFittedText(t, r, juce::Justification::centredLeft, 2, 0.8f);
}
inline void lbl(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 2) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, juce::Justification::centred, 2, 0.8f);
}
// the chords a pad can be (intervals above its root) - for the quick buttons and for naming a chord
struct ChordType { const char* name; const char* label; std::vector<int> iv; };
inline const std::vector<ChordType>& chordTypes() {
    static const std::vector<ChordType> t = {
        { "", "MAJ", { 0, 4, 7 } }, { "m", "MIN", { 0, 3, 7 } }, { "7", "7", { 0, 4, 7, 10 } }, { "maj7", "MAJ7", { 0, 4, 7, 11 } },
        { "m7", "MIN7", { 0, 3, 7, 10 } }, { "sus4", "SUS4", { 0, 5, 7 } }, { "sus2", "SUS2", { 0, 2, 7 } }, { "dim", "DIM", { 0, 3, 6 } },
        { "aug", "AUG", { 0, 4, 8 } }, { "add9", "ADD9", { 0, 4, 7, 14 } }, { "m9", "MIN9", { 0, 3, 7, 10, 14 } }, { "6", "6", { 0, 4, 7, 9 } } };
    return t;
}
inline const char* NOTE_NAMES[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
inline juce::String chordName(std::vector<int> n) {
    if (n.empty()) return "-";
    std::sort(n.begin(), n.end());
    if (n.size() == 1) return NOTE_NAMES[((n[0] % 12) + 12) % 12];
    for (int inv = 0; inv < (int) n.size(); ++inv) {               // try each note as the root
        int root = n[(size_t) inv];
        std::set<int> pcs; for (int x : n) pcs.insert(((x - root) % 12 + 12) % 12);
        for (auto& t : chordTypes()) {
            std::set<int> tp; for (int v : t.iv) tp.insert(v % 12);
            if (tp == pcs) return juce::String(NOTE_NAMES[((root % 12) + 12) % 12]) + t.name + (inv > 0 ? juce::String("/") + NOTE_NAMES[((n[0] % 12) + 12) % 12] : juce::String());
        }
    }
    juce::String s; for (size_t i = 0; i < n.size() && i < 4; ++i) s << (i ? " " : "") << NOTE_NAMES[((n[i] % 12) + 12) % 12];
    return s;
}

// four ways to play on a screen: KEYS (the piano below), SLIDE PAD, CHORDS (8 pads you can set note by note), HARMONY
// (circles where neighbours always sound good together: any three touching circles are a major or minor chord)
class KeyPad : public juce::Component {
public:
    KeyPad(Canvas& c, std::function<int()> oct) : cv(c), octave(std::move(oct)) {}
    Canvas& cv; std::function<int()> octave; int mode = 1;
    bool editing = false; int editPad = 0;
    std::map<int, int> touches; juce::Point<float> padAt; bool padOn = false;
    AudioHost& host() const { return cv.host; }
    int key() const { int k = (int) cv.snap("halo_key"); return k < 0 ? 0 : k % 12; }
    int base() const { return 12 * octave(); }
    static const int* diatonic(int i) { static const int CH[8][4] = { { 0, 4, 7, 12 }, { 2, 5, 9, -1 }, { 4, 7, 11, -1 }, { 5, 9, 12, -1 }, { 7, 11, 14, -1 }, { 9, 12, 16, -1 }, { 7, 11, 14, 17 }, { 12, 16, 19, 24 } }; return CH[i]; }
    std::vector<int> padNotes(int i) const {                          // the pad's notes (offsets above the octave's C)
        auto& custom = host().chordPads[(size_t) i];
        if (!custom.empty()) return custom;
        std::vector<int> v; for (int k = 0; k < 4; ++k) if (diatonic(i)[k] >= 0) v.push_back(key() + diatonic(i)[k]);
        return v;
    }
    // ---- HARMONY: a hex grid - right = a fifth, up-right = a major third, up-left = a minor third
    float cellH() const { return getHeight() / 3.0f; }
    float cellW() const { return cellH() * 1.06f; }
    int cols() const { return juce::jmax(4, (int) ((getWidth() - cellW() * 0.5f) / cellW())); }
    juce::Point<float> cellCentre(int c, int r) const { float x = cellW() * (c + 0.5f) + ((r % 2) ? cellW() * 0.5f : 0.0f); return { x, getHeight() - cellH() * (r + 0.5f) }; }
    int harmonyNote(int c, int r) const {
        int c0 = c - cols() / 2 + 1;
        int f = 4 * ((r + 1) / 2) - 3 * (r / 2);                     // row steps: +4 then -3 (the hex rows lean alternately)
        int pc = ((7 * c0 + f) % 24 + 24) % 24;
        return base() + key() + pc - (pc >= 19 ? 12 : 0); }
    bool inKey(int n) const { static const bool IN[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 }; return IN[((n - key()) % 12 + 12) % 12]; }
    bool sounding(int n) const { if (host().latched.count(n)) return true; for (auto& t : touches) if (t.second == n) return true; return false; }

    // ---- chord editor layout
    juce::Rectangle<float> editPads() const { return getLocalBounds().toFloat().removeFromTop(38).reduced(4, 3).withTrimmedRight(214); }
    juce::Rectangle<float> editKeys() const { auto r = getLocalBounds().toFloat().reduced(4); r.removeFromTop(38); return r.withTrimmedRight(214); }
    juce::Rectangle<float> editTypes() const { auto r = getLocalBounds().toFloat().reduced(4); return r.removeFromRight(206); }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 10.0f);
        if (mode == 1) {
            for (int i = 0; i <= 24; ++i) { float x = r.getX() + r.getWidth() * i / 24.0f;
                g.setColour(Theme::txt().withAlpha(i % 12 == 0 ? 0.4f : (inKey(key() + i) ? 0.15f : 0.05f))); g.fillRect(x, r.getY() + 6, 1.0f, r.getHeight() - 12); }
            psText(g, juce::String::fromUTF8("SLIDE PAD   ←  lower · higher  →    ↑ louder · softer ↓   (two octaves, gliding)"), getLocalBounds().reduced(12, 6).removeFromTop(16), 8.0f, Theme::sub());
            if (padOn) { g.setColour(Theme::violet()); g.fillEllipse(juce::Rectangle<float>(30, 30).withCentre(padAt)); }
        } else if (mode == 2 && editing) {
            auto pr = editPads(); float pw = pr.getWidth() / 8;
            for (int i = 0; i < 8; ++i) { auto c = juce::Rectangle<float>(pr.getX() + i * pw, pr.getY(), pw, pr.getHeight()).reduced(2);
                g.setColour(i == editPad ? Theme::violet() : Theme::card()); g.fillRoundedRectangle(c, 6.0f);
                lbl(g, chordName(padNotes(i)), c.toNearestInt(), 8.5f, i == editPad ? Theme::textOn(Theme::violet()) : Theme::txt()); }
            // two octaves: touch a key to add / remove it from the chord
            auto kr = editKeys(); auto notes = padNotes(editPad);
            static const bool BLK[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
            int whites = 15; float ww = kr.getWidth() / whites;
            int wi = 0;
            for (int k = 0; k < 25; ++k) if (!BLK[k % 12]) { auto c = juce::Rectangle<float>(kr.getX() + wi * ww, kr.getY(), ww, kr.getHeight()).reduced(1);
                bool on = std::find(notes.begin(), notes.end(), k) != notes.end();
                g.setColour(on ? Theme::violet() : juce::Colour(0xffe8e4dc)); g.fillRoundedRectangle(c, 4.0f);
                lbl(g, NOTE_NAMES[k % 12], c.removeFromBottom(16).toNearestInt(), 7.0f, on ? Theme::textOn(Theme::violet()) : juce::Colour(0xff555555)); ++wi; }
            wi = 0;
            for (int k = 0; k < 25; ++k) { if (!BLK[k % 12]) { ++wi; continue; }
                auto c = juce::Rectangle<float>(kr.getX() + wi * ww - ww * 0.32f, kr.getY(), ww * 0.64f, kr.getHeight() * 0.58f);
                bool on = std::find(notes.begin(), notes.end(), k) != notes.end();
                g.setColour(on ? Theme::violet().brighter(0.2f) : juce::Colour(0xff201820)); g.fillRoundedRectangle(c, 3.0f); }
            auto tr = editTypes(); float bh = tr.getHeight() / 5, bw = tr.getWidth() / 3;
            for (int i = 0; i < 12; ++i) { auto c = juce::Rectangle<float>(tr.getX() + (i % 3) * bw, tr.getY() + (i / 3) * bh, bw, bh).reduced(2);
                g.setColour(Theme::card()); g.fillRoundedRectangle(c, 5.0f);
                lbl(g, chordTypes()[(size_t) i].label, c.toNearestInt(), 7.5f, Theme::txt()); }
            auto rc = juce::Rectangle<float>(tr.getX(), tr.getY() + 4 * bh, tr.getWidth(), bh).reduced(2);
            g.setColour(Theme::btn()); g.fillRoundedRectangle(rc, 5.0f);
            lbl(g, juce::String::fromUTF8("THE KEY'S CHORD (reset)"), rc.toNearestInt(), 7.5f, Theme::txt());
        } else if (mode == 2) {
            float cw = r.getWidth() / 8;
            for (int i = 0; i < 8; ++i) { auto c = juce::Rectangle<float>(r.getX() + i * cw, r.getY(), cw, r.getHeight()).reduced(4);
                auto ns = padNotes(i); bool on = false;
                for (auto& t : touches) if (t.second == 1000 + i) on = true;
                if (host().latch && !host().latched.empty()) { bool all = true; for (int n : ns) all &= host().latched.count(base() + n) > 0; on |= all; }
                g.setColour(on ? Theme::violet() : Theme::card()); g.fillRoundedRectangle(c, 8.0f);
                lbl(g, chordName(ns), c.toNearestInt().withTrimmedBottom(20), 15.0f, on ? Theme::textOn(Theme::violet()) : Theme::txt());
                juce::String nn; for (int n : ns) nn << NOTE_NAMES[((n % 12) + 12) % 12] << " ";
                lbl(g, nn.trim(), c.toNearestInt().removeFromBottom(26), 7.5f, on ? Theme::textOn(Theme::violet()) : Theme::sub(), 1); }
        } else {
            float d = juce::jmin(cellW(), cellH()) * 0.92f;
            for (int row = 0; row < 3; ++row) for (int c = 0; c < cols(); ++c) {
                auto ctr = cellCentre(c, row); if (ctr.x + d * 0.5f > r.getRight()) continue;
                int n = harmonyNote(c, row); bool on = sounding(n), root = ((n - key()) % 12 + 12) % 12 == 0;
                auto e = juce::Rectangle<float>(d, d).withCentre(ctr);
                g.setColour(on ? Theme::violet() : (root ? Theme::blue().withAlpha(0.55f) : (inKey(n) ? Theme::card() : Theme::cardS().withAlpha(0.45f))));
                g.fillEllipse(e);
                if (!on && inKey(n)) { g.setColour(Theme::violet().withAlpha(0.35f)); g.drawEllipse(e.reduced(1), 1.4f); }
                lbl(g, NOTE_NAMES[n % 12], e.toNearestInt(), 9.0f, on ? Theme::textOn(Theme::violet()) : (inKey(n) ? Theme::txt() : Theme::sub()));
            }
        }
    }
    int noteAt(juce::Point<float> p) const {
        auto r = getLocalBounds().toFloat();
        if (mode == 2) return 1000 + juce::jlimit(0, 7, (int) ((p.x - r.getX()) / (r.getWidth() / 8)));
        int best = -1; float bd = 1e9f;                                  // HARMONY: the nearest circle
        for (int row = 0; row < 3; ++row) for (int c = 0; c < cols(); ++c) { float dd = cellCentre(c, row).getDistanceFrom(p); if (dd < bd) { bd = dd; best = harmonyNote(c, row); } }
        return best;
    }
    float velAt(juce::Point<float> p) const { if (mode != 3) return 0.8f; float fy = std::fmod(getHeight() - p.y, cellH()) / cellH(); return juce::jlimit(0.35f, 1.0f, 0.45f + 0.55f * fy); }
    void on(int n, bool down, float vel = 0.8f) {
        if (n >= 1000) {
            std::vector<int> abs; for (int v : padNotes(n - 1000)) abs.push_back(base() + v);
            if (host().latch) { if (down) host().latchChord(abs, vel); return; }
            for (int nn : abs) { if (down) host().keys.noteOn(1, nn, vel); else host().keys.noteOff(1, nn, 0.0f); }
        } else { if (down) host().keys.noteOn(1, n, vel); else host().keys.noteOff(1, n, 0.0f); }
    }
    void pad(const juce::MouseEvent& e, int what) {
        auto r = getLocalBounds().toFloat();
        float x = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()), y = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight());
        float pitch = (float) (base() + key()) + x * 24.0f;
        auto& s = host().synth;
        if (what == 0) s.padOn(pitch, 0.3f + 0.7f * y); else if (what == 1) s.padMove(pitch, y); else s.padOff();
        padAt = e.position; padOn = what != 2; repaint();
    }
    void editTap(juce::Point<float> p) {
        auto& custom = host().chordPads[(size_t) editPad];
        if (editPads().contains(p)) { editPad = juce::jlimit(0, 7, (int) ((p.x - editPads().getX()) / (editPads().getWidth() / 8))); playPad(); repaint(); return; }
        if (editTypes().contains(p)) {
            auto tr = editTypes(); float bh = tr.getHeight() / 5, bw = tr.getWidth() / 3;
            int row = (int) ((p.y - tr.getY()) / bh), col = juce::jlimit(0, 2, (int) ((p.x - tr.getX()) / bw));
            if (row >= 4) { custom.clear(); playPad(); repaint(); return; }
            auto cur = padNotes(editPad); int root = cur.empty() ? key() : *std::min_element(cur.begin(), cur.end());
            root = ((root % 12) + 12) % 12;
            custom.clear(); for (int v : chordTypes()[(size_t) juce::jlimit(0, 11, row * 3 + col)].iv) custom.push_back(root + v);
            playPad(); repaint(); return;
        }
        if (editKeys().contains(p)) {
            auto kr = editKeys(); static const bool BLK[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
            float ww = kr.getWidth() / 15; int k = -1;
            if (p.y < kr.getY() + kr.getHeight() * 0.58f) { int wi = 0; for (int x = 0; x < 25; ++x) { if (!BLK[x % 12]) { ++wi; continue; } float cx = kr.getX() + wi * ww; if (std::abs(p.x - cx) < ww * 0.32f) k = x; } }
            if (k < 0) { int wi = juce::jlimit(0, 14, (int) ((p.x - kr.getX()) / ww)); int c2 = 0; for (int x = 0; x < 25; ++x) if (!BLK[x % 12]) { if (c2 == wi) { k = x; break; } ++c2; } }
            if (k < 0) return;
            auto cur = padNotes(editPad);
            auto it = std::find(cur.begin(), cur.end(), k);
            if (it != cur.end()) cur.erase(it); else { cur.push_back(k); host().keys.noteOn(1, base() + k, 0.7f); host().keys.noteOff(1, base() + k, 0.0f); }
            std::sort(cur.begin(), cur.end()); custom = cur;
            repaint();
        }
    }
    void playPad() { if (host().latch) return; for (int v : padNotes(editPad)) host().keys.noteOn(1, base() + v, 0.7f);
        juce::Timer::callAfterDelay(450, [sp = juce::Component::SafePointer<KeyPad>(this), ns = padNotes(editPad), b = base()] { if (sp) for (int v : ns) sp->host().keys.noteOff(1, b + v, 0.0f); }); }
    void mouseDown(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true);
        if (mode == 1) { pad(e, 0); return; }
        if (mode == 2 && editing) { editTap(e.position); return; }
        int n = noteAt(e.position); if (n < 0) return;
        touches[e.source.getIndex()] = n; on(n, true, velAt(e.position)); repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (mode == 1) { pad(e, 1); return; }
        if (mode == 2 && editing) return;
        if (host().latch) return;                                       // HOLD: a drag does not sweep notes on and off
        int n = noteAt(e.position); auto it = touches.find(e.source.getIndex());
        if (n >= 0 && it != touches.end() && it->second != n) { on(it->second, false); it->second = n; on(n, true, velAt(e.position)); repaint(); } }
    void mouseUp(const juce::MouseEvent& e) override {
        if (mode == 1) { pad(e, 2); return; }
        auto it = touches.find(e.source.getIndex()); if (it != touches.end()) { if (!host().latch) on(it->second, false); touches.erase(it); } repaint(); }
};

// the notes HOLD keeps on, shown on the piano
class HeldOverlay : public juce::Component {
public:
    HeldOverlay(juce::MidiKeyboardComponent& k, AudioHost& h) : kb(k), host(h) { setInterceptsMouseClicks(false, false); }
    juce::MidiKeyboardComponent& kb; AudioHost& host;
    void paint(juce::Graphics& g) override {
        if (!host.latch) return;
        for (int n : host.latched) {
            auto r = kb.getRectangleForKey(n); if (r.isEmpty()) continue;
            r = r.translated((float) (kb.getX() - getX()), (float) (kb.getY() - getY()));
            g.setColour(Theme::violet()); g.fillEllipse(juce::Rectangle<float>(12, 12).withCentre({ r.getCentreX(), r.getBottom() - 14 }));
        }
    }
};


} // namespace arco
