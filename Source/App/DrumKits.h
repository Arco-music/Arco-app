#pragma once
// ARCO 2.0: the DRUM MACHINE's kits. JAZZ is sampled (Virtuosity Drums, CC0 - close mics, overheads and room
// mixed per hit, built into the app by tools/drumkit); 808, 909 and TRAP are modelled the way those machines make
// their sounds (rendered once, at start); LO-FI is the jazz kit through a tape and an old sampler.
#include <JuceHeader.h>
#include "../Engine/DrumMachine.h"

namespace arco {

struct DrumKits {
    static const char* padKey(int pad);             // "kick", "snare" ...
    static juce::StringArray kitNames();             // JAZZ, 808, 909, TRAP, LO-FI
    static juce::String padName(int kit, int pad);   // what the pad is in that kit (CROSS STICK in JAZZ, CLAP in 808 ...)
    static const DrumBank* bank();                   // made once, kept for the life of the program
    static int midiNote(int pad);                    // General MIDI drum notes (a MIDI keyboard / pads play them)
    static int padForNote(int note);
};

} // namespace arco
