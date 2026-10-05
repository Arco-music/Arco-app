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
};

struct Theme {
    static juce::Colour rgb(float r, float g, float b) { return juce::Colour::fromFloatRGBA(r, g, b, 1.0f); }
    static juce::Colour hex(juce::uint32 v) { return juce::Colour(0xff000000u | v); }

    // One family of five colours: FIG 5A4155 · CHAMBRAY 8FA4B0 · STORM 445662 · MANUSCRIPT EFEAD9 · TIMBER 9D836C.
    // Two dark palettes (Fig, Storm) and two light ones (Manuscript, Chambray); the accents are tints of the same five.
    // Text and marks are kept readable automatically where they are drawn (ink(), arc()).
    static const std::vector<Palette>& palettes() {
        // accent slots: 0 rec/red  1 on/green  2 amber  3 blue  4 mauve  5 teal  6 warm  7 olive
        auto onDark = [] { return std::array<juce::Colour, 8> { hex(0xc58fae), hex(0x9fb6c2), hex(0xc6a689), hex(0x8fa4b0), hex(0xa77d9c), hex(0x86a0ad), hex(0xb9a58f), hex(0xd9cfb2) }; };
        auto onLight = [] { return std::array<juce::Colour, 8> { hex(0x7b4d6f), hex(0x4f6c7c), hex(0x8a6c51), hex(0x445662), hex(0x5a4155), hex(0x5d7887), hex(0x9d836c), hex(0x7a7055) }; };
        static const std::vector<Palette> P = {
            { "Fig", false, hex(0x241a22), hex(0x30232e), hex(0x3e2d3b), hex(0x1c1419), hex(0x5a4155), hex(0xefead9), hex(0xbdb4a5), hex(0x4a3846), hex(0x4a3846), hex(0x8fa4b0), onDark() },
            { "Storm", false, hex(0x1d282f), hex(0x26333c), hex(0x324250), hex(0x172026), hex(0x445662), hex(0xefead9), hex(0xabb9c1), hex(0x3a4a56), hex(0x3a4b58), hex(0x9d836c), onDark() },
            { "Manuscript", true, hex(0xe0d9c6), hex(0xefead9), hex(0xe5dfcb), hex(0xe9e3d1), hex(0xc8bea5), hex(0x2e2330), hex(0x4a5761), hex(0xcdc4ad), hex(0x8fa4b0), hex(0x5a4155), onLight() },
            { "Chambray", true, hex(0xc8d2d7), hex(0xdbe2e5), hex(0xcbd5da), hex(0xd2dbdf), hex(0xa6b5bd), hex(0x22303a), hex(0x445662), hex(0xb2bfc6), hex(0x9d836c), hex(0x5a4155), onLight() },
        };
        return P;
    }
    static int& paletteIndex() { static int i = 0; return i; }
    static int& lastDark() { static int i = 0; return i; }
    static int& lastLight() { static int i = 2; return i; }   // Manuscript
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
    static juce::Colour c(juce::Colour design) {
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
    static juce::Colour arc(juce::Colour c) { return ensure(ensure(c, pal().card, 3.0f), pal().card2, 3.0f); }
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
            { "flg", 3 }, { "png", 3 }, { "wah", 2 }, { "oct", 4 }, { "tap", 0 }, { "flt", 1 }, { "rng", 0 }, { "stu", 2 }, { "vib", 1 }, { "crs", 0 } };
        auto it = F.find(k); return it == F.end() ? 6 : it->second;
    }
    static juce::Colour fxRaw(const std::string& k) { return pal().acc[(size_t) family(k)]; }
    static juce::Colour fx(const std::string& k) { return fxRaw(k); }
    static juce::Colour strip(int s) {
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
