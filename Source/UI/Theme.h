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
};

struct Theme {
    static juce::Colour rgb(float r, float g, float b) { return juce::Colour::fromFloatRGBA(r, g, b, 1.0f); }
    static juce::Colour hex(juce::uint32 v) { return juce::Colour(0xff000000u | v); }

    // Four palettes: Black (the 1.1 design) and Storm (dark), Light and Chambray (light). Every effect keeps its own colour.
    // Text and marks are kept readable automatically where they are drawn (ink(), arc()).
    static const std::vector<Palette>& palettes() {
        // accent slots: 0 rec/red  1 on/green  2 amber  3 blue  4 mauve  5 teal  6 warm  7 olive
        auto cDark = [] { return std::array<juce::Colour, 8> { rgb(0.910f, 0.330f, 0.350f), rgb(0.300f, 0.760f, 0.480f), rgb(0.950f, 0.680f, 0.250f), rgb(0.300f, 0.520f, 0.900f), rgb(0.62f, 0.48f, 1.00f), rgb(0.40f, 0.88f, 0.78f), rgb(0.90f, 0.70f, 0.50f), rgb(0.70f, 0.80f, 0.30f) }; };
        auto cLight = [] { return std::array<juce::Colour, 8> { hex(0xd9606a), hex(0x5fae7f), hex(0xe2a84a), hex(0x5f8fcf), hex(0x9077c4), hex(0x4fae9e), hex(0xc99a6e), hex(0x9fb04a) }; };
        static const std::vector<Palette> P = {
            // the 1.1 black, exactly - every effect and track its own clear colour
            { "Black", false, rgb(0.067f, 0.071f, 0.086f), rgb(0.112f, 0.118f, 0.143f), rgb(0.158f, 0.165f, 0.196f), rgb(0.050f, 0.053f, 0.066f), rgb(0.235f, 0.245f, 0.290f), rgb(0.925f, 0.932f, 0.950f), rgb(0.560f, 0.580f, 0.640f), rgb(0.240f, 0.252f, 0.300f), rgb(0.205f, 0.214f, 0.255f), rgb(0.27f, 0.30f, 0.40f), cDark(), 1 },
            { "Storm", false, hex(0x182128), hex(0x202b33), hex(0x2a3741), hex(0x131a20), hex(0x354550), hex(0xeef1f3), hex(0x93a3ad), hex(0x2f3d48), hex(0x2c3a45), hex(0x3d5263), cDark(), 1 },
            // light: the same colours, softer, on soft grey (no bright white)
            { "Light", true, hex(0xe6e5e1), hex(0xf3f2ef), hex(0xe9e8e4), hex(0xecebe7), hex(0xd2d0cb), hex(0x26272b), hex(0x6b6c72), hex(0xdcdad5), hex(0xe2e1dd), hex(0x8b93a8), cLight(), 1 },
            { "Chambray", true, hex(0xd9e0e4), hex(0xedf1f3), hex(0xe2e8eb), hex(0xe6ecef), hex(0xc1ccd2), hex(0x1f2a32), hex(0x55656f), hex(0xd2dbe0), hex(0xd8e0e4), hex(0x7f93a3), cLight(), 1 },
        };
        return P;
    }
    static int& paletteIndex() { static int i = 0; return i; }
    static int& lastDark() { static int i = 0; return i; }
    static int& lastLight() { static int i = 2; return i; }   // Light
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
    static juce::Colour textOn(juce::Colour bgc) {
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
    // readable versions, applied where things are drawn: text (4.5:1 on cards and background), marks (3:1)
    static juce::Colour ink(juce::Colour c) { return ensure(ensure(c, pal().card, 4.5f), pal().bg, 4.5f); }
    static juce::Colour arc(juce::Colour c) { float r = (pal().style == 1 && pal().light) ? 2.3f : 3.0f; return ensure(ensure(c, pal().card, r), pal().card2, r); }
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
    static juce::Font font(float size, int weight = 0) { return juce::Font(juce::FontOptions(face(weight)).withHeight(size * 1.25f)); }
};

} // namespace arco
