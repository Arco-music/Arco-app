// ARCO 2.5: FLUXO - the blocks (the drawers), and the recipes "write what you want" builds from.
#include "Fluxo.h"

namespace arco { namespace fx {

const std::vector<Opt>& timeOpts() {
    static const std::vector<Opt> o = { { "1/32", "32n" }, { "1/16", "16n" }, { "1/16 triplet", "16nt" }, { "1/8", "8n" }, { "1/8 triplet", "8nt" }, { "dotted 1/8", "8nd" },
        { "1/4", "4n" }, { "1/4 triplet", "4nt" }, { "dotted 1/4", "4nd" }, { "1/2", "2n" }, { "1 bar", "1n" }, { "2 bars", "2bar" }, { "4 bars", "4bar" }, { "8 bars", "8bar" } };
    return o;
}
const std::vector<Opt>& scaleOpts() {
    static const std::vector<Opt> o = { { "major", "major" }, { "minor", "minor" }, { "dorian", "dorian" }, { "phrygian", "phrygian" }, { "lydian", "lydian" }, { "mixolydian", "mixolydian" },
        { "pentatonic", "pentatonic" }, { "minor pentatonic", "minorpentatonic" }, { "blues", "blues" }, { "harmonic minor", "harmonic" }, { "whole tone", "wholetone" },
        { "japanese", "japanese" }, { "arabic", "arabic" }, { "every note", "chromatic" } };
    return o;
}
const std::vector<Opt>& rootOpts() {
    static const std::vector<Opt> o = { { "C", "0" }, { "C#", "1" }, { "D", "2" }, { "Eb", "3" }, { "E", "4" }, { "F", "5" }, { "F#", "6" }, { "G", "7" }, { "Ab", "8" }, { "A", "9" }, { "Bb", "10" }, { "B", "11" } };
    return o;
}
static int timeIx(const char* v) { auto& o = timeOpts(); for (int i = 0; i < (int) o.size(); ++i) if (o[(size_t) i].value == v) return i; return 3; }
static int scaleIx(const char* v) { auto& o = scaleOpts(); for (int i = 0; i < (int) o.size(); ++i) if (o[(size_t) i].value == v) return i; return 0; }

static std::vector<BlockDef> build() {
    std::vector<BlockDef> L;
    auto B = [&](const char* id, const char* shelf, const char* name, const char* sentence, const char* help, const char* words) -> BlockDef& {
        BlockDef d; d.id = id; d.shelf = shelf; d.name = name; d.sentence = sentence; d.help = help; d.words = words; L.push_back(d); return L.back(); };
    const char* LI = "Listen"; const char* TO = "Touch"; const char* TI = "Time"; const char* NO = "Notes"; const char* SO = "Sound"; const char* SA = "Samples";
    const char* CH = "Change the sound"; const char* MO = "Move by itself"; const char* LO = "Numbers & logic"; const char* WA = "Watch"; const char* OU = "Out"; const char* PO = "Ways in & out"; const char* MX = "Max boxes";
    auto sym = [](const char* box, int inlet) { return std::vector<To> { { box, inlet, "symbol $1" } }; };
    auto num = [](const char* box, int inlet) { return std::vector<To> { { box, inlet, "$1" } }; };
    auto num2 = [](const char* a, int ia, const char* b, int ib) { return std::vector<To> { { a, ia, "$1" }, { b, ib, "$1" } }; };
    auto sym2 = [](const char* a, int ia, const char* b, int ib) { return std::vector<To> { { a, ia, "symbol $1" }, { b, ib, "symbol $1" } }; };

    // ---------------------------------------------------------------- LISTEN
    B("listen.input", LI, "Input", "Listen to {input}", "A microphone, a guitar, anything plugged into the sound card - or the station's whole mix.", "input microphone mic guitar voice listen record entrada microfone voz")
        .choice("input", { { "input 1", "1 1" }, { "input 2", "2 2" }, { "input 3", "3 3" }, { "input 4", "4 4" }, { "inputs 1 + 2", "1 2" }, { "the station's mix", "5 6" } }, 0, {}).baked()
        .out("sound", "sound", Sound, "a", 0, "a", 1).widget("level", "m", 10)
        .patch("obj a 0 0 adc~ {input}\nobj m 0 60 follow~ 1\n- a 0 m 0");
    B("listen.keys", LI, "Keys you play", "The notes you play", "Everything you play: a MIDI keyboard, the computer keys, the keys on screen.", "keyboard keys notes midi play piano teclado notas")
        .out("notes", "notes", Notes, "p", 0).patch("obj n 0 0 notein\nobj p 0 40 pack 0 0\n- n 0 p 0; n 1 p 1");
    B("listen.knob", LI, "MIDI knob", "MIDI knob number {cc}", "A knob or fader of your MIDI controller, as a movement 0 to 1. Number 1 is the mod wheel.", "midi knob fader controller cc mod wheel botao")
        .num("cc", 0, 127, 1, "", {}, 1, 1).baked().out("move", "0 to 1", Move, "d", 0).widget("move", "v", 10)
        .patch("obj c 0 0 ctlin {cc}\nobj d 0 40 / 127.\nobj v 0 80 flonum\n- c 0 d 0; d 0 v 0");
    B("listen.wheel", LI, "Pitch wheel", "The pitch-bend wheel", "The pitch-bend wheel, 0 to 1 (the middle is 0.5).", "pitch bend wheel")
        .out("move", "0 to 1", Move, "d", 0).patch("obj c 0 0 bendin\nobj d 0 40 / 127.\n- c 0 d 0");
    B("listen.pitch", LI, "Hear the note", "Listen for the note being sung or played", "Hears which note is sounding (a voice, a flute, a cello) and turns it into notes - sing and a synth follows you.", "pitch tracking sing voice follow tuner listen note detect cantar afinador")
        .in("sound", "sound", Sound, "f", 0).out("notes", "notes", Notes, "n", 0).out("move", "how high", Move, "h", 0)
        .patch("obj f 0 0 fzero~\nobj n 0 40 hz2note 100\nobj m 120 40 ftom\nobj h 120 80 / 127.\n- f 0 n 0; f 0 m 0; m 0 h 0");

    // ---------------------------------------------------------------- TOUCH
    B("touch.button", TO, "Button", "A button: a tap when you press it", "Press it to send a tap - to start things, play a drum, jump a step.", "button bang tap trigger press botao")
        .out("tap", "tap", Move, "b", 0).widget("button", "b", 44).patch("obj b 0 0 button");
    B("touch.switch", TO, "Switch", "A switch: on or off", "On (1) or off (0) - to start and stop things.", "switch toggle on off interruptor ligar")
        .out("move", "1 / 0", Move, "t", 0).widget("toggle", "t", 40).patch("obj t 0 0 toggle");
    B("touch.slider", TO, "Slider", "A slider you move", "Drag it: a movement from 0 to 1. Plug it into any word of any block.", "slider fader control move hand deslizador")
        .out("move", "0 to 1", Move, "s", 0).widget("slider", "s", 34).patch("obj s 0 0 hslider 0. 1.");
    B("touch.knob", TO, "Knob", "A knob you turn", "Turn it: a movement from 0 to 1.", "knob dial turn control rodar")
        .out("move", "0 to 1", Move, "s", 0).widget("knob", "s", 64).patch("obj s 0 0 dial 0. 1.");
    B("touch.xy", TO, "XY pad", "A square you play with one finger: across and up", "Drag the dot: across is the first movement, up the second - two things at once.", "xy pad touch two axis kaoss")
        .out("x", "across", Move, "x", 0).out("y", "up", Move, "y", 0).widget("xy", "x", 120).patch("obj x 0 0 float 0.5\nobj y 60 0 float 0.5");
    B("touch.keys", TO, "Keys on screen", "Keys to play with the mouse", "A small keyboard on the block.", "keys keyboard piano screen mouse teclado")
        .out("notes", "notes", Notes, "p", 0).widget("keys", "k", 54).patch("obj k 0 0 kslider\nobj p 0 60 pack 0 0\n- k 0 p 0; k 1 p 1");
    B("touch.message", TO, "Message", "Click to send: {text}", "A message you click: words or numbers (\"start\", \"0.5\", \"60 100\").", "message click send words text mensagem")
        .text("text", "1", {}, true).out("out", "it", Any, "m", 0).widget("message", "m", 32).patch("msg m 0 0 {text}");

    // ---------------------------------------------------------------- TIME
    B("time.every", TI, "Every", "A tap every {time}", "A tap in time with ARCO (and on its own when ARCO is stopped). The heart of rhythms.", "every clock tick beat metronome pulse time tempo cada pulso")
        .choice("time", timeOpts(), timeIx("8n"), sym("b", 1)).in("onoff", "on / off", Move, "b", 0).out("tap", "tap", Move, "b", 0).out("count", "the count", Any, "b", 1)
        .widget("blink", "b", 10).patch("obj b 0 0 beat 8n");
    B("time.metro", TI, "Metronome", "A tap every {ms}", "Like Every, but in milliseconds, free of the tempo.", "metro metronome ms milliseconds clock")
        .num("ms", 10, 4000, 250, "ms", num("m", 1), 1, 1, true).in("onoff", "on / off", Move, "m", 0).out("tap", "tap", Move, "m", 0)
        .patch("obj l 0 0 loadbang\nobj m 0 40 metro 250\n- l 0 m 0");
    B("time.rhythm", TI, "Rhythm", "Every {time}: {hits} hits spread over {steps} steps, turned {rot}", "Euclidean rhythms: 3 over 8 is a tresillo, 5 over 8 a cinquillo, 4 over 16 a four-on-the-floor.", "rhythm euclidean pattern groove beat drums hits ritmo")
        .choice("time", timeOpts(), timeIx("16n"), sym("b", 1)).num("hits", 0, 32, 3, "", num("e", 1), 1, 1).num("steps", 1, 32, 8, "", num("e", 2), 1, 1).num("rot", 0, 31, 0, "", num("e", 3), 1, 1)
        .in("onoff", "on / off", Move, "b", 0).out("tap", "tap", Move, "e", 0).out("step", "the step", Any, "e", 1).widget("euclid", "e", 22).tip("Three of these with different numbers, each on a drum: polyrhythm.")
        .patch("obj b 0 0 beat 16n\nobj e 0 40 euclid 3 8 0\n- b 1 e 0");
    B("time.steps", TI, "Step sequencer", "Every {time}, play the {len} steps you draw", "Draw the steps (click to turn one on or off, drag up and down). Each step gives a tap and a value.", "step sequencer steps pattern draw sequence passos")
        .choice("time", timeOpts(), timeIx("16n"), sym("b", 1)).num("len", 1, 32, 16, "", num("s", 1), 1, 1)
        .in("onoff", "on / off", Move, "b", 0).out("value", "value 0 to 1", Move, "s", 0).out("tap", "tap", Move, "s", 1).widget("steps", "s", 64).tip("The value into a filter's frequency, the tap into a drum.")
        .patch("obj b 0 0 beat 16n\nobj s 0 40 steps 16\n- b 1 s 0");
    B("time.count", TI, "Count", "Count the taps from 0 to {max}", "Counts: 0, 1, 2 ... and round again. A tap at the end of each round.", "count counter number steps contar")
        .num("max", 1, 128, 7, "", num("c", 3), 1, 1).in("tap", "tap", Move, "c", 0).out("count", "the count", Any, "c", 0).out("end", "tap at the top", Move, "c", 2)
        .patch("obj c 0 0 counter 0 7");
    B("time.wait", TI, "Wait", "Wait {time}, then tap", "Each tap comes out later (a new one starts the wait again).", "wait delay later after esperar")
        .choice("time", timeOpts(), timeIx("4n"), sym("d", 1)).in("tap", "tap", Move, "d", 0).out("tap", "tap", Move, "d", 0).patch("obj d 0 0 delay 4n");
    B("time.sometimes", TI, "Sometimes", "Let it through {p} of the time", "Chance: some taps or notes pass, the others don't - patterns that never repeat.", "chance probability random sometimes maybe acaso")
        .num("p", 0, 100, 50, "%", num("c", 1), 0.01, 1).in("in", "anything", Any, "c", 0).out("out", "passed", Any, "c", 0).out("no", "the others", Any, "c", 1).patch("obj c 0 0 chance 0.5");
    B("time.quantize", TI, "Land in time", "Wait for the next {grid}", "Holds what comes in until the next step of ARCO's clock: everything lands in time.", "quantize grid in time snap align")
        .choice("grid", timeOpts(), timeIx("16n"), sym("q", 1)).in("in", "anything", Any, "q", 0).out("out", "in time", Any, "q", 0).patch("obj q 0 0 quantize 16n");
    B("time.slower", TI, "Not too fast", "At most one every {ms}", "Lets at most one message through every so many ms.", "speedlim limit slower throttle")
        .num("ms", 1, 2000, 50, "ms", num("s", 1), 1, 1, true).in("in", "anything", Any, "s", 0).out("out", "slower", Any, "s", 0).patch("obj s 0 0 speedlim 50");
    B("time.transport", TI, "Play / stop ARCO", "Start (1) or stop (0) ARCO, or set the tempo to {bpm} with a tap", "Drives ARCO's clock: the loops, the drums, everything plays with it.", "transport play stop start tempo bpm clock tocar parar")
        .num("bpm", 40, 240, 120, "BPM", {}, 1, 1).baked().in("onoff", "1 play / 0 stop", Move, "t", 0).in("tempo", "tap: set the tempo", Move, "m", 0).out("playing", "playing", Any, "t", 0)
        .patch("obj t 0 0 transport\nmsg m 100 0 tempo {bpm}\n- m 0 t 0");

    // ---------------------------------------------------------------- NOTES
    B("notes.random", NO, "Random notes", "On each tap, a note of {scale} in {root}, from {low} to {high}, {len} long", "Melodies that never end: each tap picks a note of the scale.", "random notes melody generative scale aleatorio melodia")
        .choice("scale", scaleOpts(), scaleIx("minor"), sym("r", 1)).choice("root", rootOpts(), 9, num("r", 2)).note("low", 57, num("r", 3)).note("high", 81, num("r", 4))
        .choice("len", timeOpts(), timeIx("8n"), sym("n", 1)).in("tap", "tap", Move, "r", 0).out("notes", "notes", Notes, "n", 0).tip("Feed it from 'Every' through 'Sometimes': a melody with breath.")
        .patch("obj r 0 0 rnote minor 9 57 81\nobj n 0 40 nlen 8n 100\n- r 0 n 0");
    B("notes.arp", NO, "Arpeggio", "Plays the notes you hold {mode}, every {time}, over {oct} octaves, {len} long, {latch}", "Hold a chord: it is played one note at a time.", "arpeggio arpeggiator arp chord broken arpejo")
        .choice("mode", { { "going up", "up" }, { "going down", "down" }, { "up and down", "updown" }, { "at random", "random" }, { "as you played them", "played" } }, 0, sym("a", 1))
        .choice("time", timeOpts(), timeIx("16n"), sym("b", 1)).num("oct", 1, 4, 1, "", num("a", 2), 1, 1).choice("len", timeOpts(), timeIx("16n"), sym("n", 1))
        .choice("latch", { { "only while held", "0" }, { "holding on", "1" } }, 0, num("a", 3))
        .in("notes", "notes", Notes, "a", 0).out("notes", "notes", Notes, "n", 0).tip("Put 'Keep in key' before it, and 'Note echo' after it.")
        .patch("obj b 0 0 beat 16n\nobj a 0 40 arp up 1\nobj n 0 80 nlen 16n 100\n- b 0 a 0; a 0 n 0");
    B("notes.chord", NO, "Chord", "Every note becomes a {type} chord, {inv}", "One finger, whole chords.", "chord harmony triad major minor seventh acorde")
        .choice("type", { { "major", "maj" }, { "minor", "min" }, { "7th", "7" }, { "major 7th", "maj7" }, { "minor 7th", "min7" }, { "diminished", "dim" }, { "augmented", "aug" }, { "sus2", "sus2" }, { "sus4", "sus4" }, { "add9", "add9" }, { "minor 9th", "min9" }, { "6th", "6" }, { "power (5th)", "fifth" }, { "octave", "octave" }, { "stacked fifths", "stack" } }, 0, sym("c", 1))
        .choice("inv", { { "as it is", "0" }, { "1st inversion", "1" }, { "2nd inversion", "2" } }, 0, num("c", 2))
        .in("notes", "notes", Notes, "c", 0).out("notes", "notes", Notes, "c", 0).patch("obj c 0 0 chordnotes maj");
    B("notes.key", NO, "Keep in key", "Keep every note in {scale} of {root}", "No wrong notes: each one moves to the nearest note of the scale.", "scale key quantize in tune lock tonalidade escala")
        .choice("scale", scaleOpts(), 0, sym("s", 1)).choice("root", rootOpts(), 0, num("s", 2)).in("notes", "notes", Notes, "s", 0).out("notes", "notes", Notes, "s", 0).patch("obj s 0 0 nscale major 0");
    B("notes.transpose", NO, "Up / down", "Move the notes by {by}", "Transpose: 12 is an octave up, -12 down, 7 a fifth.", "transpose octave shift up down transpor")
        .num("by", -36, 36, 12, "semitones", num("t", 1), 1, 1).in("notes", "notes", Notes, "t", 0).out("notes", "notes", Notes, "t", 0).patch("obj t 0 0 ntrans 12");
    B("notes.length", NO, "Note length", "Every note lasts {len}", "Short and dry, or long and singing.", "length duration staccato legato gate")
        .choice("len", timeOpts(), timeIx("16n"), sym("n", 1)).in("notes", "notes", Notes, "n", 0).out("notes", "notes", Notes, "n", 0).patch("obj n 0 0 nlen 16n 100");
    B("notes.velocity", NO, "How hard", "Play them {amt} as hard, give or take {rnd}", "Louder, softer, more alive.", "velocity dynamics accent loud soft")
        .num("amt", 10, 200, 100, "%", num("v", 1), 0.01, 1).num("rnd", 0, 100, 10, "%", num("v", 2), 0.01, 1).in("notes", "notes", Notes, "v", 0).out("notes", "notes", Notes, "v", 0).patch("obj v 0 0 nvel 1 0.1");
    B("notes.human", NO, "Like a person", "Up to {ms} late, {vel} looser", "Notes a little late, a little softer or harder - it breathes.", "humanize human feel groove loose")
        .num("ms", 0, 80, 15, "ms", num("h", 1), 1, 1).num("vel", 0, 50, 15, "%", num("h", 2), 0.01, 1).in("notes", "notes", Notes, "h", 0).out("notes", "notes", Notes, "h", 0).patch("obj h 0 0 humanize 15 0.15");
    B("notes.echo", NO, "Note echo", "Each note comes back every {time}, {count} times, {fade} each time", "Echoes made of notes: each one can play another sound.", "note echo repeat delay midi")
        .choice("time", timeOpts(), timeIx("8n"), sym("e", 1)).num("count", 1, 16, 3, "times", num("e", 2), 1, 1).num("fade", 10, 100, 60, "%", num("e", 3), 0.01, 1)
        .in("notes", "notes", Notes, "e", 0).out("notes", "notes", Notes, "e", 0).patch("obj e 0 0 notecho 8n 3 0.6");
    B("notes.split", NO, "Split the keys", "Notes from {low} to {high} go left, the rest right", "Two instruments on one keyboard: bass on the left, melody on the right.", "split keyboard zones layer")
        .note("low", 0, num("s", 1)).note("high", 59, num("s", 2)).in("notes", "notes", Notes, "s", 0).out("in", "inside", Notes, "s", 0).out("out", "outside", Notes, "s", 1).patch("obj s 0 0 nsplit 0 59");
    B("notes.melody", NO, "Melody steps", "Every {time}, {len} steps from {low} to {high} in {scale} of {root}, {nlen} long", "Draw a melody: each step is a note (higher is higher). Click a step to make it a rest.", "melody sequencer steps notes draw melodia")
        .choice("time", timeOpts(), timeIx("8n"), sym("b", 1)).num("len", 1, 32, 8, "", num("s", 1), 1, 1).note("low", 57, num("c", 3)).note("high", 81, num("c", 4))
        .choice("scale", scaleOpts(), scaleIx("minor"), sym("k", 1)).choice("root", rootOpts(), 9, num("k", 2)).choice("nlen", timeOpts(), timeIx("8n"), sym("n", 1))
        .in("onoff", "on / off", Move, "b", 0).out("notes", "notes", Notes, "n", 0).widget("steps", "s", 64)
        .patch("obj b 0 0 beat 8n\nobj s 0 40 steps 8\nobj c 0 80 scale 0. 1. 57. 81.\nobj r 0 120 round\nobj k 0 160 nscale minor 9\nobj n 0 200 nlen 8n 100\n- b 0 s 0; s 0 c 0; c 0 r 0; r 0 k 0; k 0 n 0");
    B("notes.fromnumbers", NO, "Numbers into notes", "Turn movement into notes from {low} to {high} in {scale} of {root}, {len} long", "A slow wave, the XY pad, your voice's loudness... becomes a melody.", "numbers to notes map melody")
        .note("low", 48, num("c", 3)).note("high", 84, num("c", 4)).choice("scale", scaleOpts(), 0, sym("k", 1)).choice("root", rootOpts(), 0, num("k", 2)).choice("len", timeOpts(), timeIx("8n"), sym("n", 1))
        .in("move", "0 to 1", Move, "c", 0).out("notes", "notes", Notes, "n", 0)
        .patch("obj c 0 0 scale 0. 1. 48. 84.\nobj r 0 40 round\nobj k 0 80 nscale major 0\nobj g 0 120 change\nobj n 0 160 nlen 8n 100\n- c 0 r 0; r 0 k 0; k 0 g 0; g 0 n 0");

    // ---------------------------------------------------------------- SOUND
    B("sound.synth", SO, "Synth", "Plays the notes with a {wave} sound, {bright} bright, {atk} in, {rel} out", "An 8-voice synth: chords, pads, leads, basses.", "synth synthesizer instrument pad lead bass keys sintetizador")
        .choice("wave", { { "warm saw", "saw" }, { "hollow square", "square" }, { "soft triangle", "triangle" }, { "pure sine", "sine" }, { "round organ", "soft" } }, 0, sym("s", 1))
        .num("bright", 0, 100, 55, "%", num("s", 2), 0.01, 1).num("atk", 1, 3000, 8, "ms", num("s", 3), 1, 1, true).num("rel", 10, 6000, 350, "ms", num("s", 4), 1, 1, true)
        .in("notes", "notes", Notes, "s", 0).out("sound", "sound", Sound, "s", 0, "s", 1).patch("obj s 0 0 synth~ saw 0.55 8 350").tip("Plug a slow wave into 'bright': the sound opens and closes by itself.");
    B("sound.tone", SO, "Tone", "A {wave} tone at {hz}, {vol} loud", "One steady tone. Plug notes in to play it, or a movement into its pitch.", "tone oscillator sine wave beep frequency hz drone tom")
        .choice("wave", { { "sine", "cycle~" }, { "saw", "saw~" }, { "square", "rect~" }, { "triangle", "tri~" } }, 0, {}).baked()
        .num("hz", 20, 8000, 220, "Hz", num("o", 0), 1, 0, true).num("vol", 0, 100, 30, "%", num("g", 1), 0.01, 1)
        .in("notes", "notes", Notes, "u", 0).out("sound", "sound", Sound, "g", 0)
        .patch("obj u 0 0 unpack 0 0\nobj sg 0 30 stripnote\nobj m 0 60 mtof\nobj o 0 100 {wave} 220\nobj g 0 140 *~ 0.3\n- u 0 sg 0; u 1 sg 1; sg 0 m 0; m 0 o 0; o 0 g 0");
    B("sound.noise", SO, "Noise", "{color} noise, {vol} loud", "Hiss, rain, wind, the sea - and the start of drums and risers.", "noise white pink hiss rain wind ruido")
        .choice("color", { { "white", "noise~" }, { "pink", "pink~" } }, 1, {}).baked().num("vol", 0, 100, 20, "%", num("g", 1), 0.01, 1)
        .out("sound", "sound", Sound, "g", 0).patch("obj n 0 0 {color}\nobj g 0 40 *~ 0.2\n- n 0 g 0");
    B("sound.drum", SO, "Drum", "Each tap plays a {kind}, tone {tone}, {decay} long", "A synthesized drum for each tap (or note).", "drum kick snare hat clap tom percussion bateria bombo tarola")
        .choice("kind", { { "kick", "kick" }, { "snare", "snare" }, { "closed hat", "hat" }, { "open hat", "openhat" }, { "clap", "clap" }, { "tom", "tom" }, { "rim", "rim" }, { "cowbell", "cowbell" } }, 0, sym("d", 1))
        .num("tone", 0, 100, 50, "%", num("d", 2), 0.01, 1).num("decay", 0, 100, 50, "%", num("d", 3), 0.01, 1)
        .in("hit", "tap or notes", Any, "d", 0).out("sound", "sound", Sound, "d", 0).widget("blink", "d", 10).patch("obj d 0 0 drum~ kick");
    B("sound.beat", SO, "Beat maker", "Every {time}: {kick} kicks, {snare} snares, {hat} hats, {clap} claps in 16 steps", "A whole drum machine in one block. Change the numbers: the pattern changes, always in time.", "drum machine beat groove rhythm kick snare hat bateria batida")
        .choice("time", timeOpts(), timeIx("16n"), sym("b", 1)).num("kick", 0, 16, 4, "", num("ek", 1), 1, 1).num("snare", 0, 16, 2, "", num("es", 1), 1, 1).num("hat", 0, 16, 8, "", num("eh", 1), 1, 1).num("clap", 0, 16, 0, "", num("ec", 1), 1, 1)
        .in("onoff", "on / off", Move, "b", 0).out("sound", "sound", Sound, "g", 0).widget("euclid", "ek", 22)
        .patch("obj b 0 0 beat 16n\nobj ek 0 40 euclid 4 16 0\nobj es 80 40 euclid 2 16 4\nobj eh 160 40 euclid 8 16 0\nobj ec 240 40 euclid 0 16 4\n"
               "obj dk 0 80 drum~ kick\nobj ds 80 80 drum~ snare\nobj dh 160 80 drum~ hat 0.6 0.3\nobj dc 240 80 drum~ clap\nobj g 0 140 *~ 0.8\n"
               "- b 1 ek 0; b 1 es 0; b 1 eh 0; b 1 ec 0; ek 0 dk 0; es 0 ds 0; eh 0 dh 0; ec 0 dc 0; dk 0 g 0; ds 0 g 0; dh 0 g 0; dc 0 g 0");
    B("sound.pluck", SO, "Plucked string", "Plays the notes as a plucked string, ringing {ring}", "A string you pluck (Karplus-Strong): harps, guitars, kotos.", "pluck string guitar harp karplus corda")
        .num("ring", 50, 99.9, 98, "%", num("c", 4), 0.01, 0.1)
        .in("notes", "notes", Notes, "u", 0).out("sound", "sound", Sound, "c", 0)
        .patch("obj u 0 0 unpack 0 0\nobj sn 0 30 stripnote\nobj m 0 60 mtof\nobj d 0 90 !/ 1000.\nmsg e 120 60 1 1 0 25\nobj l 120 90 line~\nobj n 200 60 noise~\nobj x 120 130 *~\nobj c 0 170 comb~ 60 5 1 0 0.98\n"
               "- u 0 sn 0; u 1 sn 1; sn 0 m 0; m 0 d 0; d 0 c 1; sn 0 e 0; e 0 l 0; l 0 x 0; n 0 x 1; x 0 c 0");

    // ---------------------------------------------------------------- SAMPLES
    B("samples.record", SA, "Record", "Record the sound into {name}, up to {secs}", "Switch it on to record (1), off to stop (0). Then play it with Sample player, Loop or Grain cloud with the same name.", "record sampler capture buffer gravar")
        .text("name", "take1", {}, true).num("secs", 1, 120, 10, "s", {}, 1, 1).baked()
        .in("sound", "sound", Sound, "r", 0).in("onoff", "1 record / 0 stop", Move, "r", 0).widget("wave", "buf", 40)
        .patch("obj buf 0 0 buffer~ {name} {secs}000\nobj r 0 40 record~ {name}");
    B("samples.player", SA, "Sample player", "Each tap plays {name}", "Plays a sound from the beginning. Drop a sound file on the block to load it.", "sample player play one shot sound file amostra")
        .text("name", "take1", {}, true).in("tap", "tap", Move, "m", 0).out("sound", "sound", Sound, "p", 0).widget("wave", "buf", 40)
        .patch("obj buf 0 0 buffer~ {name}\nmsg m 0 40 start\nobj p 0 80 play~ {name}\n- m 0 p 0");
    B("samples.loop", SA, "Loop", "Loop {name} at {speed}", "Plays a sound round and round, at any speed (negative: backwards).", "loop sample varispeed groove speed backwards")
        .text("name", "take1", {}, true).num("speed", -400, 400, 100, "%", num("sp", 0), 0.01, 1)
        .out("sound", "sound", Sound, "g", 0).widget("wave", "buf", 40)
        .patch("obj buf 0 0 buffer~ {name}\nobj sp 0 40 sig~ 1\nobj lp 120 40 loadmess loop 1\nobj g 0 80 groove~ {name}\n- sp 0 g 0; lp 0 g 0");
    B("samples.grains", SA, "Grain cloud", "A cloud of {dens} grains a second from {name}, at {pos}, {size} each, pitch {pitch}, spread {spray}", "Granular: tiny pieces of a sound, into textures and clouds. Move the position slowly.", "granular grains cloud texture ambient freeze")
        .text("name", "take1", {}, true).num("dens", 1, 120, 25, "", num("g", 3), 1, 1).num("pos", 0, 100, 30, "%", num("g", 1), 0.01, 1).num("size", 10, 800, 90, "ms", num("g", 2), 1, 1, true)
        .num("pitch", -24, 24, 0, "semitones", num("g", 4), 1, 1).num("spray", 0, 100, 10, "%", num("g", 5), 0.01, 1)
        .out("sound", "sound", Sound, "g", 0, "g", 1).widget("wave", "buf", 40).tip("A slow wave into the position, and a Room after it.")
        .patch("obj buf 0 0 buffer~ {name}\nobj g 0 40 grain~ {name}");

    // ---------------------------------------------------------------- CHANGE THE SOUND
    B("change.echo", CH, "Echo", "Repeat every {time}, {back} coming back, {mix} heard", "The sound comes back, in time with the music.", "echo delay repeat feedback tape eco")
        .choice("time", timeOpts(), timeIx("8n"), sym2("l", 1, "r", 1)).num("back", 0, 95, 45, "%", num2("l", 2, "r", 2), 0.01, 1).num("mix", 0, 100, 35, "%", num2("l", 3, "r", 3), 0.01, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 echo~ 8n 0.45 0.35\nobj r 120 0 echo~ 8n 0.45 0.35")
        .tip("Plug a slow wave into 'back' - the echo breathes.");
    B("change.room", CH, "Room", "Like {size}, {mix} of it", "Reverb: puts the sound in a space.", "reverb room hall space ambience big sala")
        .choice("size", { { "a small room", "0.45" }, { "a room", "0.7" }, { "a hall", "0.85" }, { "a cathedral", "0.95" } }, 1, num("v", 2)).num("mix", 0, 100, 30, "%", num("v", 4), 0.01, 1)
        .in("sound", "sound", Sound, "v", 0, "v", 1).out("sound", "sound", Sound, "v", 0, "v", 1).patch("obj v 0 0 reverb~ 0.7 0.4 0.3");
    B("change.filter", CH, "Filter", "Keep the {type} around {hz}, {res} sharp", "Takes part of the sound away: darker, thinner, a wah.", "filter lowpass highpass bandpass wah eq tone dark bright filtro")
        .choice("type", { { "lows", "lowpass" }, { "highs", "highpass" }, { "middle", "bandpass" }, { "all but a notch", "notch" } }, 0, num2("l", 0, "r", 0))
        .num("hz", 40, 18000, 1200, "Hz", num2("l", 1, "r", 1), 1, 0, true).num("res", 0.5, 12, 1, "", num2("l", 2, "r", 2), 1, 0.1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 filter~ lowpass 1200 1\nobj r 120 0 filter~ lowpass 1200 1")
        .tip("Plug 'Follow the volume' into the frequency: an auto-wah.");
    B("change.drive", CH, "Distortion", "Drive it {drive} hard", "Warm to dirty: a guitar amp, a broken radio.", "distortion drive overdrive fuzz dirty saturate distorcao")
        .num("drive", 1, 30, 4, "x", num2("l", 1, "r", 1), 1, 0.5).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "gl", 0, "gr", 0)
        .patch("obj l 0 0 overdrive~ 4\nobj r 120 0 overdrive~ 4\nobj gl 0 40 *~ 0.7\nobj gr 120 40 *~ 0.7\n- l 0 gl 0; r 0 gr 0");
    B("change.lofi", CH, "Lo-fi", "Crush it to {rate} of the samples and {bits} bits", "Old samplers, video games, broken toys.", "lofi bitcrush crush retro 8bit")
        .num("rate", 2, 100, 30, "%", num2("l", 1, "r", 1), 0.01, 1).num("bits", 2, 24, 8, "", num2("l", 2, "r", 2), 1, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 degrade~ 0.3 8\nobj r 120 0 degrade~ 0.3 8");
    B("change.chorus", CH, "Chorus", "Thicken it, {rate} slow, {depth} deep, {mix} of it", "Many voices from one: wide and shimmering.", "chorus wide thick ensemble shimmer coro")
        .num("rate", 0.05, 5, 0.6, "Hz", num("c", 1), 1, 0, true).num("depth", 0, 100, 50, "%", num("c", 2), 0.01, 1).num("mix", 0, 100, 50, "%", num("c", 3), 0.01, 1)
        .in("sound", "sound", Sound, "c", 0, "c", 0).out("sound", "sound", Sound, "c", 0, "c", 1).patch("obj c 0 0 chorus~ 0.6 0.5 0.5");
    B("change.flanger", CH, "Flanger", "A jet sweep, {rate} slow, {depth} deep", "The whoosh of a jet.", "flanger jet sweep whoosh")
        .num("rate", 0.02, 5, 0.25, "Hz", num2("l", 1, "r", 1), 1, 0, true).num("depth", 0, 100, 70, "%", num2("l", 2, "r", 2), 0.01, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 flanger~ 0.25 0.7 0.5\nobj r 120 0 flanger~ 0.27 0.7 0.5");
    B("change.phaser", CH, "Phaser", "A swirl, {rate} slow, {depth} deep", "A swirling, liquid sweep.", "phaser swirl sweep liquid")
        .num("rate", 0.02, 5, 0.3, "Hz", num2("l", 1, "r", 1), 1, 0, true).num("depth", 0, 100, 80, "%", num2("l", 2, "r", 2), 0.01, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 phaser~ 0.3 0.8 0.4\nobj r 120 0 phaser~ 0.32 0.8 0.4");
    B("change.pitch", CH, "Higher / lower", "Move the pitch {st}", "Higher or lower without changing the speed: harmonies, chipmunks, giants.", "pitch shift harmony octave transpose higher lower")
        .num("st", -24, 24, 7, "semitones", num2("l", 1, "r", 1), 1, 1).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 pitchshift~ 7\nobj r 120 0 pitchshift~ 7");
    B("change.shift", CH, "Frequency shift", "Shift every frequency by {hz}", "Metallic, bell-like, strange - not in tune on purpose.", "frequency shift metallic bell strange")
        .num("hz", -500, 500, 40, "Hz", num2("l", 1, "r", 1), 1, 1).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 freqshift~ 40\nobj r 120 0 freqshift~ 40");
    B("change.even", CH, "Even out", "Squeeze it above {th} by {ratio}", "A compressor: quiet parts up, loud parts down - punchier and steadier.", "compressor compress dynamics punch level squeeze")
        .num("th", -50, 0, -18, "dB", num("c", 2), 1, 1).num("ratio", 1, 20, 4, ": 1", num("c", 3), 1, 0.5)
        .in("sound", "sound", Sound, "c", 0, "c", 1).out("sound", "sound", Sound, "c", 0, "c", 1).patch("obj c 0 0 compressor~ -18 4 10 150");
    B("change.limit", CH, "Never too loud", "Never louder than {db}", "A limiter: protects your ears and the speakers.", "limiter limit protect safe ears loud")
        .num("db", -24, 0, -1, "dB", num("c", 2), 1, 0.5).in("sound", "sound", Sound, "c", 0, "c", 1).out("sound", "sound", Sound, "c", 0, "c", 1).patch("obj c 0 0 limiter~ -1");
    B("change.volume", CH, "Volume", "Set the volume to {db}", "Louder or softer, smoothly.", "volume gain level loud quiet")
        .num("db", -60, 12, 0, "dB", num2("l", 1, "r", 1), 1, 0.5).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 volume~ 0\nobj r 120 0 volume~ 0");
    B("change.pan", CH, "Left / right", "Place it at {pos} (0 left, 100 right)", "Where the sound sits between the speakers. A slow wave makes it travel.", "pan stereo left right place")
        .num("pos", 0, 100, 50, "", num("p", 1), 0.01, 1).in("sound", "sound", Sound, "p", 0, "p", 0).out("sound", "sound", Sound, "p", 0, "p", 1).patch("obj p 0 0 pan~ 0.5");
    B("change.tremolo", CH, "Tremolo", "The volume pulses every {rate}, {depth} deep", "The volume goes up and down in time.", "tremolo pulse volume wobble")
        .choice("rate", timeOpts(), timeIx("8n"), sym2("l", 1, "r", 1)).num("depth", 0, 100, 60, "%", num2("l", 2, "r", 2), 0.01, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 tremolo~ 8n 0.6\nobj r 120 0 tremolo~ 8n 0.6");
    B("change.gate", CH, "Quiet the hiss", "Silence everything under {th}", "A noise gate: the hiss between notes disappears.", "gate noise hiss silence clean")
        .num("th", -80, -10, -45, "dB", num2("l", 1, "r", 1), 1, 1).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 noisegate~ -45\nobj r 120 0 noisegate~ -45");
    B("change.ring", CH, "Ring modulation", "Multiply it by {hz}, {mix} of it", "Bells and robots.", "ring modulation robot bell metallic dalek")
        .num("hz", 1, 3000, 220, "Hz", num2("l", 1, "r", 1), 1, 0, true).num("mix", 0, 100, 100, "%", num2("l", 2, "r", 2), 0.01, 1)
        .in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "l", 0, "r", 0).patch("obj l 0 0 ringmod~ 220 1\nobj r 120 0 ringmod~ 220 1");
    B("change.wide", CH, "Wider", "Make it {amt} as wide", "Spreads a stereo sound between the speakers (0 is mono).", "stereo wide width spread imager")
        .num("amt", 0, 300, 140, "%", num("w", 2), 0.01, 1).in("sound", "sound", Sound, "w", 0, "w", 1).out("sound", "sound", Sound, "w", 0, "w", 1).patch("obj w 0 0 widen~ 1.4");
    B("change.fold", CH, "Wavefold", "Fold it {drive} times", "Folds the wave back on itself: rich, glassy, wild.", "wavefold fold distortion west coast")
        .num("drive", 1, 12, 3, "x", num2("l", 1, "r", 1), 1, 0.1).in("sound", "sound", Sound, "l", 0, "r", 0).out("sound", "sound", Sound, "fl", 0, "fr", 0)
        .patch("obj l 0 0 *~ 3\nobj r 120 0 *~ 3\nobj fl 0 40 fold~\nobj fr 120 40 fold~\n- l 0 fl 0; r 0 fr 0");
    B("change.mix", CH, "Mix two", "{a} of the first and {b} of the second", "Two sounds into one.", "mix mixer blend two together juntar")
        .num("a", 0, 100, 70, "%", num2("al", 1, "ar", 1), 0.01, 1).num("b", 0, 100, 70, "%", num2("bl", 1, "br", 1), 0.01, 1)
        .in("first", "first", Sound, "al", 0, "ar", 0).in("second", "second", Sound, "bl", 0, "br", 0).out("sound", "sound", Sound, "ol", 0, "or", 0)
        .patch("obj al 0 0 *~ 0.7\nobj ar 60 0 *~ 0.7\nobj bl 120 0 *~ 0.7\nobj br 180 0 *~ 0.7\nobj ol 0 40 +~\nobj or 120 40 +~\n- al 0 ol 0; bl 0 ol 0; ar 0 or 0; br 0 or 0");

    // ---------------------------------------------------------------- MOVE BY ITSELF
    B("move.wave", MO, "Slow wave", "A {shape} wave every {rate}, between {lo} and {hi}", "Moves any word of any block by itself: plug its cable into a word.", "lfo wave slow move modulate wobble sweep onda")
        .choice("shape", { { "smooth", "sine" }, { "triangle", "triangle" }, { "rising", "saw" }, { "falling", "down" }, { "on / off", "square" }, { "random steps", "random" }, { "wandering", "smooth" } }, 0, sym("l", 2))
        .choice("rate", timeOpts(), timeIx("1n"), sym("l", 1)).num("lo", 0, 100, 0, "%", num("s", 3), 0.01, 1).num("hi", 0, 100, 100, "%", num("s", 4), 0.01, 1)
        .in("onoff", "on / off", Move, "l", 0).out("move", "0 to 1", Move, "s", 0).widget("move", "v", 10).tip("Plug it into any coloured word of any block.")
        .patch("obj l 0 0 lfo sine 1n\nobj s 0 40 scale 0. 1. 0. 1.\nobj v 0 80 flonum\n- l 0 s 0; s 0 v 0");
    B("move.follow", MO, "Follow the volume", "How loud it is, {gain} sensitive", "The louder you play, the more it moves.", "envelope follower level loudness dynamics follow")
        .num("gain", 10, 400, 100, "%", num("f", 1), 0.01, 1).in("sound", "sound", Sound, "f", 0, "f", 0).out("move", "0 to 1", Move, "f", 0).widget("move", "f", 10).patch("obj f 0 0 follow~ 1");
    B("move.swell", MO, "Swell", "Each tap rises in {up} and falls in {down}", "An envelope: a shape for each tap.", "envelope swell attack release shape")
        .num("up", 1, 4000, 20, "ms", {}, 1, 1, true).baked().num("down", 5, 8000, 600, "ms", {}, 1, 1, true).baked()
        .in("tap", "tap", Move, "m", 0).out("move", "0 to 1", Move, "s", 0).widget("move", "s", 10)
        .patch("msg m 0 0 1 {up} 0 {down}\nobj l 0 40 line~\nobj s 0 80 snapshot~ 15\n- m 0 l 0; l 0 s 0");
    B("move.glide", MO, "Glide", "Glide to each new value in {ms}", "Smooths jumps into slides.", "glide smooth ramp portamento slew")
        .num("ms", 1, 5000, 200, "ms", num("l", 1), 1, 1, true).in("move", "a value", Move, "l", 0).out("move", "gliding", Move, "l", 0).widget("move", "v", 10).patch("obj l 0 0 line 0. 15\nobj v 0 40 flonum\n- l 0 v 0");
    B("move.squeeze", MO, "Between", "Squeeze it between {lo} and {hi}", "Makes a movement smaller, or turns it upside down (low above high).", "map range scale squeeze invert")
        .num("lo", 0, 100, 20, "%", num("s", 3), 0.01, 1).num("hi", 0, 100, 80, "%", num("s", 4), 0.01, 1).in("move", "0 to 1", Move, "s", 0).out("move", "0 to 1", Move, "s", 0).widget("move", "v", 10).patch("obj s 0 0 scale 0. 1. 0.2 0.8\nobj v 0 40 flonum\n- s 0 v 0");
    B("move.dice", MO, "Dice", "On each tap, a random value", "A new random value 0 to 1 for each tap.", "random value dice chance number dado")
        .in("tap", "tap", Move, "r", 0).out("move", "0 to 1", Move, "d", 0).widget("move", "v", 10).patch("obj r 0 0 random 1000\nobj d 0 40 / 999.\nobj v 0 80 flonum\n- r 0 d 0; d 0 v 0");

    // ---------------------------------------------------------------- NUMBERS & LOGIC
    B("logic.formula", LO, "Formula", "Work out {expr}", "Math as you write it: $f1 is what comes into the first way in, $f2 the second.", "formula math expression calculate expr")
        .text("expr", "$f1 * 2", {}, true).in("a", "first", Any, "e", 0).in("b", "second", Any, "e", 1).out("out", "result", Any, "e", 0).patch("obj e 0 0 expr {expr}");
    B("logic.above", LO, "When it goes above", "A tap when it goes above {lvl}", "Turns a movement into a tap: when the voice gets loud, when the wave goes high.", "threshold above trigger past level")
        .num("lvl", 0, 100, 50, "%", num("p", 1), 0.01, 1).in("move", "0 to 1", Move, "p", 0).out("tap", "tap", Move, "p", 0).patch("obj p 0 0 past 0.5");
    B("logic.is", LO, "When it is", "A tap when it is {what}", "Listens for one number or word: a tap left when it comes, the rest go right.", "select equal match is")
        .text("what", "0", {}, true).in("in", "anything", Any, "s", 0).out("tap", "tap", Move, "s", 0).out("rest", "the rest", Any, "s", 1).patch("obj s 0 0 sel {what}");
    B("logic.flip", LO, "Flip", "Each tap flips between 0 and 1", "Turns taps into on, off, on, off.", "flip toggle alternate")
        .in("tap", "tap", Move, "c", 0).out("move", "1 / 0", Move, "c", 0).patch("obj c 0 0 counter 0 1");
    B("logic.changes", LO, "Only changes", "Only when it changes", "Lets a number through only when it is different from the last.", "change filter repeat")
        .in("in", "number", Any, "c", 0).out("out", "number", Any, "c", 0).patch("obj c 0 0 change");
    B("logic.route", LO, "Send to one of four", "Send it to way {which}", "A switch for messages: 0 nowhere, 1 to 4 one of the ways out.", "gate route switch choose direct")
        .num("which", 0, 4, 1, "", num("g", 0), 1, 1).in("in", "anything", Any, "g", 1).out("1", "1", Any, "g", 0).out("2", "2", Any, "g", 1).out("3", "3", Any, "g", 2).out("4", "4", Any, "g", 3).patch("obj g 0 0 gate 4 1");
    B("logic.keep", LO, "Remember", "Keep the last number; a tap says it again", "A memory for one number.", "remember store keep hold memory")
        .in("in", "number", Any, "f", 1).in("tap", "tap: say it", Move, "f", 0).out("out", "number", Any, "f", 0).patch("obj f 0 0 float");
    B("logic.many", LO, "One into three", "One tap becomes three, right to left", "One message, three ways out - in order (right first).", "trigger bangbang order split")
        .in("in", "anything", Any, "t", 0).out("1", "1", Any, "t", 0).out("2", "2", Any, "t", 1).out("3", "3", Any, "t", 2).patch("obj t 0 0 t a a a");
    B("logic.send", LO, "Send (no cable)", "Send it to {name}", "Sends without a cable to every Receive with the same name.", "send wireless bus name")
        .text("name", "a", {}, true).in("in", "anything", Any, "s", 0).patch("obj s 0 0 send {name}");
    B("logic.receive", LO, "Receive (no cable)", "Receive what is sent to {name}", "Hears every Send with the same name.", "receive wireless bus name")
        .text("name", "a", {}, true).out("out", "anything", Any, "r", 0).patch("obj r 0 0 receive {name}");

    // ---------------------------------------------------------------- WATCH
    B("watch.scope", WA, "Oscilloscope", "Look at the wave", "The shape of the sound, live.", "scope oscilloscope wave see look shape")
        .in("sound", "sound", Sound, "s", 0, "s", 0).widget("scope", "s", 80).patch("obj s 0 0 scope~");
    B("watch.spectrum", WA, "Spectrum", "Look at the frequencies", "Low on the left, high on the right.", "spectrum frequencies analyzer fft see")
        .in("sound", "sound", Sound, "s", 0, "s", 0).widget("spectrum", "s", 80).patch("obj s 0 0 spectroscope~");
    B("watch.number", WA, "Show the number", "Show what comes in", "Shows a number as it changes.", "show number display value see")
        .in("in", "number", Any, "n", 0).widget("number", "n", 30).patch("obj n 0 0 flonum");
    B("watch.console", WA, "Write it down", "Write what comes in, as {name}", "Writes everything that comes in at the bottom of the screen (the console).", "print console log debug write")
        .text("name", "here", {}, true).in("in", "anything", Any, "p", 0).patch("obj p 0 0 print {name}");

    // ---------------------------------------------------------------- OUT
    B("out.speakers", OU, "Speakers", "Hear it, {db}", "The way out to the mix (choose at the top where FLUXO plays: on its own or into LIVE 2-4).", "out speakers hear output play sound saida colunas")
        .num("db", -60, 6, -6, "dB", num2("l", 1, "r", 1), 1, 0.5).in("sound", "sound", Sound, "l", 0, "r", 0).widget("level", "m", 10)
        .patch("obj l 0 0 volume~ -6\nobj r 120 0 volume~ -6\nobj d 0 60 dac~ 1 2\nobj m 200 60 follow~ 1\n- l 0 d 0; r 0 d 1; l 0 m 0");
    B("out.instrument", OU, "ARCO's instrument", "Play the notes on ARCO's keyboard sound", "Plays the KEYBOARD's instrument (a modular patch, a sampled piano, a plug-in) or the SYNTH.", "instrument keyboard plugin play notes out")
        .in("notes", "notes", Notes, "u", 0).patch("obj u 0 0 unpack 0 0\nobj n 0 40 noteout\n- u 0 n 0; u 1 n 1");
    B("out.cc", OU, "MIDI knob out", "Turn MIDI knob {cc}", "Sends a movement as a MIDI controller.", "midi cc out controller send")
        .num("cc", 0, 127, 1, "", {}, 1, 1).baked().in("move", "0 to 1", Move, "m", 0).patch("obj m 0 0 * 127.\nobj r 0 40 round\nobj c 0 80 ctlout {cc}\n- m 0 r 0; r 0 c 0");

    // ---------------------------------------------------------------- WAYS IN & OUT (plug-ins)
    auto port = [&](const char* id, const char* name, const char* sentence, Kind k, bool in) -> BlockDef& {
        auto& d = B(id, PO, name, sentence, in ? "A way in of your plug-in (as an effect in STUDIO: the track's sound or notes)." : "A way out of your plug-in (as an effect in STUDIO: what goes on to the track).", "plugin port in out");
        d.port = in ? "in" : "out"; if (in) d.out("o", kindName(k), k, "#", 0, k == Sound ? "#" : "", 1); else d.in("i", kindName(k), k, "#", 0, k == Sound ? "#" : "", 1); return d; };
    port("port.sound.in", "Sound in", "Sound comes in here", Sound, true); port("port.sound.out", "Sound out", "Sound goes out here", Sound, false);
    port("port.notes.in", "Notes in", "Notes come in here", Notes, true); port("port.notes.out", "Notes out", "Notes go out here", Notes, false);
    port("port.move.in", "Movement in", "A movement comes in here", Move, true); port("port.move.out", "Movement out", "A movement goes out here", Move, false);

    // ---------------------------------------------------------------- MAX (every object of the PATCHER)
    B("max.box", MX, "Max box", "Any Max object: type it", "Any of the PATCHER's 290 objects (metro, cycle~, zl, coll, expr ...) as a block. Its ways in and out are its inlets and outlets.", "max msp object box patcher advanced");
    B("max.patch", MX, "Classic patch", "A whole classic patch", "A PATCHER patch inside one block: double-click to open it in the PATCHER.", "max msp patch classic patcher subpatch");
    return L;
}
const std::vector<BlockDef>& library() { static const std::vector<BlockDef> L = build(); return L; }
const BlockDef* findBlock(const std::string& id) { for (auto& b : library()) if (b.id == id) return &b; return nullptr; }
std::vector<std::string> shelves() { std::vector<std::string> s; for (auto& b : library()) if (std::find(s.begin(), s.end(), b.shelf) == s.end()) s.push_back(b.shelf); return s; }

// ================================================================== recipes: "write what you want"
const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> R = [] {
        std::vector<Recipe> v; auto add = [&](const char* n, const char* says, const char* words, const char* t) { v.push_back({ juce::String::fromUTF8(n), juce::String::fromUTF8(says), juce::String::fromUTF8(words), t }); };
        add("Echo on your voice", "The microphone, an echo in time, a room - out to the speakers.", "echo voice mic microphone delay eco voz microfone",
            "b 1 listen.input 0 0\nb 2 change.echo 0 120\nb 3 change.room 0 260\nb 4 out.speakers 0 380\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("Echo that breathes", "An echo whose repeats come and go with a slow wave.", "echo breathing moving lfo modulated delay",
            "b 1 listen.input 0 0\nb 2 change.echo 0 120 back=60\nb 3 move.wave 340 120 rate=2bar lo=10 hi=85\nb 4 out.speakers 0 260\nw 1 0 2 0\nw 3 0 2 back\nw 2 0 4 0");
        add("Arpeggio in A minor", "Hold a chord: an arpeggio goes up, every 16th, on a warm synth.", "arpeggiator arpeggio chord synth arp arpejo arpejador minor",
            "b 1 listen.keys 0 0\nb 2 notes.key 0 110 scale=minor root=A\nb 3 notes.arp 0 220 mode=updown oct=2\nb 4 sound.synth 0 360\nb 5 change.echo 0 480 mix=25\nb 6 out.speakers 0 600\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0\nw 5 0 6 0");
        add("Chords that play themselves", "No hands: every bar a new minor 7th chord, played as an arpeggio.", "arpeggio automatic chords progression generative self playing sozinho acordes",
            "b 1 time.every 0 0 time=1n\nb 2 notes.random 0 110 low=45 high=57 scale=minor root=A len=2n\nb 3 notes.chord 0 240 type=min7\nb 4 notes.arp 0 350 mode=updown oct=2 latch=1\nb 5 sound.synth 0 500 wave=triangle\nb 6 change.room 0 620\nb 7 out.speakers 0 740\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0\nw 5 0 6 0\nw 6 0 7 0");
        add("Generative melody", "Random notes of a scale, sometimes, in time - a melody that never repeats.", "generative random melody ambient endless never repeats aleatorio melodia generativa",
            "b 1 time.every 0 0 time=8n\nb 2 time.sometimes 0 110 p=60\nb 3 notes.random 0 220 scale=pentatonic root=D\nb 4 notes.echo 0 350 count=2\nb 5 sound.synth 0 480 wave=sine bright=40 rel=900\nb 6 change.room 0 600 size=a_hall mix=45\nb 7 out.speakers 0 720\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0\nw 5 0 6 0\nw 6 0 7 0");
        add("Drum machine", "Kicks, snares and hats in 16 steps - change the numbers.", "drums beat drum machine groove rhythm bateria batida ritmo",
            "b 1 sound.beat 0 0\nb 2 change.even 0 140\nb 3 out.speakers 0 260\nw 1 0 2 0\nw 2 0 3 0");
        add("Euclidean percussion", "Three drums, three rhythms spread evenly - polyrhythms from Africa, Cuba, the Balkans.", "euclidean polyrhythm percussion tresillo world rhythm",
            "b 1 time.rhythm 0 0 hits=3 steps=8\nb 2 time.rhythm 300 0 hits=5 steps=16 rot=2\nb 3 time.rhythm 600 0 hits=7 steps=12\nb 4 sound.drum 0 140 kind=kick\nb 5 sound.drum 300 140 kind=rim\nb 6 sound.drum 600 140 kind=hat\nb 7 change.mix 0 280\nb 8 out.speakers 0 400\nw 1 0 4 0\nw 2 0 5 0\nw 3 0 6 0\nw 4 0 7 0\nw 5 0 7 1\nw 6 0 8 0\nw 7 0 8 0");
        add("Auto-wah", "The louder you play, the more the filter opens.", "auto wah envelope filter funk guitar",
            "b 1 listen.input 0 0\nb 2 change.filter 0 140 type=lows res=5\nb 3 move.follow 340 140 gain=150\nb 4 out.speakers 0 280\nw 1 0 2 0\nw 1 0 3 0\nw 3 0 2 hz\nw 2 0 4 0");
        add("Sing and a synth follows", "Your voice becomes notes: a synth sings with you, a fifth up.", "pitch follow voice sing harmony synth listen cantar voz",
            "b 1 listen.input 0 0\nb 2 listen.pitch 0 120\nb 3 notes.transpose 0 260 by=7\nb 4 sound.synth 0 380 wave=soft\nb 5 out.speakers 0 500\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0");
        add("Harmony voice", "Your voice and the same voice a fifth higher.", "harmony pitch shift voice choir fifth harmonia",
            "b 1 listen.input 0 0\nb 2 change.pitch 340 120 st=7\nb 3 change.mix 0 260\nb 4 change.room 0 380\nb 5 out.speakers 0 500\nw 1 0 3 0\nw 1 0 2 0\nw 2 0 3 1\nw 3 0 4 0\nw 4 0 5 0");
        add("Grain cloud", "Record a few seconds, then turn them into a slowly moving cloud.", "granular ambient texture cloud pad freeze grains",
            "b 1 listen.input 0 0\nb 2 touch.switch 340 0\nb 3 samples.record 0 120\nb 4 samples.grains 0 260\nb 5 move.wave 340 260 rate=4bar shape=smooth lo=10 hi=90\nb 6 change.room 0 420 size=a_cathedral mix=50\nb 7 out.speakers 0 540\nw 1 0 3 0\nw 2 0 3 1\nw 5 0 4 pos\nw 4 0 6 0\nw 6 0 7 0");
        add("Looper", "Record with the switch; it plays back in a loop.", "looper loop record overdub play back",
            "b 1 listen.input 0 0\nb 2 touch.switch 340 0\nb 3 samples.record 0 120 secs=8\nb 4 samples.loop 0 260\nb 5 out.speakers 0 400\nw 1 0 3 0\nw 2 0 3 1\nw 4 0 5 0\nw 1 0 5 0");
        add("Ambient pad", "A soft pad that changes colour by itself.", "ambient pad drone texture slow evolving",
            "b 1 listen.keys 0 0\nb 2 notes.chord 0 110 type=add9\nb 3 sound.synth 0 220 wave=soft atk=900 rel=3000 bright=35\nb 4 move.wave 340 220 rate=8bar lo=20 hi=70\nb 5 change.chorus 0 360\nb 6 change.room 0 480 size=a_cathedral mix=55\nb 7 out.speakers 0 600\nw 1 0 2 0\nw 2 0 3 0\nw 4 0 3 bright\nw 3 0 5 0\nw 5 0 6 0\nw 6 0 7 0");
        add("Bass line", "A random bass line in time, low and round.", "bass line bassline groove low baixo",
            "b 1 time.rhythm 0 0 hits=5 steps=16\nb 2 notes.random 0 140 low=33 high=45 len=16n\nb 3 sound.synth 0 280 wave=square bright=30 rel=120\nb 4 out.speakers 0 400\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("Step melody", "Draw a melody in 8 steps.", "melody steps sequencer draw tune",
            "b 1 notes.melody 0 0\nb 2 sound.synth 0 170 wave=triangle\nb 3 change.echo 0 290 mix=25\nb 4 out.speakers 0 410\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("XY performance", "One finger moves the filter and the echo at once.", "xy pad performance live control filter echo",
            "b 1 listen.input 0 0\nb 2 touch.xy 340 0\nb 3 change.filter 0 160 res=4\nb 4 change.echo 0 300\nb 5 out.speakers 0 440\nw 1 0 3 0\nw 2 0 3 hz\nw 2 1 4 back\nw 3 0 4 0\nw 4 0 5 0");
        add("Lo-fi radio", "Crushed, filtered, a little wobbly.", "lofi radio old vintage crush telephone",
            "b 1 listen.input 0 0\nb 2 change.lofi 0 120\nb 3 change.filter 0 240 type=middle hz=1500 res=2\nb 4 change.tremolo 0 360 rate=1/16 depth=15\nb 5 out.speakers 0 480\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0");
        add("Robot voice", "Ring modulation and a little distortion.", "robot voice ring modulation dalek alien",
            "b 1 listen.input 0 0\nb 2 change.ring 0 120 hz=60\nb 3 change.drive 0 240 drive=3\nb 4 out.speakers 0 360\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("Tremolo in time", "The volume pulses with the tempo.", "tremolo pulse rhythm volume",
            "b 1 listen.input 0 0\nb 2 change.tremolo 0 120\nb 3 out.speakers 0 240\nw 1 0 2 0\nw 2 0 3 0");
        add("Sweeping filter", "A slow wave opens and closes a filter.", "filter sweep lfo moving wah",
            "b 1 listen.input 0 0\nb 2 change.filter 0 140 res=5\nb 3 move.wave 340 0 rate=2bar\nb 4 out.speakers 0 280\nw 1 0 2 0\nw 3 0 2 hz\nw 2 0 4 0");
        add("Travelling sound", "The sound travels from left to right and back.", "pan auto pan travel stereo move",
            "b 1 listen.input 0 0\nb 2 change.pan 0 140\nb 3 move.wave 340 0 rate=1n\nb 4 out.speakers 0 280\nw 1 0 2 0\nw 3 0 2 pos\nw 2 0 4 0");
        add("Plucked harp", "Your notes as a harp, with a room.", "harp pluck string guitar koto",
            "b 1 listen.keys 0 0\nb 2 sound.pluck 0 110\nb 3 change.room 0 230 size=a_hall\nb 4 out.speakers 0 350\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("Note echo cascade", "Each note comes back an octave up, softer.", "note echo cascade repeat octave",
            "b 1 listen.keys 0 0\nb 2 notes.echo 0 110 count=4 fade=70\nb 3 sound.synth 0 230 wave=sine rel=600\nb 4 change.room 0 350\nb 5 out.speakers 0 470\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0");
        add("Watch your sound", "See the wave and the frequencies of the microphone.", "see watch scope spectrum analyse look",
            "b 1 listen.input 0 0\nb 2 watch.scope 0 140\nb 3 watch.spectrum 340 140\nw 1 0 2 0\nw 1 0 3 0");
        add("Drum on loud notes", "When you play loud, a drum hits.", "trigger drum loud threshold accent",
            "b 1 listen.input 0 0\nb 2 move.follow 0 120 gain=200\nb 3 logic.above 0 230 lvl=60\nb 4 sound.drum 0 340 kind=clap\nb 5 out.speakers 0 460\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0");
        add("Two hands", "Bass on the left of the keyboard, a lead on the right.", "split keyboard bass lead two hands zones",
            "b 1 listen.keys 0 0\nb 2 notes.split 0 110\nb 3 sound.synth 0 240 wave=square bright=25\nb 4 sound.synth 340 240 wave=saw bright=70\nb 5 change.mix 0 380\nb 6 out.speakers 0 500\nw 1 0 2 0\nw 2 0 3 0\nw 2 1 4 0\nw 3 0 5 0\nw 4 0 5 1\nw 5 0 6 0");
        add("Knob controls the echo", "A knob on the screen sets how much echo comes back.", "knob control echo hand",
            "b 1 listen.input 0 0\nb 2 touch.knob 340 0\nb 3 change.echo 0 140\nb 4 out.speakers 0 280\nw 1 0 3 0\nw 2 0 3 back\nw 3 0 4 0");
        add("Random rhythm on a drum", "Taps at random moments on a hat.", "random rhythm hats glitch probability",
            "b 1 time.every 0 0 time=16n\nb 2 time.sometimes 0 110 p=40\nb 3 sound.drum 0 220 kind=hat\nb 4 out.speakers 0 340\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        add("Distorted guitar", "Drive, a filter to tame it, a room.", "guitar distortion amp rock fuzz",
            "b 1 listen.input 0 0\nb 2 change.drive 0 120 drive=12\nb 3 change.filter 0 240 hz=4000\nb 4 change.room 0 360 size=a_small_room\nb 5 out.speakers 0 480\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0\nw 4 0 5 0");
        add("Effect for a STUDIO track", "Sound in, echo and room, sound out - make it a plug-in and put it on a track.", "plugin effect studio track insert device",
            "b 1 port.sound.in 0 0\nb 2 change.echo 0 120\nb 3 change.room 0 240\nb 4 port.sound.out 0 360\nw 1 0 2 0\nw 2 0 3 0\nw 3 0 4 0");
        return v;
    }();
    return R;
}

}} // namespace arco::fx
