#pragma once
// SCORE - colours and fonts (the ARCO family look: dark is the design source, light is a soft grey).
#include <JuceHeader.h>
#include <map>
#include <string>

namespace score {
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }

struct Theme {
    static bool& lightMode() { static bool l = false; return l; }
    static juce::Colour rgb(float r, float g, float b) { return juce::Colour::fromFloatRGBA(r, g, b, 1.0f); }
    static juce::Colour toLight(juce::Colour c) {
        static const std::map<juce::uint32, juce::Colour> L = {
            { rgb(0.067f, 0.071f, 0.086f).getARGB(), rgb(0.835f, 0.842f, 0.858f) }, { rgb(0.112f, 0.118f, 0.143f).getARGB(), rgb(0.905f, 0.910f, 0.922f) },
            { rgb(0.158f, 0.165f, 0.196f).getARGB(), rgb(0.858f, 0.865f, 0.880f) }, { rgb(0.050f, 0.053f, 0.066f).getARGB(), rgb(0.952f, 0.955f, 0.962f) },
            { rgb(0.235f, 0.245f, 0.290f).getARGB(), rgb(0.735f, 0.745f, 0.775f) }, { rgb(0.925f, 0.932f, 0.950f).getARGB(), rgb(0.07f, 0.08f, 0.10f) },
            { rgb(0.560f, 0.580f, 0.640f).getARGB(), rgb(0.36f, 0.38f, 0.43f) }, { rgb(0.240f, 0.252f, 0.300f).getARGB(), rgb(0.62f, 0.64f, 0.68f) },
            { rgb(0.205f, 0.214f, 0.255f).getARGB(), rgb(0.78f, 0.79f, 0.82f) }, { rgb(0.27f, 0.30f, 0.40f).getARGB(), rgb(0.70f, 0.76f, 0.90f) },
            { rgb(0.085f, 0.090f, 0.110f).getARGB(), rgb(0.875f, 0.880f, 0.895f) } };
        auto it = L.find(c.getARGB());
        if (it != L.end()) return it->second;
        float h, s, v; c.getHSB(h, s, v);
        if (s < 0.12f) return c;
        s = juce::jmin(1.0f, s * 1.3f + 0.1f);
        auto out = juce::Colour::fromHSV(h, s, v, c.getFloatAlpha());
        for (int i = 0; i < 40; ++i) {
            float lum = 0.2126f * out.getFloatRed() + 0.7152f * out.getFloatGreen() + 0.0722f * out.getFloatBlue();
            if (lum <= 0.45f) break;
            v *= 0.95f; out = juce::Colour::fromHSV(h, s, v, c.getFloatAlpha());
        }
        return out;
    }
    static juce::Colour c(juce::Colour dark) { return lightMode() ? toLight(dark) : dark; }
    static juce::Colour bg()    { return c(rgb(0.067f, 0.071f, 0.086f)); }
    static juce::Colour panel() { return c(rgb(0.085f, 0.090f, 0.110f)); }
    static juce::Colour card()  { return c(rgb(0.112f, 0.118f, 0.143f)); }
    static juce::Colour card2() { return c(rgb(0.158f, 0.165f, 0.196f)); }
    static juce::Colour scr()   { return c(rgb(0.050f, 0.053f, 0.066f)); }
    static juce::Colour line()  { return c(rgb(0.235f, 0.245f, 0.290f)); }
    static juce::Colour txt()   { return c(rgb(0.925f, 0.932f, 0.950f)); }
    static juce::Colour sub()   { return c(rgb(0.560f, 0.580f, 0.640f)); }
    static juce::Colour off()   { return c(rgb(0.240f, 0.252f, 0.300f)); }
    static juce::Colour btn()   { return c(rgb(0.205f, 0.214f, 0.255f)); }
    static juce::Colour sel()   { return c(rgb(0.27f, 0.30f, 0.40f)); }
    static juce::Colour red()   { return c(rgb(0.910f, 0.330f, 0.350f)); }
    static juce::Colour green() { return c(rgb(0.300f, 0.760f, 0.480f)); }
    static juce::Colour amber() { return c(rgb(0.950f, 0.680f, 0.250f)); }
    static juce::Colour gold()  { return c(rgb(1.000f, 0.784f, 0.341f)); }
    static juce::Colour blue()  { return c(rgb(0.300f, 0.520f, 0.900f)); }
    static juce::Colour violet(){ return c(rgb(0.62f, 0.48f, 1.00f)); }
    static juce::Colour cyan()  { return c(rgb(0.35f, 0.80f, 0.86f)); }
    static juce::Colour onBtn() { return lightMode() ? juce::Colours::white : juce::Colours::white; }
    static juce::Colour argb(juce::uint32 v) { return c(juce::Colour(v)); }
    // fonts (Inter, bundled)
    static juce::Typeface::Ptr face(int w) {
        static juce::Typeface::Ptr r = juce::Typeface::createSystemTypefaceFor(ScoreBinary::InterRegular_otf, ScoreBinary::InterRegular_otfSize);
        static juce::Typeface::Ptr s = juce::Typeface::createSystemTypefaceFor(ScoreBinary::InterSemiBold_otf, ScoreBinary::InterSemiBold_otfSize);
        static juce::Typeface::Ptr b = juce::Typeface::createSystemTypefaceFor(ScoreBinary::InterBold_otf, ScoreBinary::InterBold_otfSize);
        return w == 0 ? r : (w == 1 ? s : b);
    }
    static juce::Font font(float size, int weight = 0) { return juce::Font(juce::FontOptions(face(weight)).withHeight(size * 1.25f)); }
};

} // namespace score
