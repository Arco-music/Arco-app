#pragma once
// SCORE - the settings (sound card, pedals, look) and the export window.
#include "Icons.h"

namespace score {

class Overlay : public juce::Component {          // a card in the middle of a dimmed window
public:
    Overlay(juce::String t, int w, int h) : title(std::move(t)), cw(w), ch(h) { addAndMakeVisible(close); close.onClick = [] { if (Ui::get()->closeOverlay) Ui::get()->closeOverlay(); }; close.fsize = 9.0f; }
    void paint(juce::Graphics& g) override;
    void resized() override;
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre(juce::jmin(cw, getWidth() - 20), juce::jmin(ch, getHeight() - 20)); }
    juce::Rectangle<int> body() const { return card().reduced(20, 0).withTrimmedTop(50).withTrimmedBottom(16); }
    virtual void layout() {}
    juce::String title; int cw, ch;
    Pill close { "CLOSE" };
    void mouseDown(const juce::MouseEvent& e) override { if (!card().contains(e.getPosition()) && Ui::get()->closeOverlay) Ui::get()->closeOverlay(); }
};

class SettingsPanel : public Overlay, private juce::Timer {
public:
    SettingsPanel();
    void paint(juce::Graphics& g) override;
    void layout() override;
private:
    void timerCallback() override;
    void refreshDevices();
    Pill outBtn { "" }, inBtn { "" }, rateBtn { "" }, bufBtn { "" }, outChBtn { "" }, clickBtn { "" }, toneBtn { "TEST TONE" }, themeBtn { "" }, scaleBtn { "" };
    juce::OwnedArray<Pill> learn;
    std::vector<std::unique_ptr<Meter>> inMeters;
    juce::Rectangle<int> audioBox, pedalBox, lookBox, keysBox;
};

class ExportPanel : public Overlay, private juce::Timer {
public:
    ExportPanel();
    void paint(juce::Graphics& g) override;
    void layout() override;
private:
    void timerCallback() override;
    void go();
    Host::ExportOptions o;
    Pill what { "" }, range { "" }, bits { "" }, norm { "NORMALIZE" }, tail { "" }, start { "EXPORT" }, bundle { "SEND THE WHOLE PIECE (.zip)" };
    juce::String result; juce::File last;
    Pill reveal { "SHOW THE FILE" }, share { U("SAVE / SHARE …") };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace score
