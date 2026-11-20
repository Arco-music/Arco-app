#pragma once
// ARCO 2.3: a plug-in (AU / VST3) living on a STUDIO lane - its own window (the plug-in's own face), its sound in
// the lane's chain. Shared by the song's copies (undo, export): one running instance per device.
#include <JuceHeader.h>
#include <memory>

namespace arco {

struct PluginSlot {
    std::unique_ptr<juce::AudioPluginInstance> inst;
    juce::CriticalSection lock;                     // the audio thread plays under a try-lock; the window / state take it
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    juce::String name, maker;
    bool instrument = false;
    int latency = 0;
    std::unique_ptr<juce::DocumentWindow> win;

    PluginSlot() = default;
    PluginSlot(const PluginSlot&) = delete;
    ~PluginSlot() {
        // the plug-in and its window go on the message thread (the last copy can be let go elsewhere: an export)
        if (!juce::MessageManager::existsAndIsCurrentThread() && juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
            auto* w = win.release(); auto* p = inst.release();
            juce::MessageManager::callAsync([w, p] { delete w; delete p; });
        } else { win.reset(); inst.reset(); }
    }
    void prepare(double sr, int block) {
        if (!inst) return;
        inst->enableAllBuses();
        inst->setRateAndBufferSizeDetails(sr, block);
        inst->prepareToPlay(sr, block);
        int ch = juce::jmax(2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
        buf.setSize(ch, juce::jmax(block, 1024)); buf.clear();
        midi.ensureSize(4096);
        latency = inst->getLatencySamples();
        instrument = inst->getPluginDescription().isInstrument;
    }
    // an effect: L / R through it, in place (n <= the prepared block)
    void process(float* L, float* R, int n) {
        const juce::ScopedTryLock sl(lock);
        if (!sl.isLocked() || inst == nullptr || n > buf.getNumSamples()) return;
        juce::AudioBuffer<float> b(buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
        b.clear();
        b.copyFrom(0, 0, L, n); b.copyFrom(1, 0, R, n);
        midi.clear();
        inst->processBlock(b, midi);
        std::copy(b.getReadPointer(0), b.getReadPointer(0) + n, L);
        std::copy(b.getReadPointer(b.getNumChannels() > 1 ? 1 : 0), b.getReadPointer(b.getNumChannels() > 1 ? 1 : 0) + n, R);
    }
    // an instrument: the notes in, its sound added to L / R
    void play(const juce::MidiBuffer& notes, float* L, float* R, int n) {
        const juce::ScopedTryLock sl(lock);
        if (!sl.isLocked() || inst == nullptr || n > buf.getNumSamples()) return;
        juce::AudioBuffer<float> b(buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
        b.clear();
        midi.clear(); midi.addEvents(notes, 0, n, 0);
        inst->processBlock(b, midi);
        for (int i = 0; i < n; ++i) { L[i] += b.getSample(0, i); R[i] += b.getSample(b.getNumChannels() > 1 ? 1 : 0, i); }
    }
    juce::String state() {
        if (!inst) return {};
        const juce::ScopedLock sl(lock);
        juce::MemoryBlock mb; inst->getStateInformation(mb); return mb.toBase64Encoding();
    }
    void setState(const juce::String& s) {
        if (!inst || s.isEmpty()) return;
        juce::MemoryBlock mb; if (!mb.fromBase64Encoding(s)) return;
        const juce::ScopedLock sl(lock); inst->setStateInformation(mb.getData(), (int) mb.getSize());
    }
    // the plug-in's own window (its own face, as its maker drew it)
    void show() {
        if (!inst) return;
        struct Win : juce::DocumentWindow {
            Win(const juce::String& n) : juce::DocumentWindow(n, juce::Colours::black, juce::DocumentWindow::closeButton) {}
            void closeButtonPressed() override { setVisible(false); }
        };
        if (win == nullptr) {
            auto* ed = inst->createEditorIfNeeded();
            if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor(*inst);
            auto* w = new Win(name + (maker.isNotEmpty() ? "  -  " + maker : juce::String()));
            w->setUsingNativeTitleBar(true);
            w->setContentOwned(ed, true);
            w->setResizable(ed->isResizable(), false);
            w->setAlwaysOnTop(true);
            w->centreWithSize(w->getWidth(), w->getHeight());
            win.reset(w);
        }
        win->setVisible(true); win->toFront(true);
    }
};
using PluginSlotPtr = std::shared_ptr<PluginSlot>;

} // namespace arco
