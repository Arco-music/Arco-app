#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/Updater.h"

namespace arco {
class Canvas;

// menus and lists in the ARCO font and size
struct ArcoLaf : public juce::LookAndFeel_V4 {
    using juce::LookAndFeel_V4::LookAndFeel_V4;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return Theme::font(9.5f, 1); }
    juce::Font getPopupMenuFont() override { return Theme::font(10.0f, 0); }
    void positionComboBoxText(juce::ComboBox& b, juce::Label& l) override {
        l.setBounds(8, 1, b.getWidth() - 28, b.getHeight() - 2); l.setFont(getComboBoxFont(b)); }
};

// AUDIO: the sound card - output, input, sample rate, buffer, a test tone, and what happened in words
class AudioBox : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit AudioBox(Canvas& c, juce::LookAndFeel* laf);
    ~AudioBox() override;
    void paint(juce::Graphics& g) override;
    void refresh();
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { repaint(); }
    void apply();
    Canvas& cv;
    juce::ComboBox out, in, rate, buf;
    juce::OwnedArray<juce::Component> kids;
    juce::StringArray outNames, inNames;
    juce::Array<double> rates; juce::Array<int> bufs;
    bool filling = false;
   #if JUCE_IOS
    juce::StringArray iosIds;
    juce::String lastRoute;
   #endif
};

// LOOK & EFFECTS: the colour palette, and which effect cards are on the main window
class LookPanel : public juce::Component {
public:
    explicit LookPanel(Canvas& c);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
private:
    Canvas& cv;
    juce::Rectangle<int> box { 150, 34, 1140, 812 };
    juce::OwnedArray<juce::Component> kids;
    juce::Rectangle<int> paletteTile(int i) const;
    juce::Rectangle<int> effectTile(int i) const;
};

// the phones the station found: each one a microphone on a LIVE input (or just a remote)
class PhoneList : public juce::Component, private juce::Timer {
public:
    explicit PhoneList(Canvas& c) : cv(c) { startTimerHz(8); }
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    Canvas& cv;
    void timerCallback() override { if (isShowing()) repaint(); }
    struct Hit { juce::Rectangle<int> r; int conn; int live; };
    std::vector<Hit> hits;
};

// SETUP: the sound card, live inputs, headphones, speakers, recording timing, practice, tuner.
class SetupPanel : public juce::Component, private juce::Timer {
public:
    explicit SetupPanel(Canvas& c);
    ~SetupPanel() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
private:
    Canvas& cv;
    juce::Rectangle<int> box { 150, 34, 1140, 812 };
    juce::OwnedArray<juce::Component> kids;
    std::unique_ptr<AudioBox> audio;
    std::unique_ptr<ArcoLaf> laf;
    juce::ComboBox phones;
    juce::TextEditor wifi;
    std::unique_ptr<PhoneList> phoneList;
    Updater updater;
    juce::String updateText;
    void askKey(const juce::String& why);
    void startUpdateCheck();
public:
    void checkForUpdates() { if (updater.key().isEmpty()) askKey({}); else startUpdateCheck(); }
private:
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    void styleLaf();
    void timerCallback() override { if (isVisible()) repaint(box); }
    // 2.2: one section at a time (a list on the left), bigger - it was everything on one crowded page
    static constexpr int NSEC = 7;
    int section = 0;
    juce::Rectangle<int> secR(int s) const;              // where a section's controls are drawn in the old page (box coordinates)
    juce::Rectangle<int> contentR() const { return { box.getX() + 252, box.getY() + 70, box.getWidth() - 252 - 22, box.getHeight() - 70 - 18 }; }
    juce::AffineTransform secT(int s) const;             // the old place of section s -> the content area, 1.25 x
    struct Placed { juce::Component::SafePointer<juce::Component> c; int sec; };
    std::vector<Placed> placed;
    std::vector<Pill*> nav;
    void classify();                                     // which section each control belongs to
public:
    void showSection(int s);
private:
};

} // namespace arco
