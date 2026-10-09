#pragma once
// iPhone / iPad (1.3): choose the microphone (built-in front / back / bottom, a cable or USB interface, a Bluetooth headset)
// and the output (speaker, cable, Bluetooth, AirPlay - the system picker). Implemented in IOSAudio.mm.
#include <JuceHeader.h>

namespace arco::ios {
struct Port { juce::String id, name, kind; };       // kind: "built-in", "cable", "usb", "bluetooth", "other"
juce::Array<Port> inputs();                          // first entry: "auto" (the best built-in microphone, or what is plugged in)
juce::String chosenInput();                          // "auto" or a port id
void chooseInput(const juce::String& id);            // remembered; applied now and every time the sound starts
void applyInput();                                   // (the host calls this when the sound starts)
juce::String currentInputName();
juce::String currentOutputName();
bool outputIsBluetooth();
bool speakerForced();
void forceSpeaker(bool on);                          // the built-in speaker even with headphones plugged in
void* makeRoutePicker();                             // a UIView* (AVRoutePickerView): Bluetooth, AirPlay, cable, speaker
}
