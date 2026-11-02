#pragma once
#include <JuceHeader.h>
#include <map>
#include <string>
#include <vector>
#include <array>

namespace arco {
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }

// The palettes. Each one is designed as a whole (surfaces, text, lines), and every colour drawn on it is
// checked for contrast (WCAG): text >= 7:1, labels and coloured text >= 4.5:1, knob arcs and marks >= 3:1.
struct Palette {
    const char* name;
    bool light;
    juce::Colour bg, card, card2, scr, line, txt, sub, off, btn, sel;
    // eight muted accents, used for the effects, the tracks and the signals:
    // 0 red  1 moss/green  2 ochre/amber  3 blue  4 mauve  5 teal  6 warm grey  7 olive
    std::array<juce::Colour, 8> acc;
    int style = 0;       // 0 accents of the family; 1 classic: every effect and track its own clear colour (1.1 design)
    // 2.0: "liquid glass" - surfaces are frosted glass (translucent white) over a deep black with soft glows;
    // solidCard / solidCard2 stand for them where a contrast has to be measured
    bool glass = false;
    juce::Colour solidCard {}, solidCard2 {};
    int glow = 0;        // the backdrop's glows: 0 cool (blue / violet), 1 warm (champagne)
    // 2.1: SOFT - surfaces raised out of one material (neumorphic): a light from the top left (hi) and its shade (lo)
    bool soft = false;
    juce::Colour hi {}, lo {};
};

struct Theme {
    static juce::Colour rgb(float r, float g, float b) { return juce::Colour::fromFloatRGBA(r, g, b, 1.0f); }
    static juce::Colour hex(juce::uint32 v) { return juce::Colour(0xff000000u | v); }
    static juce::Colour wa(float a) { return juce::Colour::fromFloatRGBA(1, 1, 1, a); }     // white glass

    // Four palettes: Black (the 1.1 design) and Storm (dark), Light and Chambray (light). Every effect keeps its own colour.
    // Text and marks are kept readable automatically where they are drawn (ink(), arc()).
    static const std::vector<Palette>& palettes() {
        // accent slots: 0 rec/red  1 on/green  2 amber  3 blue  4 mauve  5 teal  6 warm  7 olive
        auto cDark = [] { return std::array<juce::Colour, 8> { rgb(0.910f, 0.330f, 0.350f), rgb(0.300f, 0.760f, 0.480f), rgb(0.950f, 0.680f, 0.250f), rgb(0.300f, 0.520f, 0.900f), rgb(0.62f, 0.48f, 1.00f), rgb(0.40f, 0.88f, 0.78f), rgb(0.90f, 0.70f, 0.50f), rgb(0.70f, 0.80f, 0.30f) }; };
        auto cLight = [] { return std::array<juce::Colour, 8> { hex(0xd9606a), hex(0x5fae7f), hex(0xe2a84a), hex(0x5f8fcf), hex(0x9077c4), hex(0x4fae9e), hex(0xc99a6e), hex(0x9fb04a) }; };
        static const std::vector<Palette> P = {
            // the 1.1 black, exactly - every effect and track its own clear colour
            { "Black", false, hex(0x0e0f12), hex(0x16171b), hex(0x1f2024), hex(0x0a0b0d), hex(0x2a2c31), hex(0xeef0f4), hex(0x9a9da5), hex(0x2c2e34), hex(0x1f2024), hex(0x2b3956), cDark(), 1 },
            { "Storm", false, hex(0x182128), hex(0x202b33), hex(0x2a3741), hex(0x131a20), hex(0x354550), hex(0xeef1f3), hex(0x93a3ad), hex(0x2f3d48), hex(0x2c3a45), hex(0x3d5263), cDark(), 1 },
            // light: the same colours, softer, on warm mid-light grey (1.6: nothing near white, easy on the eyes)
            { "Light", true, hex(0xcdcbc5), hex(0xd9d7d1), hex(0xc9c7c0), hex(0xc8c6bf), hex(0xbfbdb6), hex(0x34353a), hex(0x5d5e64), hex(0xc4c2bb), hex(0xcac8c1), hex(0x9aa3b8), cLight(), 1 },
            { "Chambray", true, hex(0xd9e0e4), hex(0xedf1f3), hex(0xe2e8eb), hex(0xe6ecef), hex(0xc1ccd2), hex(0x1f2a32), hex(0x55656f), hex(0xd2dbe0), hex(0xd8e0e4), hex(0x7f93a3), cLight(), 1 },
            // 2.0: black and glass. NOIR: cool, quiet colours; NOIR GOLD: the same with champagne
            { "Noir", false, hex(0x222329), wa(0.08f), wa(0.12f), juce::Colour::fromFloatRGBA(0, 0, 0, 0.26f), wa(0.12f), hex(0xffffff), hex(0xb6b7c0), wa(0.07f), wa(0.09f), wa(0.22f),
              std::array<juce::Colour, 8> { hex(0xff6b72), hex(0x4fd6a0), hex(0xf2c46d), hex(0x7ea8ff), hex(0xb79cff), hex(0x62e3d1), hex(0xe7b98d), hex(0xc6d36c) }, 0, true, hex(0x343539), hex(0x3c3d42), 0 },
            { "Noir Gold", false, hex(0x25221e), wa(0.08f), wa(0.12f), juce::Colour::fromFloatRGBA(0, 0, 0, 0.26f), wa(0.12f), hex(0xfffcf7), hex(0xbdb5a9), wa(0.07f), wa(0.09f), wa(0.22f),
              std::array<juce::Colour, 8> { hex(0xf07a6a), hex(0x8fd1a2), hex(0xe2bf7e), hex(0xb9c7e6), hex(0xcbb5e8), hex(0x9cd8cc), hex(0xd9b48a), hex(0xd2cd8a) }, 0, true, hex(0x37342f), hex(0x3f3c37), 1 },
            // 2.1: SOFT DARK / SOFT LIGHT - one soft material, dusty colours, light only where something is on
            { "Soft Dark", false, hex(0x2a2e37), hex(0x2a2e37), hex(0x262931), hex(0x23262d), hex(0x3a3f4a), hex(0xeceef3), hex(0x8f96a3), hex(0x5c6270), hex(0x2e323c), hex(0x3d4559),
              std::array<juce::Colour, 8> { hex(0xe3837a), hex(0x94c7b0), hex(0xdcbd8f), hex(0x9db0dc), hex(0xb3a7da), hex(0x8fc4c7), hex(0xd9a6bc), hex(0xc2c48f) }, 0, false, hex(0x2a2e37), hex(0x262931), 0,
              true, hex(0x353a45), hex(0x1e2128) },
            { "Soft Light", true, hex(0xe8e9ec), hex(0xe8e9ec), hex(0xe1e3e7), hex(0xdcdee3), hex(0xd3d6dc), hex(0x2a2d33), hex(0x767c88), hex(0xb3b7bf), hex(0xebecef), hex(0xd3daea),
              std::array<juce::Colour, 8> { hex(0xd9665d), hex(0x76ad95), hex(0xc9a36e), hex(0x7d93c4), hex(0x9a8cc4), hex(0x6aa5a8), hex(0xc48aa3), hex(0x9a9c5e) }, 0, false, hex(0xe8e9ec), hex(0xe1e3e7), 0,
              true, hex(0xffffff), hex(0xcdd0d6) },
        };
        return P;
    }
    static int& paletteIndex() { static int i = 6; return i; }      // 2.1: SOFT DARK first
    static int& lastDark() { static int i = 6; return i; }
    static int& lastLight() { static int i = 7; return i; }   // Soft Light
    static const Palette& pal() { auto& P = palettes(); return P[(size_t) juce::jlimit(0, (int) P.size() - 1, paletteIndex())]; }
    static bool lightMode() { return pal().light; }
    static void setPalette(int i) {
        paletteIndex() = juce::jlimit(0, (int) palettes().size() - 1, i);
        (pal().light ? lastLight() : lastDark()) = paletteIndex();
    }
    static void toggleLight() { setPalette(lightMode() ? lastDark() : lastLight()); }
    static int paletteByName(const juce::String& n) {
        auto& P = palettes(); for (int i = 0; i < (int) P.size(); ++i) if (n == P[(size_t) i].name) return i; return 0; }

    // ---- contrast (WCAG relative luminance)
    static float lum(juce::Colour c) {
        auto f = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * f(c.getFloatRed()) + 0.7152f * f(c.getFloatGreen()) + 0.0722f * f(c.getFloatBlue()); }
    static float contrast(juce::Colour a, juce::Colour b) {
        float la = lum(a), lb = lum(b); return (juce::jmax(la, lb) + 0.05f) / (juce::jmin(la, lb) + 0.05f); }
    // move a colour's brightness (keeping its hue) until it reaches the contrast against a background
    static juce::Colour ensure(juce::Colour c, juce::Colour on, float ratio) {
        if (contrast(c, on) >= ratio) return c;
        bool darker = lum(on) > 0.35f;
        float h, s, v; c.getHSB(h, s, v);
        if (darker) s = juce::jmin(1.0f, s * 1.15f + 0.05f);
        for (int i = 0; i < 60 && contrast(c, on) < ratio; ++i) {
            if (darker) v *= 0.94f; else { v = juce::jmin(1.0f, v + 0.03f); if (v >= 1.0f) s *= 0.92f; }
            c = juce::Colour::fromHSV(h, s, v, 1.0f);
        }
        return c;
    }
    // black or white text, whichever reads better on this background
    static bool glass() { return pal().glass; }
    static bool softUI() { return pal().soft; }
    static juce::Colour hiC() { return pal().hi; }
    static juce::Colour loC() { return pal().lo; }
    // a translucent colour as it looks on the glass (for measuring contrast)
    static juce::Colour flat(juce::Colour c) { return c.isOpaque() ? c : (glass() ? pal().solidCard : pal().bg).overlaidWith(c); }
    static juce::Colour cardS() { return glass() ? pal().solidCard : pal().card; }
    static juce::Colour card2S() { return glass() ? pal().solidCard2 : pal().card2; }
    // 2.0: the backdrop of every screen: black, and (glass) two very soft glows the glass catches
    static void backdrop(juce::Graphics& g, juce::Rectangle<int> r) {
        g.setColour(pal().bg); g.fillRect(r);
        if (!glass()) return;
        // 2.1: clean - no haze, just a light from above fading into the graphite
        auto rf = r.toFloat();
        g.setGradientFill(juce::ColourGradient(pal().bg.brighter(0.18f), 0, rf.getY(), pal().bg.darker(0.12f), 0, rf.getBottom(), false));
        g.fillRect(rf);
    }
    static juce::Colour textOn(juce::Colour bgc) {
        bgc = flat(bgc);
        return contrast(juce::Colours::white, bgc) >= contrast(juce::Colour(0xff111216), bgc) ? juce::Colours::white : juce::Colour(0xff111216); }

    // a colour from the (dark) design: kept on dark palettes, made deep enough on light ones
    // classic on a light palette: the same hue, softer and a little deeper (pastel, like printed colour)
    static juce::Colour soft(juce::Colour d) {
        if (! pal().light) return d;
        float h, s, v; d.getHSB(h, s, v);
        if (s < 0.25f) return juce::Colour::fromHSV(h, juce::jmin(0.35f, s * 1.6f), 0.62f, 1.0f);
        return juce::Colour::fromHSV(h, juce::jlimit(0.38f, 0.62f, s * 0.78f), juce::jmin(0.86f, v * 0.9f), 1.0f);
    }
    static juce::Colour c(juce::Colour design) {
        if (pal().style == 1) { float h, s, v; design.getHSB(h, s, v); if (s < 0.12f && lum(design) < 0.12f) return pal().btn; return s < 0.12f ? design : soft(design); }
        float h, s, v; design.getHSB(h, s, v);
        if (s < 0.12f) return lum(design) < 0.12f ? pal().btn : design;      // greys: the palette's button tone
        // a coloured accent from the design: the palette accent with the closest hue (keeps everything in one family)
        int best = 0; float bd = 9;
        for (int i = 0; i < 8; ++i) {
            float hh, ss, vv; pal().acc[(size_t) i].getHSB(hh, ss, vv);
            if (ss < 0.08f) continue;
            float d = std::abs(hh - h); d = juce::jmin(d, 1.0f - d);
            if (d < bd) { bd = d; best = i; }
        }
        return pal().acc[(size_t) best];
    }
    static juce::Colour bg()    { return pal().bg; }
    static juce::Colour card()  { return pal().card; }
    static juce::Colour card2() { return pal().card2; }
    static juce::Colour scr()   { return pal().scr; }
    static juce::Colour line()  { return pal().line; }
    static juce::Colour txt()   { return pal().txt; }
    static juce::Colour sub()   { return pal().sub; }
    static juce::Colour off()   { return pal().off; }
    static juce::Colour btn()   { return pal().btn; }
    static juce::Colour sel()   { return pal().sel; }
    // 1.6: the knob face of the new design
    static juce::Colour knobHi()   { return glass() ? hex(0x232327) : pal().light ? hex(0xe3e1db) : hex(0x2b2d33); }
    static juce::Colour knobLo()   { return glass() ? hex(0x08080a) : pal().light ? hex(0xc9c7c0) : hex(0x18191d); }
    static juce::Colour knobEdge() { return glass() ? wa(0.16f) : pal().light ? hex(0xbab8b1) : hex(0x33363d); }
    static juce::Colour knobDot()  { return pal().light ? hex(0x3a3b40) : hex(0xeef0f4); }
    // readable versions, applied where things are drawn: text (4.5:1 on cards and background), marks (3:1)
    static juce::Colour ink(juce::Colour c) { return ensure(ensure(flat(c), cardS(), 4.5f), pal().bg, 4.5f); }
    static juce::Colour arc(juce::Colour c) { float r = (pal().style == 1 && pal().light) ? 2.3f : 3.0f; return ensure(ensure(flat(c), cardS(), r), card2S(), r); }
    static juce::Colour mark(juce::Colour d) { return ink(d); }
    // signal colours (soft, for buttons and fills; the button text picks black or white)
    static juce::Colour red()   { return pal().acc[0]; }
    static juce::Colour green() { return pal().acc[1]; }
    static juce::Colour amber() { return pal().acc[2]; }
    static juce::Colour blue()  { return pal().acc[3]; }
    static juce::Colour violet(){ return pal().acc[4]; }
    // each effect has a family colour from the palette's accents (drive = red, modulation = blue, space = teal ...)
    static int family(const std::string& k) {
        static const std::map<std::string, int> F = {
            { "eq", 2 }, { "drv", 0 }, { "trm", 7 }, { "phs", 4 }, { "swl", 7 }, { "tun", 4 }, { "cho", 5 }, { "dly", 3 }, { "rev", 3 },
            { "fxb", 0 }, { "shd", 4 }, { "halo", 4 }, { "res", 2 }, { "grn", 7 }, { "frz", 5 }, { "spc", 5 }, { "orb", 1 }, { "drm", 6 },
            { "flg", 3 }, { "png", 3 }, { "wah", 2 }, { "oct", 4 }, { "tap", 0 }, { "flt", 1 }, { "rng", 0 }, { "stu", 2 }, { "vib", 1 }, { "crs", 0 },
            { "pdl", 2 }, { "fbk", 0 }, { "voc", 4 }, { "pit", 4 }, { "spr", 5 }, { "tlk", 2 } };
        auto it = F.find(k); return it == F.end() ? 6 : it->second;
    }
    static juce::Colour fxRaw(const std::string& k) { return pal().acc[(size_t) family(k)]; }
    static juce::Colour fxClassic(const std::string& k) {
        static const std::map<std::string, juce::Colour> F = {
            { "eq", rgb(0.35f, 0.80f, 0.86f) }, { "drv", rgb(1.00f, 0.50f, 0.28f) }, { "trm", rgb(0.96f, 0.78f, 0.32f) },
            { "phs", rgb(0.93f, 0.42f, 0.78f) }, { "swl", rgb(0.70f, 0.80f, 0.30f) }, { "tun", rgb(0.86f, 0.48f, 0.95f) },
            { "cho", rgb(0.48f, 0.86f, 0.56f) }, { "dly", rgb(0.32f, 0.62f, 1.00f) }, { "rev", rgb(0.45f, 0.48f, 1.00f) },
            { "fxb", rgb(1.00f, 0.36f, 0.45f) }, { "shd", rgb(0.55f, 0.64f, 0.95f) }, { "halo", rgb(0.72f, 0.57f, 1.00f) },
            { "res", rgb(0.90f, 0.70f, 0.50f) }, { "grn", rgb(0.85f, 0.90f, 0.45f) }, { "frz", rgb(0.62f, 0.90f, 1.00f) },
            { "spc", rgb(1.00f, 0.52f, 0.72f) }, { "orb", rgb(0.40f, 0.88f, 0.78f) }, { "drm", rgb(0.88f, 0.89f, 0.92f) },
            { "flg", rgb(0.40f, 0.75f, 0.95f) }, { "png", rgb(0.38f, 0.56f, 0.98f) }, { "wah", rgb(1.00f, 0.62f, 0.30f) },
            { "oct", rgb(0.66f, 0.52f, 0.98f) }, { "tap", rgb(0.92f, 0.62f, 0.45f) }, { "flt", rgb(0.36f, 0.82f, 0.62f) },
            { "rng", rgb(0.98f, 0.45f, 0.60f) }, { "stu", rgb(0.98f, 0.82f, 0.40f) }, { "vib", rgb(0.55f, 0.85f, 0.45f) },
            { "crs", rgb(1.00f, 0.42f, 0.38f) }, { "pdl", rgb(0.95f, 0.84f, 0.52f) }, { "fbk", rgb(1.00f, 0.46f, 0.30f) },
            { "voc", rgb(0.62f, 0.66f, 1.00f) }, { "pit", rgb(0.80f, 0.55f, 1.00f) }, { "spr", rgb(0.50f, 0.85f, 0.85f) }, { "tlk", rgb(1.00f, 0.66f, 0.52f) } };
        auto it = F.find(k); return soft(it == F.end() ? rgb(0.88f, 0.89f, 0.92f) : it->second);
    }
    static juce::Colour fx(const std::string& k) { return pal().style == 1 ? fxClassic(k) : fxRaw(k); }
    static juce::Colour strip(int s) {
        if (pal().style == 1) {
            static const float S[12][3] = { { 0.78f, 0.84f, 0.95f }, { 0.70f, 0.78f, 0.92f }, { 0.62f, 0.72f, 0.88f }, { 0.55f, 0.66f, 0.84f },
                { 0.98f, 0.45f, 0.45f }, { 1.00f, 0.63f, 0.32f }, { 0.97f, 0.82f, 0.32f }, { 0.56f, 0.85f, 0.42f },
                { 0.30f, 0.82f, 0.76f }, { 0.38f, 0.64f, 1.00f }, { 0.64f, 0.50f, 1.00f }, { 0.93f, 0.47f, 0.86f } };
            int i = s < 4 ? s : 4 + (s - 4) % 8; return soft(rgb(S[i][0], S[i][1], S[i][2]));
        }
        if (pal().soft) {      // 2.1: blue, lilac, sand, sage, rose ... (never the record red)
            if (s < 4) { static const int LV[4] = { 3, 5, 4, 7 }; return pal().acc[(size_t) LV[s]]; }
            static const int O[8] = { 4, 2, 1, 6, 3, 5, 7, 2 }; return pal().acc[(size_t) O[(s - 4) % 8]];
        }
        // LIVE 1-4: soft blue-greys; TRACK 1-8: the accents in a pleasant order
        if (s < 4) { static const int LV[4] = { 3, 4, 5, 7 }; return pal().acc[(size_t) LV[s]]; }
        static const int O[8] = { 0, 6, 2, 7, 1, 5, 3, 4 };
        return pal().acc[(size_t) O[(s - 4) % 8]];
    }
    // fonts (Inter, bundled)
    static juce::Typeface::Ptr face(int w) {
        static juce::Typeface::Ptr r = juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
        static juce::Typeface::Ptr s = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        static juce::Typeface::Ptr b = juce::Typeface::createSystemTypefaceFor(BinaryData::InterBold_otf, BinaryData::InterBold_otfSize);
        return w == 0 ? r : (w == 1 ? s : b);
    }
    // 2.1: the small labels of the soft design (PAN, VOL, BAR, BPM, IN 1 ...): a monospace with a little air between the letters
    static juce::Font mono(float size) {
        static juce::String name = [] { juce::StringArray want { "SF Mono", "Menlo", "DejaVu Sans Mono", "Liberation Mono", "Courier New" };
            auto have = juce::Font::findAllTypefaceNames(); for (auto& n : want) if (have.contains(n)) return n; return juce::Font::getDefaultMonospacedFontName(); }();
        return juce::Font(juce::FontOptions(name, size * 1.18f, juce::Font::plain)).withExtraKerningFactor(0.14f); }
    static juce::Font font(float size, int weight = 0) { return juce::Font(juce::FontOptions(face(weight)).withHeight(size * 1.25f)); }
};

} // namespace arco
