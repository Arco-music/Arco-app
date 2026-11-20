// ARCO 2.4: PATCHER - the ready-made patches and the lessons.
#include "PatcherExamples.h"

namespace arco { namespace px {

PatchDoc patchFromText(const juce::String& name, const juce::String& text) {
    PatchDoc d; d.name = name;
    std::map<juce::String, int> ids;
    for (auto line : juce::StringArray::fromLines(text)) {
        line = line.trim(); if (line.isEmpty()) continue;
        if (line.startsWith("- ")) {
            for (auto seg : juce::StringArray::fromTokens(line.substring(2), ";", "")) {
                auto t = juce::StringArray::fromTokens(seg.trim(), " ", ""); t.removeEmptyStrings();
                if (t.size() < 4) continue;
                auto a = ids.find(t[0]), b = ids.find(t[2]); if (a == ids.end() || b == ids.end()) continue;
                d.connect(a->second, t[1].getIntValue(), b->second, t[3].getIntValue());
            }
            continue;
        }
        auto t = juce::StringArray::fromTokens(line, " ", ""); if (t.size() < 4) continue;
        juce::String kind = t[0] == "obj" ? "obj" : t[0] == "msg" ? "msg" : t[0] == "com" ? "comment" : "";
        if (kind.isEmpty()) continue;
        t.removeEmptyStrings(); if (t.size() < 4) continue;
        juce::String rest; for (int i = 4; i < t.size(); ++i) rest << (i > 4 ? " " : "") << t[i];
        int id = d.add(kind, rest, (float) t[2].getIntValue(), (float) t[3].getIntValue());
        ids[t[1]] = id;
    }
    return d;
}

const std::vector<Example>& examples() {
    static const std::vector<Example> E = [] {
        std::vector<Example> v;
        auto add = [&](const char* name, const char* shelf, const char* notes, const char* text) { Example e; e.name = name; e.shelf = shelf; e.notes = notes; e.doc = patchFromText(name, text); e.doc.notes = notes; v.push_back(e); };
        // ---- first steps
        add("First sound", "First steps", "A sine wave. Click the speaker to turn it on, click a message to change the note.", R"(
com c1 30 16 click a number, then the speaker
msg m1 30 52 220
msg m2 84 52 440
msg m3 138 52 660
obj n 30 92 flonum
obj o 30 132 cycle~ 440
obj g 30 172 *~ 0.2
obj d 30 216 ezdac~
- m1 0 n 0; m2 0 n 0; m3 0 n 0; n 0 o 0; o 0 g 0; g 0 d 0; g 0 d 1)");
        add("Counting", "First steps", "Click the button: the counter counts 0 to 9 and round again. See it in the number and in the console.", R"(
obj b 30 40 button
obj c 30 84 counter 0 9
obj n 30 124 number
obj p 140 124 print count
- b 0 c 0; c 0 n 0; c 0 p 0)");
        add("Volume with a slider", "First steps", "The slider sets the volume; [line~] makes the change smooth (no clicks).", R"(
com c 30 12 drag the slider
obj s 30 40 slider 0 127
obj sc 90 40 scale 0 127 0. 0.4
msg ms 90 80 $1 20
obj l 90 120 line~
obj o 230 40 cycle~ 330
obj g 230 170 *~
obj d 230 214 ezdac~
- s 0 sc 0; sc 0 ms 0; ms 0 l 0; l 0 g 1; o 0 g 0; g 0 d 0; g 0 d 1)");
        add("Formulas", "First steps", "[expr] writes math as on paper. Change the two numbers.", R"(
obj a 30 40 number
obj b 120 40 number
obj e 30 90 expr sqrt($f1 * $f1 + $f2 * $f2)
obj r 30 134 flonum
- a 0 e 0; b 0 e 1; e 0 r 0)");
        add("Send and receive", "First steps", "[send] and [receive] with the same name talk without a cord.", R"(
msg m 30 40 hello there
obj s 30 80 send talk
obj r 220 40 receive talk
obj p 220 80 print heard
- m 0 s 0; r 0 p 0)");
        // ---- time
        add("Metronome", "Time & rhythm", "A click on every beat of ARCO's tempo. Turn it on with the toggle.", R"(
obj t 30 40 toggle
com c 70 44 on / off
obj m 30 84 metro 4n
obj b 30 124 button
obj k 110 124 click~
obj r 110 164 reson~ 6 1500 25
obj d 110 210 ezdac~
- t 0 m 0; m 0 b 0; m 0 k 0; k 0 r 0; r 0 d 0; r 0 d 1)");
        add("In time with ARCO", "Time & rhythm", "[beat] follows the app's clock: a click on each quarter, in time with the loops and the drums.", R"(
obj bt 30 40 beat 4n
obj k 30 84 click~
obj r 30 124 reson~ 6 1200 30
obj d 30 170 ezdac~
obj n 160 84 number
obj tr 260 40 transport
obj b 260 0 button
obj pr 260 84 print tempo
- bt 0 k 0; k 0 r 0; r 0 d 0; r 0 d 1; bt 1 n 0; b 0 tr 0; tr 1 pr 0)");
        add("Drums from noise", "Time & rhythm", "A kick, a snare and hats made from a sine and noise, played by a counter.", R"(
obj t 30 30 toggle
obj m 30 70 metro 16n
obj c 30 110 counter 0 15
obj ks 30 150 sel 0 8 10
obj ss 200 150 sel 4 12
obj hm 480 150 % 2
obj hs 480 190 sel 0
msg ka 30 200 1, 0 260
msg kp 120 200 160, 45 90
obj kl 30 240 line~
obj kpl 120 240 line~
obj ko 120 280 cycle~
obj kg 30 320 *~
msg sa 200 200 1, 0 140
obj sl 200 240 line~
obj sn 270 240 noise~
obj sf 270 280 filter~ bandpass 1800 0.9
obj sg 200 320 *~
msg ha 480 230 0.5, 0 40
obj hl 480 270 line~
obj hn 580 270 noise~
obj hh 580 310 hip~ 7000
obj hg 480 350 *~
obj d 250 420 dac~ 1 2
- t 0 m 0; m 0 c 0; c 0 ks 0; c 0 ss 0; c 0 hm 0; hm 0 hs 0
- ks 0 ka 0; ks 1 ka 0; ks 2 ka 0; ks 0 kp 0; ks 1 kp 0; ks 2 kp 0; ka 0 kl 0; kp 0 kpl 0; kpl 0 ko 0; ko 0 kg 0; kl 0 kg 1
- ss 0 sa 0; ss 1 sa 0; sa 0 sl 0; sn 0 sf 0; sf 0 sg 0; sl 0 sg 1
- hs 0 ha 0; ha 0 hl 0; hn 0 hh 0; hh 0 hg 0; hl 0 hg 1
- kg 0 d 0; kg 0 d 1; sg 0 d 0; sg 0 d 1; hg 0 d 0; hg 0 d 1)");
        add("Chance rhythm", "Time & rhythm", "Every sixteenth a dice: under 35, a click. Never the same twice.", R"(
obj t 30 30 toggle
obj m 30 70 metro 16n
obj r 30 110 random 100
obj lt 30 150 < 35
obj s 30 190 sel 1
obj k 30 230 click~
obj rs 30 270 reson~ 8 900 18
obj d 30 316 ezdac~
- t 0 m 0; m 0 r 0; r 0 lt 0; lt 0 s 0; s 0 k 0; k 0 rs 0; rs 0 d 0; rs 0 d 1)");
        // ---- notes
        add("Play it with the keyboard", "Notes", "Your own synth: the keys you play (on screen, the computer's, a MIDI keyboard) into a saw, a filter and an envelope.", R"(
com c 30 8 play the keys
obj ni 30 36 notein
obj mt 30 80 mtof
msg ms 30 120 $1 5
obj l 30 160 line~
obj s 30 200 saw~
obj lp 30 240 lores~ 1400 0.4
obj dv 170 80 / 127.
obj e 170 120 adsr~ 5 220 0.6 400
obj g 30 290 *~
obj d 30 336 ezdac~
- ni 0 mt 0; mt 0 ms 0; ms 0 l 0; l 0 s 0; s 0 lp 0; lp 0 g 0; ni 1 dv 0; dv 0 e 0; e 0 g 1; g 0 d 0; g 0 d 1)");
        add("Random melody", "Notes", "A note every eighth, at random but always in a pentatonic scale - with an envelope and a room.", R"(
obj t 30 30 toggle
obj m 30 70 metro 8n
obj r 30 110 random 12
obj sc 30 150 toscale pentatonic
obj p 30 190 + 60
obj f 30 230 mtof
msg ms 30 270 $1 10
obj l 30 310 line~
obj o 30 350 tri~
msg on 200 110 1
obj dl 270 110 delay 90
msg off 270 150 0
obj e 200 190 adsr~ 5 120 0.4 250
obj g 30 400 *~
obj v 30 440 reverb~ 0.75 0.4 0.35
obj d 30 486 dac~ 1 2
- t 0 m 0; m 0 r 0; r 0 sc 0; sc 0 p 0; p 0 f 0; f 0 ms 0; ms 0 l 0; l 0 o 0; m 0 on 0; m 0 dl 0; dl 0 off 0; on 0 e 0; off 0 e 0
- o 0 g 0; e 0 g 1; g 0 v 0; v 0 d 0; v 1 d 1)");
        add("Step sequencer", "Notes", "Draw eight notes in the bars; they play round, in ARCO's tempo.", R"(
com c 30 8 draw the notes, then the toggle
obj ms 30 36 multislider 8 48 72
obj t 220 36 toggle
obj b 220 76 metro 8n
obj cn 220 116 counter 0 7
obj pf 220 156 prepend fetch
obj mt 30 200 mtof
msg l1 30 240 $1 2
obj ln 30 280 line~
obj sw 30 320 saw~
obj lp 30 360 lores~ 900 0.5
msg on 170 240 1
obj dl 240 240 delay 80
msg off 240 280 0
obj e 170 320 adsr~ 3 100 0.4 120
obj g 30 410 *~
obj d 30 456 dac~ 1 2
- t 0 b 0; b 0 cn 0; cn 0 pf 0; pf 0 ms 0; ms 1 mt 0; mt 0 l1 0; l1 0 ln 0; ln 0 sw 0; sw 0 lp 0; lp 0 g 0
- b 0 on 0; b 0 dl 0; dl 0 off 0; on 0 e 0; off 0 e 0; e 0 g 1; g 0 d 0; g 0 d 1)");
        add("Chord player", "Notes", "Each key you play becomes a chord on ARCO's instrument (the KEYBOARD's sound or the SYNTH).", R"(
obj ni 30 36 notein
obj ch 30 80 chord maj7
obj it 30 120 iter
obj no 30 160 noteout
com c 150 80 try min7, sus4, add9 ...
- ni 1 no 1; ni 0 ch 0; ch 0 it 0; it 0 no 0)");
        add("Ambient generator", "Notes", "Slow notes that wander (a random walk) in a scale, glided and drowned in a big room.", R"(
obj lb 30 20 loadbang
obj m 30 60 metro 2n
obj dr 30 100 drunk 12 2
obj sc 30 140 toscale pentatonic
obj p 30 180 + 48
obj f 30 220 mtof
msg ms 30 260 $1 900
obj l 30 300 line~
obj o 30 340 cycle~
obj o2 150 340 *~ 1.5
obj c2 150 380 cycle~
obj g 30 420 *~ 0.12
obj g2 150 420 *~ 0.05
obj v 30 470 reverb~ 0.95 0.3 0.6
obj d 30 516 dac~ 1 2
msg on 140 20 1
- lb 0 on 0; on 0 m 0; m 0 dr 0; dr 0 sc 0; sc 0 p 0; p 0 f 0; f 0 ms 0; ms 0 l 0; l 0 o 0; l 0 o2 0; o2 0 c2 0
- o 0 g 0; c2 0 g2 0; g 0 v 0; g2 0 v 0; v 0 d 0; v 1 d 1)");
        add("MIDI knob on a filter", "Notes", "Turn the mod wheel (controller 1) of your MIDI keyboard: it opens the filter.", R"(
obj c 30 36 ctlin 1
obj sc 30 76 scale 0 127 100 5000
msg ms 30 116 $1 20
obj l 30 156 line~
obj o 170 116 saw~ 110
obj lp 170 196 lores~ 800 0.6
obj g 170 236 *~ 0.25
obj d 170 282 ezdac~
- c 0 sc 0; sc 0 ms 0; ms 0 l 0; l 0 lp 1; o 0 lp 0; lp 0 g 0; g 0 d 0; g 0 d 1)");
        // ---- sound design
        add("Wobble bass", "Sounds", "Two saws through a filter that opens and closes in time (an eighth note).", R"(
obj ph 30 36 phasor~ 8n
obj cs 30 76 cos~
obj sc 30 116 scale~ -1 1 150 2500
obj o 200 36 saw~ 55
obj o2 300 36 saw~ 55.4
obj lp 200 156 lores~ 800 0.7
obj g 200 196 *~ 0.3
obj d 200 242 ezdac~
- ph 0 cs 0; cs 0 sc 0; sc 0 lp 1; o 0 lp 0; o2 0 lp 0; lp 0 g 0; g 0 d 0; g 0 d 1)");
        add("FM bell", "Sounds", "Frequency modulation: one sine moves the pitch of another. Click the button.", R"(
obj b 30 36 button
msg env 30 76 1, 0 2500
obj l 30 116 line~
obj ie 30 156 *~ 600
obj m 190 76 cycle~ 280
obj mi 190 156 *~
obj c 190 196 +~ 200
obj o 190 236 cycle~
obj g 190 276 *~
obj d 190 322 ezdac~
- b 0 env 0; env 0 l 0; l 0 ie 0; ie 0 mi 1; m 0 mi 0; mi 0 c 0; c 0 o 0; o 0 g 0; l 0 g 1; g 0 d 0; g 0 d 1)");
        add("Noise sweep", "Sounds", "Noise through a bandpass that you sweep with a drawn curve (click the button).", R"(
obj b 30 30 button
obj fn 30 70 function 3000
obj sc 30 176 scale~ 0 1 200 6000
obj l 260 70 line~
obj n 260 116 noise~
obj f 260 216 filter~ bandpass 1000 4
obj g 260 256 *~ 0.4
obj d 260 302 ezdac~
- b 0 fn 0; fn 0 l 0; l 0 sc 0; sc 0 f 1; n 0 f 0; f 0 g 0; g 0 d 0; g 0 d 1)");
        add("Watch the sound", "Sounds", "A sine and a saw seen on a scope and a spectrum.", R"(
obj o 30 36 cycle~ 220
obj s 160 36 saw~ 330
obj a 30 76 +~
obj g 30 116 *~ 0.15
obj sp 30 160 scope~
obj sp2 230 160 spectroscope~
obj d 460 116 ezdac~
- o 0 a 0; s 0 a 1; a 0 g 0; g 0 sp 0; g 0 sp2 0; g 0 d 0; g 0 d 1)");
        // ---- effects on a live input
        add("Echo on the microphone", "Effects", "Sing or play into LIVE input 1: echoes in tempo. Use it in STUDIO as an effect too.", R"(
com c 30 8 sound into LIVE input 1
obj a 30 36 adc~ 1
obj e 30 80 echo~ 8n 0.55 0.45
obj d 30 124 dac~ 1 2
obj mt 190 80 meter~
- a 0 e 0; e 0 d 0; e 0 d 1; a 0 mt 0)");
        add("Robot voice", "Effects", "Ring modulation: your voice times a sine. Drag the number.", R"(
obj a 30 36 adc~ 1
obj n 160 36 number
obj o 160 76 cycle~ 40
obj m 30 116 *~
obj d 30 160 dac~ 1 2
- a 0 m 0; n 0 o 0; o 0 m 1; m 0 d 0; m 0 d 1)");
        add("Harmonizer", "Effects", "Your voice with a third and a fifth above it.", R"(
obj a 30 36 adc~ 1
obj p1 30 80 pitchshift~ 4
obj p2 180 80 pitchshift~ 7
obj m 30 124 +~
obj mx 30 164 +~
obj g 30 204 *~ 0.4
obj d 30 248 dac~ 1 2
- a 0 p1 0; a 0 p2 0; p1 0 m 0; p2 0 m 1; m 0 mx 0; a 0 mx 1; mx 0 g 0; g 0 d 0; g 0 d 1)");
        add("Tremolo", "Effects", "The volume goes up and down five times a second.", R"(
obj a 30 36 adc~ 1
obj o 170 36 cycle~ 5
obj s 170 76 scale~ -1 1 0.2 1
obj m 30 116 *~
obj d 30 160 dac~ 1 2
- a 0 m 0; o 0 s 0; s 0 m 1; m 0 d 0; m 0 d 1)");
        add("Distortion box", "Effects", "Drive the knob for dirt, the filter keeps it warm.", R"(
obj a 30 36 adc~ 1
obj k 170 20 dial 1 20
obj od 30 96 overdrive~ 6
obj f 30 136 filter~ lowpass 3500 0.7
obj g 30 176 *~ 0.5
obj d 30 220 dac~ 1 2
- a 0 od 0; k 0 od 1; od 0 f 0; f 0 g 0; g 0 d 0; g 0 d 1)");
        add("Bitcrusher", "Effects", "Lo-fi: fewer samples and fewer bits.", R"(
obj a 30 36 adc~ 1
obj r 170 36 flonum
obj bt 260 36 number
obj dg 30 96 degrade~ 0.2 6
obj d 30 140 dac~ 1 2
- a 0 dg 0; r 0 dg 1; bt 0 dg 2; dg 0 d 0; dg 0 d 1)");
        add("Auto-pan", "Effects", "The sound moves from left to right and back.", R"(
obj a 30 36 adc~ 1
obj ph 170 36 phasor~ 0.25
obj cs 170 76 cos~
obj sc 170 116 scale~ -1 1 0 1
obj p 30 156 pan~
obj d 30 200 dac~ 1 2
- a 0 p 0; ph 0 cs 0; cs 0 sc 0; sc 0 p 1; p 0 d 0; p 1 d 1)");
        add("Compressor and limiter", "Effects", "Even loudness, and never too loud.", R"(
obj a 30 36 adc~ 1 2
obj c 30 80 compressor~ -24 4 5 120
obj l 30 124 limiter~ -1
obj d 30 168 dac~ 1 2
obj m 220 124 meter~
- a 0 c 0; a 1 c 1; c 0 l 0; c 1 l 1; l 0 d 0; l 1 d 1; l 0 m 0)");
        add("Strange shifter", "Effects", "Every frequency moves by the same amount: metallic, bell-like.", R"(
obj a 30 36 adc~ 1
obj n 170 36 number
obj f 30 80 freqshift~ 30
obj d 30 124 dac~ 1 2
- a 0 f 0; n 0 f 1; f 0 d 0; f 1 d 1)");
        // ---- samples
        add("Looper", "Samples", "Toggle 1 records 4 seconds of LIVE input 1, toggle 2 plays them round.", R"(
com c 30 8 1: record   2: play round
obj buf 30 36 buffer~ loop 4000
obj a 30 76 adc~ 1
obj tr 170 76 toggle
obj rec 30 120 record~ loop
obj lb 300 116 loadbang
msg lp 300 156 loop 1
obj tp 170 156 toggle
obj s1 230 196 sel 1
msg z 230 236 0
obj sg 170 236 sig~
obj gr 30 276 groove~ loop
obj d 30 320 dac~ 1 2
- a 0 rec 0; tr 0 rec 0; lb 0 lp 0; lp 0 gr 0; tp 0 sg 0; tp 0 s1 0; s1 0 z 0; z 0 gr 0; sg 0 gr 0; gr 0 d 0; gr 0 d 1)");
        add("Sample player", "Samples", "Drop a sound file on [buffer~], then click the button. The number changes its speed.", R"(
com c 30 8 drop a sound file on the buffer~
obj buf 30 36 buffer~ snd 2000
obj b 30 76 button
msg s 30 116 start
obj p 30 156 play~ snd
obj d 30 200 dac~ 1 2
obj w 200 36 waveform~ snd
- b 0 s 0; s 0 p 0; p 0 d 0; p 0 d 1)");
        return v;
    }();
    return E;
}

const std::vector<Lesson>& lessons() {
    static const std::vector<Lesson> L = {
        { "1  Boxes and cords", "Everything is a box. A box does one thing; cords carry what it says to the next box.\n\n"
          "- Double-click on the empty canvas (or press N) and type a name: a box appears. While you type, the list shows what each one does.\n"
          "- Drag from an outlet (the dots under a box) to an inlet (the dots on top) to connect them.\n"
          "- Press Play (top) to use the patch: click, drag, play. Edit to change it again (Cmd E switches).", "Counting" },
        { "2  Hot and cold", "The LEFT inlet of a box is hot: what comes in makes it answer. The others are cold: they only remember.\n\n"
          "So [+ 10] adds when a number comes in on the left, using the last number it got on the right. That is how you choose WHEN things happen.", "Formulas" },
        { "3  Make a sound", "Boxes with ~ make or change sound, all the time (cords with sound are thicker and coloured).\n\n"
          "[cycle~ 440] is a sine at 440 Hz. [*~ 0.2] makes it quieter. [ezdac~] (a speaker) or [dac~ 1 2] lets you hear it. Always keep the volume low at first.", "First sound" },
        { "4  Time", "[metro 500] bangs every 500 ms; [metro 8n] every eighth note of ARCO's tempo. [delay 300] waits. [line~] glides a sound's level - no clicks.\n\n"
          "A toggle turns a metro on and off.", "Metronome" },
        { "5  Order: right to left", "When one outlet goes to several boxes, the box furthest RIGHT gets it first. Out of one box with several outlets, the right outlet speaks first.\n\n"
          "When order matters, use [t b i] (trigger): it sends right to left, clearly.", "Volume with a slider" },
        { "6  Notes", "[notein] gives you the notes you play: number and velocity. [mtof] turns a note number into Hz for an oscillator. [adsr~] shapes each note's volume.\n\n"
          "[noteout] plays ARCO's own instrument instead.", "Play it with the keyboard" },
        { "7  An effect on your sound", "[adc~ 1] is LIVE input 1 (a microphone, a guitar). Change it and send it to [dac~]. This patch can also be an effect on a track in STUDIO: \"Use in Studio\" at the top.", "Echo on the microphone" },
        { "8  Remember things", "[coll] keeps lists by number or name. [table] keeps 128 numbers. [buffer~] keeps a sound: [record~] writes into it, [play~] and [groove~] play it.", "Looper" },
        { "9  Tidy patches", "[p name] is a patch inside a box (double-click to open it; [inlet] and [outlet] inside). [send] and [receive] with the same name connect without cords.", "Send and receive" },
        { "10  In time with ARCO", "[beat 16n] bangs on ARCO's clock - with the loops, the drums and the studio. [transport] tells you the tempo and the bar, and can start and stop the app. [phasor~ 1n] is a ramp once a bar.", "In time with ARCO" },
        { "11  Find anything", "The search on the left understands what you want: type \"echo\", \"random\", \"microphone\", \"make it wider\". Click a result to put it in the middle, or drag it to a place.\n\n"
          "Choose a box: the help on the right says what each inlet and outlet does, with an example.", "Random melody" },
    };
    return L;
}

}} // namespace arco::px
