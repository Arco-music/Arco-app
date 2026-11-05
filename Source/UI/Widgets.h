#pragma once
#include <JuceHeader.h>
#include <functional>
#include <map>
#include <tuple>
#include "Theme.h"
#include "../Engine/Engine.h"

namespace arco {

using ColourFn = std::function<juce::Colour()>;
using ValueFn = std::function<double()>;
using SetFn = std::function<void(double)>;

// 2.1 SOFT: surfaces raised out of one material, lit from the top left
// 2.4: real blurred shadows, as CSS box-shadow draws them (σ = blur / 2), made once per size and kept
namespace softblur {
    struct K { int w, h, r, b; bool inset; bool operator<(const K& o) const { return std::tie(w, h, r, b, inset) < std::tie(o.w, o.h, o.r, o.b, o.inset); } };
    struct M { juce::Image img; int pad; };
    inline void boxBlur(std::vector<float>& a, int W, int H, float sigma) {
        if (sigma < 0.3f) return;
        const int passes = 3;
        float wIdeal = std::sqrt(12.0f * sigma * sigma / passes + 1.0f);
        int wl = (int) std::floor(wIdeal); if (wl % 2 == 0) --wl; int wu = wl + 2;
        float mIdeal = (12.0f * sigma * sigma - passes * wl * wl - 4.0f * passes * wl - 3.0f * passes) / (-4.0f * wl - 4.0f);
        int m = (int) std::round(mIdeal);
        std::vector<float> t(a.size());
        for (int p = 0; p < passes; ++p) {
            int rad = ((p < m ? wl : wu) - 1) / 2; if (rad < 1) continue; float k = 1.0f / (2 * rad + 1);
            for (int y = 0; y < H; ++y) {                 // horizontal
                const float* src = &a[(size_t) y * W]; float* dst = &t[(size_t) y * W];
                float acc = src[0] * (rad + 1); for (int i = 1; i <= rad; ++i) acc += src[std::min(i, W - 1)];
                for (int x = 0; x < W; ++x) { dst[x] = acc * k; acc += src[std::min(x + rad + 1, W - 1)] - src[std::max(x - rad, 0)]; }
            }
            for (int x = 0; x < W; ++x) {                 // vertical
                float acc = t[(size_t) x] * (rad + 1); for (int i = 1; i <= rad; ++i) acc += t[(size_t) std::min(i, H - 1) * W + x];
                for (int y = 0; y < H; ++y) { a[(size_t) y * W + x] = acc * k; acc += t[(size_t) std::min(y + rad + 1, H - 1) * W + x] - t[(size_t) std::max(y - rad, 0) * W + x]; }
            }
        }
    }
    inline const M& get(int w, int h, int r, int blur, bool inset) {
        static std::map<K, M> cache;
        // sizes in steps of 2 px (a key that sinks while pressed reuses a few masks, not a new one every frame)
        w = (w + 1) & ~1; h = (h + 1) & ~1; r = juce::jmin(r, juce::jmin(w, h) / 2);
        K key { w, h, r, blur, inset };
        auto it = cache.find(key); if (it != cache.end()) return it->second;
        if (cache.size() > 2500) cache.clear();
        int pad = (int) std::ceil(blur * 1.2f) + 2, W = w + 2 * pad, H = h + 2 * pad;
        juce::Image shape(juce::Image::SingleChannel, W, H, true);
        { juce::Graphics sg(shape); sg.setColour(juce::Colours::white); sg.fillRoundedRectangle((float) pad, (float) pad, (float) w, (float) h, (float) r); }
        std::vector<float> a((size_t) W * H);
        { juce::Image::BitmapData bd(shape, juce::Image::BitmapData::readOnly);
          for (int y = 0; y < H; ++y) { const juce::uint8* row = bd.getLinePointer(y);
              for (int x = 0; x < W; ++x) { float v = row[(size_t) x * (size_t) bd.pixelStride] * (1.0f / 255.0f); a[(size_t) y * W + x] = inset ? 1.0f - v : v; } } }
        boxBlur(a, W, H, blur * 0.5f);
        juce::Image out(juce::Image::SingleChannel, W, H, false);
        { juce::Image::BitmapData bd(out, juce::Image::BitmapData::writeOnly);
          for (int y = 0; y < H; ++y) { juce::uint8* row = bd.getLinePointer(y);
              for (int x = 0; x < W; ++x) row[(size_t) x * (size_t) bd.pixelStride] = (juce::uint8) juce::jlimit(0, 255, (int) (a[(size_t) y * W + x] * 255.0f + 0.5f)); } }
        return cache[key] = M { out, pad };
    }
    inline void draw(juce::Graphics& g, const M& m, float x, float y, juce::Colour c) {
        static const bool off = std::getenv("ARCO_NOBLUR") != nullptr; if (off) return;
        // a soft shade needs no fine resampling (it is a blur): the fast path keeps the screen fluid on the iPad
        juce::Graphics::ScopedSaveState ss(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.setColour(c);
        g.drawImageTransformed(m.img, juce::AffineTransform::translation(x - m.pad, y - m.pad), true);
    }
}
// 2.2: the soft shades are drawn as a few layered vector shapes (a blurred image per key was too slow on the iPad:
// hundreds of keys on a screen). The layers spread from -sigma to +2 sigma round the edge, like a gaussian blur.
namespace softlayers {
    // the layers follow a real gaussian edge: a layer at distance e from the edge adds just enough that the stack
    // matches 0.5 * erfc(e / (sigma * sqrt 2)) - no rings, the same fall-off as the blurred page. A big shade gets
    // more layers (never more than ~1.2 px apart), so its steps can't be seen.
    struct W { std::vector<float> e, a; };
    inline const W& weights(int n) {
        static std::map<int, W> cache;
        auto it = cache.find(n); if (it != cache.end()) return it->second;
        W r; r.e.resize((size_t) n); r.a.resize((size_t) n);
        const float lo = -2.2f, hi = 2.6f; float step = (hi - lo) / (n - 1);
        for (int k = 0; k < n; ++k) r.e[(size_t) k] = lo + step * k;
        auto A = [](float t) { return 0.5f * std::erfc(t / std::sqrt(2.0f)); };
        std::vector<float> T((size_t) n + 1, 0.0f); for (int k = 0; k < n; ++k) T[(size_t) k] = A(r.e[(size_t) k] - step * 0.5f);
        for (int k = n - 1; k >= 0; --k) r.a[(size_t) k] = juce::jlimit(0.0f, 1.0f, 1.0f - (1.0f - T[(size_t) k]) / juce::jmax(1e-4f, 1.0f - T[(size_t) k + 1]));
        return cache[n] = r;
    }
    inline int layersFor(juce::Graphics& g, float sg) {
        float sc = std::sqrt(std::abs(g.getInternalContext().getPhysicalPixelScaleFactor()));   // device pixels per unit
        return juce::jlimit(7, 64, (int) std::ceil(4.8f * sg * juce::jmax(1.0f, sc) / 1.2f));
    }
    inline void outer(juce::Graphics& g, juce::Rectangle<float> r, float radius, float blur, juce::Colour c) {
        float sg = blur * 0.5f; int n = layersFor(g, sg); auto& w = weights(n); float ca = c.getFloatAlpha();
        for (int k = 0; k < n; ++k) {
            float e = w.e[(size_t) k] * sg;
            auto q = r.expanded(e); if (q.getWidth() < 1 || q.getHeight() < 1 || w.a[(size_t) k] < 0.001f) continue;
            g.setColour(c.withAlpha(ca * w.a[(size_t) k]));
            g.fillRoundedRectangle(q, juce::jmax(0.0f, radius + e));
        }
    }
    inline void inner(juce::Graphics& g, juce::Rectangle<float> clip, float radius, float dx, float dy, float blur, juce::Colour c) {
        float sg = blur * 0.5f; int n = layersFor(g, sg); auto& w = weights(n); float ca = c.getFloatAlpha();
        auto big = clip.expanded(blur * 3 + std::abs(dx) + std::abs(dy) + 4);
        for (int k = 0; k < n; ++k) {
            float e = w.e[(size_t) k] * sg;                         // the hole's edge moves inwards: the band fades into the key
            auto hole = clip.translated(dx, dy).reduced(e); if (hole.getWidth() < 1 || hole.getHeight() < 1 || w.a[(size_t) k] < 0.001f) continue;
            juce::Path p; p.addRectangle(big); p.addRoundedRectangle(hole, juce::jmax(0.0f, radius - e)); p.setUsingNonZeroWinding(false);
            g.setColour(c.withAlpha(ca * w.a[(size_t) k]));
            g.fillPath(p);
        }
    }
}
// raised out of the skin: CSS "d d blur lo, -d -d blur hi" (blur = 2.33 d, the page's 6 / 14)
inline void softShadow(juce::Graphics& g, juce::Rectangle<float> r, float radius, float d, float alpha = 1.0f) {
    if (d < 0.2f || alpha < 0.01f || r.getWidth() < 2 || r.getHeight() < 2) return;
    float rr = juce::jmin(radius, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f), blur = juce::jmax(1.0f, d * 2.33f);
    softlayers::outer(g, r.translated(-d, -d), rr, blur, Theme::hiC().withMultipliedAlpha(alpha));
    softlayers::outer(g, r.translated(d, d), rr, blur, Theme::loC().withMultipliedAlpha(alpha));
}
// pressed into the skin: CSS "inset d d blur lo, inset -d -d blur hi" (blur = 2 d, the page's 5 / 10)
inline void softInset(juce::Graphics& g, juce::Rectangle<float> r, float radius, float d, float alpha = 1.0f) {
    if (d < 0.2f || alpha < 0.01f || r.getWidth() < 2 || r.getHeight() < 2) return;
    float rr = juce::jmin(radius, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f), blur = juce::jmax(1.0f, d * 2.0f);
    juce::Graphics::ScopedSaveState ss(g);
    juce::Path cl; cl.addRoundedRectangle(r, rr); g.reduceClipRegion(cl);
    softlayers::inner(g, r, rr, d, d, blur, Theme::loC().withMultipliedAlpha(alpha));
    softlayers::inner(g, r, rr, -d, -d, blur, Theme::hiC().withMultipliedAlpha(alpha));
}
// the skin over a key: the very same material, flat (the shade alone makes it stand out)
inline void softFace(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour base) {
    g.setColour(base); g.fillRoundedRectangle(r, radius);
}
inline void softRaised(juce::Graphics& g, juce::Rectangle<float> r, float radius, float d, juce::Colour base) { softShadow(g, r, radius, d); softFace(g, r, radius, base); }
// a groove in the skin (slots, wells): CSS "inset 3 3 6" on the well colour
inline void softWell(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    g.setColour(Theme::card2()); g.fillRoundedRectangle(r, radius);
    softInset(g, r, radius, juce::jlimit(2.0f, 4.0f, juce::jmin(r.getWidth(), r.getHeight()) * 0.1f));
}
inline void softLed(juce::Graphics& g, juce::Point<float> c, float rad, juce::Colour col, bool on) {
    if (on) { auto& m = softblur::get(juce::roundToInt(rad * 2), juce::roundToInt(rad * 2), juce::roundToInt(rad), 16, false);
              softblur::draw(g, m, c.x - rad, c.y - rad, col.withAlpha(0.55f));
              auto& m2 = softblur::get(juce::roundToInt(rad * 2), juce::roundToInt(rad * 2), juce::roundToInt(rad), 6, false);
              softblur::draw(g, m2, c.x - rad, c.y - rad, col.withAlpha(0.9f));
              g.setColour(col); g.fillEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2); }
    else { g.setColour(Theme::sub().withAlpha(0.35f)); g.fillEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2); }
}


// 2.3: the silicone's spring - a key goes down quickly and comes back up with a slight bounce
struct SoftSpring : private juce::Timer {
    float v = 0, vel = 0, target = 0; bool init = false; bool slide = false;     // slide: moving sideways (a capsule gliding to its place)
    std::function<void()> onMove;
    void to(float t) {
        if (!init) { v = target = t; init = true; return; }
        if (std::abs(t - target) > 1e-4f) { target = t; if (!isTimerRunning()) startTimerHz(60); }
    }
    bool moving() const { return isTimerRunning(); }
private:
    void timerCallback() override {
        const bool down = target > v;
        const float k = slide ? 0.20f : (down ? 0.38f : 0.16f), damp = slide ? 0.30f : (down ? 0.50f : 0.24f);
        vel += (target - v) * k; vel *= (1.0f - damp); v += vel;
        if (std::abs(target - v) < (slide ? 0.002f : 0.003f) && std::abs(vel) < 0.003f) { v = target; vel = 0; stopTimer(); }
        if (onMove) onMove();
    }
};

// ---------------------------------------------------------------- a rounded button
class Pill : public juce::Component, public juce::SettableTooltipClient {
public:
    Pill(juce::String t, ColourFn bg_, float fs = 10.0f, float r = 7.0f) : text(std::move(t)), bg(std::move(bg_)), fsize(fs), radius(r) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        press.onMove = [this] { repaintWithParent(); }; }
    SoftSpring press;                // 2.3: how far the key is pressed into the skin (0 up, 1 down)
    float pressLevel() const { return press.v; }
    void repaintWithParent() { if (shadedByParent()) getParentComponent()->repaint(getBounds().expanded(20)); else repaint(); }
    std::function<void()> onClick;
    std::function<juce::String()> dynText;
    ColourFn textColour;
    juce::String text; ColourFn bg; float fsize, radius; int weight = 2;
    // 1.6: tint = an "on" colour shows as a soft wash with coloured text (solid only when solidFn says so);
    // seg = a part of a segmented control (1 left, 2 middle, 3 right, 4 alone): shared background, thin dividers
    bool tint = false; int seg = 0; std::function<bool()> solidFn; int icon = 0;   // icon: see drawIcon
    std::function<bool()> onFn;      // with tint: is it "on" (else: on = its colour differs from btn())
    std::function<int()> iconFn;     // 2.1: an icon that follows the state (play / stop)
    bool shadedByParent() const { auto* p = getParentComponent(); return p != nullptr && (bool) p->getProperties()["softShades"]; }
    static float keyDepth(juce::Rectangle<float> b) { return juce::jlimit(3.0f, 6.0f, b.getHeight() * 0.14f); }   // the page: 6 / 14 on a 44 key
    float softRadius(juce::Rectangle<float> b) const {
        if (Theme::softUI()) return b.getHeight() * 0.5f;          // 2.2: every key a capsule, round at the ends (as the design)
        float rr = seg == 0 ? radius : juce::jmin(10.0f, b.getHeight() * 0.32f);
        float sr = juce::jmin(b.getHeight() * 0.5f, juce::jmax(rr, 9.0f));
        if (std::abs(b.getWidth() - b.getHeight()) < 3.0f) sr = b.getHeight() * 0.5f;
        return sr; }
    // a key of the skin (not a bare word): its parent draws its shade, as deep as the key is up
    bool raisedNow() {
        auto c = bg ? bg() : Theme::btn();
        bool on = onFn ? onFn() : (c != Theme::btn() && c != Theme::card2() && c.getAlpha() > 0);
        bool solid = !tint ? (on && c.isOpaque() && c != Theme::btn() && c != Theme::card2()) : (solidFn && solidFn());
        return !(c.getAlpha() == 0 && !on && !solid); }
    bool mono = false;               // 2.1: the small monospace labels (IN 1 ▾)
    void paint(juce::Graphics& g) override {
        const int icon = iconFn ? iconFn() : this->icon;
        auto r = getLocalBounds().toFloat();
        auto c = bg ? bg() : Theme::btn();
        bool on = onFn ? onFn() : (c != Theme::btn() && c != Theme::card2() && c.getAlpha() > 0);
        juce::Colour fill = c, ink;
        if (tint && !(solidFn && solidFn())) {
            fill = on ? Theme::btn().interpolatedWith(c, Theme::lightMode() ? 0.28f : 0.20f) : (c.getAlpha() == 0 ? c : Theme::btn());
            ink = on ? Theme::ink(c) : Theme::sub();
        } else ink = Theme::textOn(c);
        if (seg != 0 && !on && !(solidFn && solidFn())) fill = Theme::btn();
        if (isMouseOver()) fill = fill.brighter(Theme::lightMode() ? 0.05f : 0.10f);
        if (isMouseButtonDown()) fill = fill.darker(0.12f);
        float rr = seg == 0 ? radius : juce::jmin(10.0f, r.getHeight() * 0.32f);
        if (Theme::glass()) rr = juce::jmin(r.getHeight() * 0.5f, juce::jmax(rr, 10.0f));
        bool led = false;
        if (Theme::softUI()) {          // 2.4: a key of silicone, as the page: raised (its shade drawn by the panel), pressed = the shade goes, the key goes in
            bool solid = !tint ? (on && c.isOpaque() && c != Theme::btn() && c != Theme::card2()) : (solidFn && solidFn());
            bool clear = c.getAlpha() == 0 && !on && !solid;
            const bool ext = shadedByParent();
            auto b = ext ? r : r.reduced(juce::jmin(5.0f, r.getHeight() * 0.12f));
            float sr = softRadius(b);
            if (clear) { press.to(0.0f); if (isMouseButtonDown()) { g.setColour(Theme::txt().withAlpha(0.06f)); g.fillRoundedRectangle(b, sr); }
                         ink = Theme::txt().withAlpha(isMouseOver() ? 1.0f : 0.8f); }
            else {
                const bool latched = (on && tint) || solid;
                press.to(isMouseButtonDown() ? 1.0f : latched ? 0.82f : 0.0f);
                const float pv = press.v, pd = juce::jlimit(0.0f, 1.0f, pv);
                if (!ext) softShadow(g, b, sr, keyDepth(b), 1.0f - pd);
                juce::Graphics::ScopedSaveState ss(g);
                float sc = 1.0f - 0.05f * juce::jlimit(-0.4f, 1.1f, pv);          // .97 latched, .95 under the finger, a little bounce past 1 when it comes back
                g.addTransform(juce::AffineTransform::scale(sc, sc, b.getCentreX(), b.getCentreY()));
                softFace(g, b, sr, Theme::btn());
                if (pd > 0.01f) softInset(g, b, sr, juce::jlimit(2.5f, 5.0f, b.getHeight() * 0.115f), pd);
                if (solid) {               // the colour shining up through the skin
                    juce::ColourGradient gl(c.withAlpha(0.34f), b.getCentreX(), b.getCentreY(), c.withAlpha(0.0f), b.getRight() + 4, b.getCentreY(), true);
                    g.setGradientFill(gl); g.fillRoundedRectangle(b, sr);
                    ink = Theme::ink(c);
                } else ink = on && !tint ? Theme::ink(c) : Theme::txt();
                led = on && tint && icon == 0 && !solid;
            }
            if (icon == 3) {              // the record dot: soft red under the skin; recording, it glows
                const bool hasText = (dynText ? dynText() : text).isNotEmpty();
                bool lit = hasText ? solid : (solid || (on && tint));
                ink = lit ? Theme::red() : Theme::red().withMultipliedAlpha(hasText ? 0.85f : 0.55f);
                if (lit && !hasText) { float is = juce::jmin((float) getHeight() * 0.42f, 22.0f); softLed(g, r.getCentre(), is * 0.5f, Theme::red(), true); }
            } else if (icon == 1 || icon == 2) { if (on && tint) ink = Theme::ink(c.getAlpha() > 0 && c != Theme::btn() ? c : Theme::blue()); }
            else if (solid && icon != 0) ink = Theme::ink(c);
        }
        else if (Theme::glass() && (seg == 0 || seg == 4)) {       // glass: a pill of light; "on" glows in its colour, solid is the colour itself
            bool solid = !tint ? (fill.isOpaque() && fill != Theme::btn()) : (solidFn && solidFn());
            if (solid) { g.setGradientFill(juce::ColourGradient(fill.brighter(0.12f), 0, r.getY(), fill.darker(0.08f), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, rr);
                         g.setColour(juce::Colours::white.withAlpha(0.25f)); g.drawRoundedRectangle(r.reduced(0.5f), rr, 1.0f); }
            else {
                g.setColour(on && tint ? c.withAlpha(isMouseOver() ? 0.24f : 0.17f) : juce::Colours::white.withAlpha(isMouseOver() ? 0.10f : (fill.getAlpha() == 0 ? 0.0f : 0.06f)));
                g.fillRoundedRectangle(r, rr);
                g.setColour(on && tint ? c.withAlpha(0.45f) : juce::Colours::white.withAlpha(fill.getAlpha() == 0 && !isMouseOver() ? 0.0f : 0.10f)); g.drawRoundedRectangle(r.reduced(0.5f), rr, 1.0f);
            }
        }
        else if (seg == 0 || seg == 4) { g.setColour(fill); g.fillRoundedRectangle(r, rr); }
        else {
            juce::Path p; bool L = seg == 1, R = seg == 3;
            p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), rr, rr, L, R, L, R);
            g.setColour(fill); g.fillPath(p);
            if (seg != 1) { g.setColour(Theme::line()); g.fillRect(r.getX(), r.getY() + 0.0f, 1.0f, r.getHeight()); }
        }
        auto tc = textColour ? textColour() : ink;
        if (Theme::softUI() && textColour && icon != 3) { auto t2 = textColour(); if (t2.getAlpha() > 0) tc = Theme::softUI() && Theme::contrast(Theme::flat(t2), Theme::btn()) < 3.0f ? ink : t2; }
        auto txt = dynText ? dynText() : text;
        if (!mono) txt = Theme::keyCase(txt);
        auto box = getLocalBounds().reduced(3, 0);
        if (led && txt.isNotEmpty()) {
            auto f = Theme::font(fsize, weight); float tw = juce::GlyphArrangement::getStringWidth(f, txt);
            float x0 = r.getCentreX() - (tw + 11.0f) * 0.5f;
            softLed(g, { x0 + 2.5f, r.getCentreY() }, 2.6f, c, true);
            g.setColour(tc); g.setFont(f); g.drawText(txt, juce::Rectangle<float>(x0 + 10.0f, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft); return;
        }
        if (icon != 0) {
            float is = juce::jmin((float) getHeight() * 0.42f, Theme::softUI() && icon == 3 ? 22.0f : 14.0f);
            float tw = txt.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth(Theme::font(fsize, weight), txt) + 5.0f;
            float x0 = r.getCentreX() - (is + tw) * 0.5f;
            drawIcon(g, icon, juce::Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), tc);
            box = box.withTrimmedLeft((int) (x0 + is + 5 - box.getX())).withWidth((int) tw + 4);
            if (txt.isEmpty()) return;
            g.setColour(tc); g.setFont(Theme::font(fsize, weight)); g.drawText(txt, box, juce::Justification::centredLeft); return;
        }
        g.setColour(tc);
        g.setFont(mono && Theme::softUI() ? Theme::mono(fsize) : Theme::font(fsize, weight));
        g.drawFittedText(mono && Theme::softUI() ? txt.toUpperCase() : txt, box, juce::Justification::centred, 1, 0.7f);
    }
    // 1 play  2 stop  3 rec dot  4 undo  5 headphones  6 folder  7 clear (x)  8 power  9 play/stop
    static void drawIcon(juce::Graphics& g, int k, juce::Rectangle<float> b, juce::Colour c) {
        g.setColour(c); float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
        juce::PathStrokeType st(juce::jmax(1.4f, w * 0.13f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (k == 1) { juce::Path p; p.addTriangle(x + w * 0.2f, y + h * 0.1f, x + w * 0.2f, y + h * 0.9f, x + w * 0.92f, y + h * 0.5f); g.fillPath(p); }
        else if (k == 2) g.fillRoundedRectangle(b.reduced(w * 0.14f), w * 0.12f);
        else if (k == 3) g.fillEllipse(b.reduced(w * 0.18f));
        else if (k == 4) { juce::Path p; p.addCentredArc(x + w * 0.52f, y + h * 0.56f, w * 0.36f, h * 0.32f, 0, -juce::MathConstants<float>::halfPi * 1.0f, juce::MathConstants<float>::pi * 0.75f, true);
                           g.strokePath(p, st); juce::Path a; a.addTriangle(x + w * 0.02f, y + h * 0.24f, x + w * 0.36f, y + h * 0.02f, x + w * 0.38f, y + h * 0.46f); g.fillPath(a); }
        else if (k == 5) { juce::Path p; p.addCentredArc(x + w * 0.5f, y + h * 0.56f, w * 0.38f, h * 0.42f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                           g.strokePath(p, st); g.fillRoundedRectangle(x + w * 0.06f, y + h * 0.55f, w * 0.22f, h * 0.38f, w * 0.06f); g.fillRoundedRectangle(x + w * 0.72f, y + h * 0.55f, w * 0.22f, h * 0.38f, w * 0.06f); }
        else if (k == 6) { juce::Path p; p.startNewSubPath(x + w * 0.06f, y + h * 0.22f); p.lineTo(x + w * 0.4f, y + h * 0.22f); p.lineTo(x + w * 0.5f, y + h * 0.34f); p.lineTo(x + w * 0.94f, y + h * 0.34f);
                           p.lineTo(x + w * 0.94f, y + h * 0.84f); p.lineTo(x + w * 0.06f, y + h * 0.84f); p.closeSubPath(); g.strokePath(p, st); }
        else if (k == 7) { g.drawLine(x + w * 0.2f, y + h * 0.2f, x + w * 0.8f, y + h * 0.8f, st.getStrokeThickness()); g.drawLine(x + w * 0.8f, y + h * 0.2f, x + w * 0.2f, y + h * 0.8f, st.getStrokeThickness()); }
        else if (k == 8) { juce::Path p; p.addCentredArc(x + w * 0.5f, y + h * 0.55f, w * 0.38f, h * 0.38f, 0, juce::MathConstants<float>::pi * 0.2f, juce::MathConstants<float>::pi * 1.8f, true);
                           g.strokePath(p, st); g.drawLine(x + w * 0.5f, y + h * 0.05f, x + w * 0.5f, y + h * 0.5f, st.getStrokeThickness()); }
        else if (k == 10) { float s = (w - w * 0.22f) / 2; for (int i = 0; i < 4; ++i) g.fillRoundedRectangle(x + (i % 2) * (s + w * 0.22f), y + (i / 2) * (s + h * 0.22f), s, s, s * 0.25f); }   // 2.0: HOME
        else if (k == 11) { float bw = juce::jmax(1.6f, w * 0.13f); g.fillRoundedRectangle(x + w * 0.08f, y + h * 0.16f, bw, h * 0.68f, bw * 0.4f);       // 2.3: back to the start
                            juce::Path p; p.addTriangle(x + w * 0.92f, y + h * 0.16f, x + w * 0.92f, y + h * 0.84f, x + w * 0.24f, y + h * 0.5f); g.fillPath(p); }
        else if (k == 12) {      // 2.3: light / dark - a sun in the dark look (tap: light), a moon in the light look (tap: dark)
            auto c = b.getCentre();
            if (!Theme::lightMode()) { float r = w * 0.2f; g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
                for (int i = 0; i < 8; ++i) { float a = i * juce::MathConstants<float>::twoPi / 8; g.drawLine(c.x + std::cos(a) * w * 0.32f, c.y + std::sin(a) * w * 0.32f, c.x + std::cos(a) * w * 0.46f, c.y + std::sin(a) * w * 0.46f, juce::jmax(1.3f, w * 0.09f)); } }
            else { juce::Path m; m.addEllipse(c.x - w * 0.40f, c.y - h * 0.40f, w * 0.8f, h * 0.8f); juce::Path cut; cut.addEllipse(c.x - w * 0.18f, c.y - h * 0.58f, w * 0.8f, h * 0.8f);
                   m.setUsingNonZeroWinding(false); m.addPath(cut); g.fillPath(m); } }
        else if (k == 13) {      // undo / redo arrows
            juce::Path p; p.addCentredArc(x + w * 0.52f, y + h * 0.58f, w * 0.34f, h * 0.30f, 0, -juce::MathConstants<float>::halfPi * 1.0f, juce::MathConstants<float>::pi * 0.75f, true);
            g.strokePath(p, st); juce::Path a; a.addTriangle(x + w * 0.98f, y + h * 0.26f, x + w * 0.64f, y + h * 0.04f, x + w * 0.62f, y + h * 0.48f); g.fillPath(a); }
        else if (k == 9) { juce::Path p; p.addTriangle(x, y + h * 0.18f, x, y + h * 0.82f, x + w * 0.5f, y + h * 0.5f); g.fillPath(p); g.fillRoundedRectangle(x + w * 0.6f, y + h * 0.26f, w * 0.4f, h * 0.48f, 1.5f); }
    }
    void mouseEnter(const juce::MouseEvent&) override { repaintWithParent(); }
    void mouseExit(const juce::MouseEvent&) override { repaintWithParent(); }
    void mouseDown(const juce::MouseEvent&) override { repaintWithParent(); }
    void mouseUp(const juce::MouseEvent& e) override { repaintWithParent(); if (getLocalBounds().contains(e.getPosition()) && onClick) onClick(); }
};

// 1.6: a panel of the new design (soft shade, hairline)
// 2.0: a sheet of frosted glass - a faint white body brighter at the top, a hairline that catches the light above
inline void drawGlass(juce::Graphics& g, juce::Rectangle<float> r, float radius, float strength = 1.0f) {
    // 2.1: crisp glass - a tight shadow, an even pane, a hairline edge and a thin light on the top edge (no haze)
    g.setColour(juce::Colours::black.withAlpha(0.22f * strength)); g.fillRoundedRectangle(r.translated(0, 3.0f).expanded(0.5f), radius + 0.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.105f * strength), 0, r.getY(), juce::Colours::white.withAlpha(0.075f * strength), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colours::white.withAlpha(0.13f * strength)); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    juce::ColourGradient edge(juce::Colours::white.withAlpha(0.30f * strength), 0, r.getY(), juce::Colours::white.withAlpha(0.0f), 0, r.getY() + juce::jmin(r.getHeight(), 24.0f), false);
    g.setGradientFill(edge); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f) {
    if (Theme::glass()) { drawGlass(g, r, radius + 2.0f); return; }
    if (Theme::softUI()) { softRaised(g, r, juce::jmin(32.0f, radius + 12.0f), 12.0f, Theme::card()); return; }
    if (Theme::lightMode()) { g.setColour(juce::Colour::fromRGBA(60, 50, 30, 16)); g.fillRoundedRectangle(r.translated(0, 2.5f).expanded(1.0f), radius); }
    else { g.setColour(juce::Colours::black.withAlpha(0.32f)); g.fillRoundedRectangle(r.translated(0, 4.0f).expanded(1.0f), radius); }
    g.setGradientFill(juce::ColourGradient(Theme::card().brighter(Theme::lightMode() ? 0.01f : 0.025f), 0, r.getY(), Theme::card(), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(Theme::lightMode() ? juce::Colour::fromRGBA(60, 50, 30, 18) : juce::Colours::white.withAlpha(0.035f)); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}
// 2.3: a caption (KIT, PATTERN, PAN, SWING ...): in the silicone skin the station's small mono, in capitals
inline void drawCaption(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft, float fs = 7.2f) {
    if (Theme::softUI()) { g.setColour(Theme::sub()); g.setFont(Theme::mono(fs)); g.drawFittedText(t.toUpperCase(), r, j, 1, 0.8f); return; }
    g.setColour(Theme::sub()); g.setFont(Theme::font(fs + 1.5f, 2)); g.drawFittedText(t, r, j, 1, 0.8f);
}
// 2.3: a panel whose shade its parent draws (a whole component: a shade of its own would be cut at its edges - a hard
// grey corner by every rounded one)
inline void drawPanelFace(juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f) {
    if (Theme::softUI()) { softFace(g, r, juce::jmin(32.0f, radius + 12.0f), Theme::card()); return; }
    drawPanel(g, r, radius);
}
inline void drawToggle(juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour col) {
    if (Theme::softUI()) {
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, r.getHeight() * 0.5f); softInset(g, r, r.getHeight() * 0.5f, 3.0f);
        float d = r.getHeight() * 0.72f, pad = (r.getHeight() - d) * 0.5f; float x = on ? r.getRight() - pad - d : r.getX() + pad;
        auto k = juce::Rectangle<float>(x, r.getY() + pad, d, d);
        if (on) { softLed(g, k.getCentre(), d * 0.5f, col, true); }
        else softRaised(g, k, d * 0.5f, 2.0f, Theme::btn());
        return;
    }
    g.setColour(on ? col : Theme::line()); g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    float d = r.getHeight() - 4; float x = on ? r.getRight() - 2 - d : r.getX() + 2;
    g.setColour(juce::Colours::black.withAlpha(0.25f)); g.fillEllipse(x, r.getY() + 2.6f, d, d);
    g.setColour(on ? juce::Colours::white : Theme::knobHi()); g.fillEllipse(x, r.getY() + 2, d, d);
}

// ---------------------------------------------------------------- an arc knob (drag up / down, double-click = back to start)
class Knob : public juce::Component, public juce::SettableTooltipClient {
public:
    Knob(ValueFn get_, SetFn set_, double lo_, double hi_, double def_, ColourFn col_) :
        get(std::move(get_)), set(std::move(set_)), lo(lo_), hi(hi_), def(def_), col(std::move(col_)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); press.onMove = [this] { repaint(); }; }
    SoftSpring press; float dragDy = 0;          // 2.3: the dome under the finger
    std::function<double()> liveOffset;          // 2.4: an LFO moving this knob: how far from where it is set (in its own units)
    ValueFn get; SetFn set; double lo, hi, def; ColourFn col;
    bool enabled = true;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        float d = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.getCentre(); float rad = d * 0.5f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        double v = get ? get() : def;
        float t = (float) ((v - lo) / juce::jmax(1e-9, hi - lo));
        if (Theme::softUI()) {          // 2.4: as the page - a groove in the skin with the light running under it; a dome that goes in when touched
            press.to(dragging ? 1.0f : 0.0f);
            const float pv = press.v, pd = juce::jlimit(0.0f, 1.0f, pv);
            const float S = d, k = S / 94.0f;                       // the page's knob: a ring of 88 (+ its 6 wide groove) round a dome of 60
            const float sw = juce::jmax(2.6f, 6.0f * k), R = S * 0.5f - sw * 0.5f - 0.5f;
            juce::PathStrokeType st(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            juce::Path tr; tr.addCentredArc(c.x, c.y, R, R, 0, a0, a1, true);
            g.setColour(Theme::card2()); g.strokePath(tr, st);                                   // the groove
            float start = lo < 0 && hi > 0 ? (float) ((0 - lo) / (hi - lo)) : 0.0f;
            float ta = a0 + (a1 - a0) * juce::jmin(start, t), tb = a0 + (a1 - a0) * juce::jmax(start, t);
            auto cc = enabled ? Theme::arc(col ? col() : Theme::txt()) : Theme::line();
            if (tb - ta > 0.001f) {             // the light under the skin: a wide blur (10 / 5) and the line (3.5 / 1.2)
                juce::Path va; va.addCentredArc(c.x, c.y, R, R, 0, ta, tb, true);
                const float ga = 0.55f + 0.3f * pd;
                for (int i = 6; i >= 1; --i) { float wv = (10.0f + i * 2.4f) * k; g.setColour(cc.withAlpha(ga * 0.13f * (7 - i) / 6.0f));
                    g.strokePath(va, juce::PathStrokeType(juce::jmax(2.0f, wv), juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
                g.setColour(cc.withAlpha(0.55f)); g.strokePath(va, juce::PathStrokeType(juce::jmax(1.8f, 4.6f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(cc); g.strokePath(va, juce::PathStrokeType(juce::jmax(1.4f, 3.0f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            float an = a0 + (a1 - a0) * t - juce::MathConstants<float>::halfPi;
            double off = liveOffset ? liveOffset() : 0.0;
            float tl = juce::jlimit(0.0f, 1.0f, (float) ((v + off - lo) / juce::jmax(1e-9, hi - lo)));
            const bool moving = std::abs(off) > 1e-6 * (hi - lo);
            float anl = a0 + (a1 - a0) * tl - juce::MathConstants<float>::halfPi;
            if (moving) {              // the LFO's swing: a soft band from where it is set to where it is now
                juce::Path sb; sb.addCentredArc(c.x, c.y, R, R, 0, a0 + (a1 - a0) * juce::jmin(t, tl), a0 + (a1 - a0) * juce::jmax(t, tl), true);
                g.setColour(cc.withAlpha(0.35f)); g.strokePath(sb, juce::PathStrokeType(juce::jmax(2.0f, 5.0f * k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(Theme::sub().withAlpha(0.8f)); g.fillEllipse(c.x + std::cos(an) * R - 1.5f, c.y + std::sin(an) * R - 1.5f, 3.0f, 3.0f);    // where it is set
            }
            if (enabled) softLed(g, { c.x + std::cos(moving ? anl : an) * R, c.y + std::sin(moving ? anl : an) * R }, juce::jmax(2.0f, 3.6f * k), cc, true);   // the point of light
            // the dome (60 of 84), its shade 5 / 12; touched: in (inset 4 / 9), smaller, pulled the way the finger goes
            float fr = R * 0.68f * (1.0f - 0.06f * pv);
            juce::Point<float> fc(c.x, c.y + juce::jlimit(-4.0f, 4.0f, -dragDy * 0.05f) * pd);
            auto face = juce::Rectangle<float>(fc.x - fr, fc.y - fr, fr * 2, fr * 2);
            softShadow(g, face, fr, 5.0f * k, 1.0f - pd);
            auto base = Theme::btn();
            juce::ColourGradient dg(base.interpolatedWith(Theme::hiC(), 0.18f), face.getX() + fr * 0.76f, face.getY() + fr * 0.64f, base.interpolatedWith(Theme::loC(), 0.15f), face.getRight(), face.getBottom(), true);
            dg.addColour(0.6, base); g.setGradientFill(dg); g.fillEllipse(face);
            if (pd > 0.01f) softInset(g, face, fr, juce::jmax(2.0f, 4.0f * k), pd);
            if (moving) {              // the dome turns with the LFO: a little mark on it, going left and right
                g.setColour(cc.withAlpha(0.9f));
                g.drawLine(fc.x + std::cos(anl) * fr * 0.45f, fc.y + std::sin(anl) * fr * 0.45f, fc.x + std::cos(anl) * fr * 0.80f, fc.y + std::sin(anl) * fr * 0.80f, juce::jmax(1.8f, 3.2f * k));
            }
            if (pd > 0.05f) {          // the value, in the dome, while it is touched
                juce::String vt = lo < 0 && hi > 0 && std::abs(lo + hi) < 1e-6 ? (std::abs(v) < 0.5 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String((int) std::round(std::abs(v))))
                                                                                : juce::String(v, std::abs(hi - lo) <= 2 ? 2 : 0);
                g.setColour(Theme::txt().withAlpha(pd)); g.setFont(Theme::font(juce::jlimit(8.0f, 15.0f, fr * 0.5f), 1));
                g.drawText(vt, face.toNearestInt(), juce::Justification::centred);
            }
            return;
        }
        // 1.6: the track ring, the value arc, a shaded face and a dot (as the new design)
        float sw = juce::jmax(2.2f, d * 0.065f), ring = rad - sw * 0.5f;
        juce::Path tr; tr.addCentredArc(c.x, c.y, ring, ring, 0, a0, a1, true);
        if (Theme::glass()) sw = juce::jmax(2.0f, d * 0.05f);
        g.setColour(Theme::glass() ? juce::Colours::white.withAlpha(0.10f) : Theme::line()); g.strokePath(tr, juce::PathStrokeType(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float start = lo < 0 && hi > 0 ? (float) ((0 - lo) / (hi - lo)) : 0.0f;
        float ta = a0 + (a1 - a0) * juce::jmin(start, t), tb = a0 + (a1 - a0) * juce::jmax(start, t);
        auto cc = enabled ? Theme::arc(col ? col() : Theme::txt()) : Theme::line();
        if (tb - ta > 0.001f) {
            juce::Path va; va.addCentredArc(c.x, c.y, ring, ring, 0, ta, tb, true);
            if (Theme::glass()) { g.setColour(cc.withAlpha(0.22f)); g.strokePath(va, juce::PathStrokeType(sw * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }   // a soft glow
            g.setColour(cc); g.strokePath(va, juce::PathStrokeType(sw * 1.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        float fr = ring - sw * 0.5f - juce::jmax(2.0f, d * 0.07f);
        auto face = juce::Rectangle<float>(c.x - fr, c.y - fr, fr * 2, fr * 2);
        if (Theme::lightMode()) { g.setColour(juce::Colours::black.withAlpha(0.10f)); g.fillEllipse(face.translated(0, 1.5f)); }
        else { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillEllipse(face.translated(0, 2.0f)); }
        g.setGradientFill(juce::ColourGradient(Theme::knobHi(), face.getX() + fr * 0.8f, face.getY() + fr * 0.64f, Theme::knobLo(), face.getRight(), face.getBottom(), true));
        g.fillEllipse(face);
        g.setColour(Theme::knobEdge()); g.drawEllipse(face, 1.0f);
        float an = a0 + (a1 - a0) * t - juce::MathConstants<float>::halfPi;
        float pr = fr - juce::jmax(3.5f, fr * 0.28f), dr = juce::jmax(1.6f, d / 26.0f);
        g.setColour(enabled ? Theme::knobDot() : Theme::sub());
        g.fillEllipse(c.x + std::cos(an) * pr - dr, c.y + std::sin(an) * pr - dr, dr * 2, dr * 2);
        if (dragging) {
            g.setColour(Theme::txt()); g.setFont(Theme::font(8.0f, 1));
            g.drawText(juce::String(v, std::abs(hi - lo) <= 2 ? 2 : 0), getLocalBounds().withY(getHeight() / 2 - 6).withHeight(12), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); start = get ? get() : def; dragging = true; startY = e.position.y; dragDy = 0; repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        dragDy = startY - e.position.y;
        if (!enabled) { repaint(); return; }
        double range = hi - lo, sens = e.mods.isShiftDown() ? 600.0 : 160.0;
        double v = juce::jlimit(lo, hi, start + (startY - e.position.y) / sens * range);
        if (set) set(v); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; dragDy = 0; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (set && enabled) set(def); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (set && enabled) set(juce::jlimit(lo, hi, (get ? get() : def) + w.deltaY * (hi - lo) * 0.1)); repaint(); }
private:
    double start = 0; float startY = 0; bool dragging = false;
};

// ---------------------------------------------------------------- a vertical fader
class Fader : public juce::Component {
public:
    Fader(ValueFn g, SetFn s, double l, double h, double d) : get(std::move(g)), set(std::move(s)), lo(l), hi(h), def(d) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    ValueFn get; SetFn set; double lo, hi, def;
    // 1.6: the strip fader - a long thin track with a dB scale, a big handle in the strip's colour
    ColourFn col; bool scale = false; bool horiz = false;     // 2.1: horiz = a wide fader lying across the strip
    static double volToDb(double v) { double g = std::pow(juce::jmax(0.0, v) / 100.0, 2.0) * 2.0; return g <= 1e-6 ? -120.0 : 20.0 * std::log10(g); }
    static double dbToVol(double d) { return 100.0 * std::sqrt(std::pow(10.0, d / 20.0) / 2.0); }
    void paint(juce::Graphics& gr) override {
        if (horiz) { paintHoriz(gr); return; }
        if (scale) { paintScale(gr); return; }
        auto r = getLocalBounds().toFloat();
        gr.setColour(Theme::card2()); gr.fillRoundedRectangle(r, r.getWidth() * 0.5f);
        float t = (float) ((get() - lo) / (hi - lo));
        auto fill = r.withTop(r.getBottom() - r.getHeight() * t);
        gr.setColour(Theme::txt().withAlpha(0.18f)); gr.fillRoundedRectangle(fill, r.getWidth() * 0.5f);
        float y = r.getBottom() - r.getHeight() * t;
        auto cap = juce::Rectangle<float>(r.getX() - 1, juce::jlimit(r.getY(), r.getBottom() - 8.0f, y - 4.0f), r.getWidth() + 2, 8.0f);
        gr.setColour(Theme::txt()); gr.fillRoundedRectangle(cap, 3.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true);
        startV = get(); startY = e.position.y; startX = e.position.x;
        if (horiz) {        // a tap on the track jumps there; a drag on the handle moves it
            float cx = capX(); if (std::abs(e.position.x - cx) > capD() * 0.7f) { double v = valueAtX(e.position.x); set(v); startV = v; repaint(); } } }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (horiz) { double v = juce::jlimit(lo, hi, startV + (e.position.x - startX) / (juce::jmax(20.0f, getWidth() - capD()) * (e.mods.isShiftDown() ? 4.0 : 1.0)) * (hi - lo));
                     set(v); repaint(); return; }
        double v = juce::jlimit(lo, hi, startV + (startY - e.position.y) / (getHeight() * (e.mods.isShiftDown() ? 4.0 : 1.0)) * (hi - lo));
        set(v); repaint(); }
    float capD() const { return juce::jlimit(16.0f, 26.0f, getHeight() - 6.0f); }
    float capX() const { float d = capD(); return d * 0.5f + (getWidth() - d) * (float) ((juce::jlimit(lo, hi, get()) - lo) / (hi - lo)); }
    double valueAtX(float x) const { float d = capD(); return juce::jlimit(lo, hi, lo + (hi - lo) * (double) ((x - d * 0.5f) / juce::jmax(1.0f, getWidth() - d))); }
    void paintHoriz(juce::Graphics& gr) {
        auto b = getLocalBounds().toFloat();
        float d = capD(), cy = b.getCentreY(), x0 = d * 0.5f, x1 = b.getWidth() - d * 0.5f;
        auto slot = juce::Rectangle<float>(x0 - 6.0f, cy - 5.0f, x1 - x0 + 12.0f, 10.0f);
        if (Theme::softUI()) softWell(gr, slot, 5.0f); else { gr.setColour(Theme::card2()); gr.fillRoundedRectangle(slot, 5.0f); }
        auto c = Theme::arc(col ? col() : Theme::txt());
        float cx = capX();
        // 0 dB mark
        float zx = x0 + (x1 - x0) * (float) ((dbToVol(0) - lo) / (hi - lo));
        gr.setColour(Theme::sub().withAlpha(0.6f)); gr.fillRect(zx - 0.5f, cy + 8.0f, 1.0f, 4.0f);
        if (cx > x0 + 1) { gr.setColour(c.withAlpha(0.22f)); gr.fillRoundedRectangle(x0 - 2.0f, cy - 2.5f, cx - x0 + 2.0f, 5.0f, 2.5f);
                           gr.setColour(c); gr.fillRoundedRectangle(x0 - 1.0f, cy - 1.25f, cx - x0, 2.5f, 1.25f); }
        auto cap = juce::Rectangle<float>(cx - d * 0.5f, cy - d * 0.5f, d, d);
        if (Theme::softUI()) softRaised(gr, cap, d * 0.5f, 3.0f, Theme::btn());
        else { gr.setColour(Theme::txt()); gr.fillEllipse(cap); }
        gr.setColour(Theme::sub()); gr.fillRoundedRectangle(cx - 1.0f, cy - d * 0.22f, 2.0f, d * 0.44f, 1.0f);
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { set(def); repaint(); }
    float capH() const { return juce::jlimit(22.0f, 34.0f, getWidth() * 0.5f); }
    void paintScale(juce::Graphics& gr) {
        auto b = getLocalBounds().toFloat();
        bool labels = b.getWidth() >= 58;
        float lx = labels ? 18.0f : 0.0f;
        float cx = lx + (b.getWidth() - lx) * 0.5f, ch = capH(), top = ch * 0.5f, bot = b.getHeight() - ch * 0.5f, len = bot - top;
        float cw = juce::jmin(60.0f, b.getWidth() - lx);
        auto yOf = [&](double v) { return bot - len * (float) ((v - lo) / (hi - lo)); };
        // the scale: +6 0 -6 -12 -24 -48 -inf
        const double marks[7] = { 6, 0, -6, -12, -24, -48, -200 };
        const char* names[7] = { "+6", "0", "-6", "-12", "-24", "-48", "" };
        for (int i = 0; i < 7; ++i) {
            float y = i == 6 ? bot : yOf(juce::jlimit(lo, hi, dbToVol(marks[i])));
            gr.setColour(Theme::line().withMultipliedAlpha(i == 1 ? 1.0f : 0.75f));
            gr.fillRect(cx - cw * 0.5f - 3, y - 0.5f, cw + 6, i == 1 ? 1.5f : 1.0f);
            if (labels && i < 6) { gr.setColour(Theme::sub()); gr.setFont(Theme::font(7.0f, i == 1 ? 2 : 0));
                gr.drawText(names[i], juce::Rectangle<float>(0, y - 6, lx - 2, 12), juce::Justification::centredRight); }
        }
        auto c = col ? col() : Theme::txt();
        auto track = juce::Rectangle<float>(cx - 3.5f, top, 7.0f, len);
        gr.setColour(Theme::card2()); gr.fillRoundedRectangle(track, 3.5f);
        float y = yOf(juce::jlimit(lo, hi, get()));
        gr.setColour(c.withAlpha(0.85f)); gr.fillRoundedRectangle(track.withTop(y), 3.5f);
        // the handle
        auto cap = juce::Rectangle<float>(cx - cw * 0.5f, y - ch * 0.5f, cw, ch);
        gr.setColour(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.16f : 0.45f)); gr.fillRoundedRectangle(cap.translated(0, 2.5f).expanded(0.5f), 9.0f);
        auto top1 = Theme::lightMode() ? juce::Colour(0xffefeee9) : juce::Colour(0xffe6e8ec), bot1 = Theme::lightMode() ? juce::Colour(0xffd6d4cd) : juce::Colour(0xffb9bcc4);
        if (Theme::glass()) { top1 = juce::Colour(0xff3a3a40); bot1 = juce::Colour(0xff1b1b1f); }
        gr.setGradientFill(juce::ColourGradient(top1, cap.getX(), cap.getY(), bot1, cap.getX(), cap.getBottom(), false));
        gr.fillRoundedRectangle(cap, 9.0f);
        gr.setColour(Theme::glass() ? juce::Colours::white.withAlpha(0.28f) : juce::Colour(0xff8a8e97).withAlpha(0.7f)); gr.drawRoundedRectangle(cap.reduced(0.5f), 9.0f, 1.0f);
        gr.setColour(juce::Colour(0xff6b6f78)); gr.fillRect(cap.getCentreX() - cw * 0.28f, cap.getCentreY() - ch * 0.2f, cw * 0.56f, 1.3f);
        gr.fillRect(cap.getCentreX() - cw * 0.28f, cap.getCentreY() + ch * 0.2f - 1.3f, cw * 0.56f, 1.3f);
        gr.setColour(c); gr.fillRect(cap.getCentreX() - cw * 0.34f, cap.getCentreY() - 1.0f, cw * 0.68f, 2.0f);
    }
private:
    double startV = 0; float startY = 0, startX = 0;
};

// ---------------------------------------------------------------- a number (drag up / down; double-click to type)
class ValueBox : public juce::Component {
public:
    ValueBox(ValueFn g, SetFn s, double l, double h, int decimals = 0, ColourFn tri = nullptr) :
        get(std::move(g)), set(std::move(s)), lo(l), hi(h), dec(decimals), triCol(std::move(tri)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    ValueFn get; SetFn set; double lo, hi; int dec; ColourFn triCol; float fsize = 12.5f; bool readOnly = false; bool flat = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (!flat) { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 5.0f); }
        if (!readOnly && triCol && !(Theme::softUI() && flat && getComponentID() == "t:ntr")) {
            juce::Path p; float h = r.getHeight() * 0.3f; float x = 5, y = r.getCentreY();
            p.addTriangle(x, y - h * 0.5f, x, y + h * 0.5f, x + h * 0.8f, y);
            g.setColour(triCol()); g.fillPath(p);
        }
        g.setColour(Theme::txt()); g.setFont(Theme::font(fsize, 1));
        bool centre = readOnly || (Theme::softUI() && getComponentID() == "t:ntr");
        g.drawText(juce::String(get(), dec), getLocalBounds().reduced(4, 0).withTrimmedLeft(centre ? 0 : 6),
                   centre ? juce::Justification::centred : juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); startV = get(); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (readOnly) return;
        double step = dec > 0 ? 0.1 : 1.0;
        double v = juce::jlimit(lo, hi, startV + std::round((startY - e.position.y) / 4.0) * step * (e.mods.isShiftDown() ? 0.1 : 1.0));
        set(v); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (readOnly) return;
        auto* ed = new juce::TextEditor(); ed->setText(juce::String(get(), dec)); ed->setBounds(getLocalBounds());
        ed->setFont(Theme::font(fsize, 1)); ed->setJustification(juce::Justification::centred); ed->selectAll();
        ed->onReturnKey = [this, ed] { set(juce::jlimit(lo, hi, ed->getText().getDoubleValue())); juce::MessageManager::callAsync([ed] { delete ed; }); repaint(); };
        ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
        addAndMakeVisible(ed); ed->grabKeyboardFocus();
    }
private:
    double startV = 0; float startY = 0;
};

// ---------------------------------------------------------------- a level meter (green -> amber -> red), with decay
class Meter : public juce::Component {
public:
    explicit Meter(std::function<float()> peakFn, bool vertical = false) : peak(std::move(peakFn)), vert(vertical) { setInterceptsMouseClicks(false, false); }
    void update() { float p = peak(); level = juce::jmax(p, level * 0.86f); if (p > 0.98f) clipHold = 40; else if (clipHold > 0) --clipHold; repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
        float db = juce::Decibels::gainToDecibels(level, -60.0f);
        float t = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto col = db > -3 ? Theme::red() : (db > -12 ? Theme::amber() : Theme::green());
        g.setColour(clipHold > 0 ? Theme::red() : col);
        auto f = vert ? r.withTop(r.getBottom() - r.getHeight() * t) : r.withWidth(r.getWidth() * t);
        if (t > 0.01f) g.fillRoundedRectangle(f, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
    }
private:
    std::function<float()> peak; bool vert; float level = 0; int clipHold = 0;
};

// ---------------------------------------------------------------- the wave screen of a strip (reads the SCOPE buffer)
class WaveScreen : public juce::Component, public juce::FileDragAndDropTarget {
public:
    WaveScreen(Engine& e, std::function<SharedBuffer*()> buf_, std::function<int()> ch_, ColourFn col_) :
        engine(e), buf(std::move(buf_)), ch(std::move(ch_)), col(std::move(col_)) {}
    std::function<void(const juce::File&)> onDrop;
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> overlay;     // 1.6: the state written inside the screen
    void paint(juce::Graphics& g) override {
        auto full = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(full, 9.0f);
        auto r = overlay ? full.withTrimmedTop(15.0f).reduced(7.0f, 4.0f) : full;
        auto* b = buf(); int c = ch();
        if (b != nullptr && c >= 0 && c < b->ch && b->len > 4) {
            int npt = (int) (b->len / 2);
            juce::Path p; float cy = r.getCentreY(), h = r.getHeight() * 0.46f;
            const float* d = b->chan(c);
            p.startNewSubPath(r.getX(), cy);
            for (int i = 0; i < npt; ++i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy - juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            for (int i = npt - 1; i >= 0; --i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy + juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            p.closeSubPath();
            g.setColour(Theme::arc(col())); g.fillPath(p);
        }
        if (overlay) overlay(g, full);
        if (dragOver) { g.setColour(Theme::amber()); g.drawRoundedRectangle(full.reduced(1), 9.0f, 2.0f);
            g.setFont(Theme::font(8.5f, 2)); g.drawText("drop to import", getLocalBounds(), juce::Justification::centred); }
    }
    bool isInterestedInFileDrag(const juce::StringArray&) override { return onDrop != nullptr; }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override { dragOver = false; repaint(); if (onDrop && files.size() > 0) onDrop(juce::File(files[0])); }
private:
    Engine& engine; std::function<SharedBuffer*()> buf; std::function<int()> ch; ColourFn col; bool dragOver = false;
};

// ---------------------------------------------------------------- the little screen of an effect card
class MiniScope : public juce::Component {
public:
    MiniScope(Ring* r, ColourFn c) : ring(r), col(std::move(c)) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 5.0f);
        if (ring == nullptr) return;
        int w = ring->w.load(std::memory_order_acquire);
        const int n = 512;
        juce::Path p;
        float cy = r.getCentreY(), h = r.getHeight() * 0.8f;
        for (int i = 0; i < n; ++i) {
            float v = ring->d[(size_t) ((w - n + i + Ring::N) % Ring::N)];
            float x = r.getX() + 3 + (r.getWidth() - 6) * i / (float) (n - 1), y = cy - juce::jlimit(-0.5f, 0.5f, v) * h;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(Theme::arc(col())); g.strokePath(p, juce::PathStrokeType(1.2f));
    }
private:
    Ring* ring; ColourFn col;
};

// ---------------------------------------------------------------- MIDI learn: every knob, fader, number and button
namespace learn {
    inline bool isLearnable(juce::Component* c) {
        return dynamic_cast<Knob*>(c) || dynamic_cast<Fader*>(c) || dynamic_cast<ValueBox*>(c) || dynamic_cast<Pill*>(c); }
    inline juce::Component* widgetAt(juce::Component* c) { while (c != nullptr && !isLearnable(c)) c = c->getParentComponent(); return c; }
    // a stable name: the component ID when it has one ("p:vol4", "trk2:rec" ...), otherwise its place in the window
    inline juce::String idOf(juce::Component* w, juce::Component* root) {
        if (w->getComponentID().isNotEmpty()) return w->getComponentID();
        juce::StringArray path;
        for (auto* c = w; c != nullptr && c != root; c = c->getParentComponent()) {
            auto* p = c->getParentComponent(); if (p == nullptr) break;
            path.insert(0, juce::String(p->getIndexOfChildComponent(c)));
        }
        return "path:" + path.joinIntoString("/");
    }
    inline juce::Component* find(juce::Component* root, const juce::String& id) {
        if (id.startsWith("path:")) {
            auto* c = root;
            for (auto& t : juce::StringArray::fromTokens(id.substring(5), "/", "")) {
                int i = t.getIntValue(); if (c == nullptr || i < 0 || i >= c->getNumChildComponents()) return nullptr; c = c->getChildComponent(i); }
            return isLearnable(c) ? c : nullptr;
        }
        std::function<juce::Component*(juce::Component*)> rec = [&](juce::Component* c) -> juce::Component* {
            if (c->getComponentID() == id) return c;
            for (auto* k : c->getChildren()) if (auto* r = rec(k)) return r;
            return nullptr; };
        return rec(root);
    }
    // a MIDI value 0..127 (controllers) or a press (keys, pads, footswitches)
    inline void apply(juce::Component* w, int type, int value, bool press) {
        double t = value / 127.0;
        if (auto* k = dynamic_cast<Knob*>(w)) {
            if (type == 2) k->set(k->lo + t * (k->hi - k->lo));
            else if (press) { double v = k->get ? k->get() : k->lo; k->set(v > (k->lo + k->hi) * 0.5 ? k->lo : k->hi); }
        } else if (auto* f = dynamic_cast<Fader*>(w)) {
            if (type == 2) f->set(f->lo + t * (f->hi - f->lo));
        } else if (auto* b = dynamic_cast<ValueBox*>(w)) {
            if (type == 2 && !b->readOnly) b->set(std::round(b->lo + t * (b->hi - b->lo)));
        } else if (auto* p = dynamic_cast<Pill*>(w)) {
            if (press && p->onClick) p->onClick();
        }
        w->repaint();
    }
}


// 2.2: the big soft shades of a component's keys, drawn by the component under them (they reach outside the keys)
inline void drawSoftShades(juce::Graphics& g, juce::Component& parent) {
    if (!Theme::softUI()) return;
    for (auto* ch : parent.getChildren()) {
        if (!ch->isVisible()) continue;
        if (auto* p = dynamic_cast<Pill*>(ch)) {
            if (!p->raisedNow()) continue;
            auto b = p->getBounds().toFloat(); float sr = p->softRadius(b);
            float pv = juce::jlimit(-0.4f, 1.0f, p->pressLevel());
            softShadow(g, b, sr, Pill::keyDepth(b), juce::jlimit(0.0f, 1.0f, 1.0f - pv));
            if (p->isMouseButtonDown() && pv > 0.05f) softInset(g, b.expanded(10.0f), sr + 10.0f, 3.0f, 0.55f * juce::jlimit(0.0f, 1.0f, pv));   // the skin round it dips in too
        }
    }
}

// 2.3: light silicone keys for big grids (drum steps, loop cells): a short shade, a dent when pressed, colour shining up through the skin
inline void softKeyLite(juce::Graphics& g, juce::Rectangle<float> r, float radius, bool pressed, juce::Colour glow = {}, float glowAmt = 0.0f) {
    const bool light = Theme::lightMode();
    float d = juce::jlimit(2.0f, 4.0f, juce::jmin(r.getWidth(), r.getHeight()) * 0.09f);
    if (!pressed) { softShadow(g, r, radius, d); softFace(g, r, radius, Theme::btn()); }
    else {
        auto k = r.reduced(r.getWidth() * 0.015f, r.getHeight() * 0.015f);
        softFace(g, k, radius, Theme::btn()); softInset(g, k, radius, d * 0.9f);
    }
    if (glowAmt > 0.001f) {
        juce::Graphics::ScopedSaveState ss(g); juce::Path cl; cl.addRoundedRectangle(r, radius); g.reduceClipRegion(cl);
        float a = juce::jlimit(0.0f, 1.0f, glowAmt);
        juce::ColourGradient gr(glow.withAlpha(a * (light ? 0.75f : 0.85f)), r.getCentreX(), r.getCentreY(), glow.withAlpha(a * 0.22f), r.getRight(), r.getBottom(), true);
        g.setGradientFill(gr); g.fillRoundedRectangle(r, radius);
    }
}
inline void softWellLite(juce::Graphics& g, juce::Rectangle<float> r, float radius) {
    g.setColour(Theme::card2()); g.fillRoundedRectangle(r, radius); softInset(g, r, radius, juce::jlimit(1.5f, 3.0f, juce::jmin(r.getWidth(), r.getHeight()) * 0.08f));
}
} // namespace arco
