#include "HelpText.h"
namespace arco {
const std::vector<HelpSection>& helpSections() {
    static const std::vector<HelpSection> s = {
        { "Start here", R"(# Welcome to ARCO
A live looping station for your instrument: record loops on the beat, build scenes, add effects, play the synth - and mix and finish the piece in STUDIO.

## The first minute
- Open SETUP (top right) and choose your sound card: OUTPUT for the speakers or headphones, INPUT for your microphone or interface. Press TEST: you hear a soft A.
- Play something: the meter on LIVE 1 moves.
- Press REC on TRACK 1 (or the right arrow key / your pedal). ARCO counts in, records, and the loop plays when you press REC again.
- Record more tracks on top. Every loop lands exactly on the bar.

## The window
- Top: transport (PLAY, STOP, UNDO), the tempo, the song, the show recorder, MASTER, and the windows: SYNTH, STUDIO, KEYS, SETUP.
- SCENES A - D: four sets of loops. Switch on the bar line. CUE listens to a scene in your headphones before the audience hears it.
- STRIPS: LIVE 1-4 (your inputs) and TRACK 1-8. Each has its fader, pan, mute, solo, CUE and ROOM.
- EFFECT CARDS: choose the strip at the top (SEND), then turn its SEND on any card.

## Where things are
- The menu: click ARCO (top left) on iPad; on the Mac it is also in the menu bar - File, Edit, View, Help.
- Help is always here: F1.)" },
        { "Looping", R"(# Recording loops
## Tracks
- REC: the first loop sets the length (and the tempo, if nothing is recorded yet). Press REC again to close it.
- REC on a playing track records over it (overdub). UNDO takes back the last recording.
- ▶ ■ starts / stops one track on the next bar. CLR empties it. 1x / ½x / 2x / ◀ plays it slower, faster or backwards.
- IN chooses which LIVE input a track records.

## Scenes
- A scene is a set of up to 8 tracks with their own faders and effect sends.
- Press another scene: it changes on the bar line. NEXT SCENE (↑ key or pedal) goes to the next one with loops.
- TRACKS sets how many tracks the scene shows.

## Count-in, click, practice
- COUNT: one bar of count-in before recording. CLICK: the click in the headphones (and the speakers if you want).
- PRACTICE (SETUP): the loops slow down, the pitch stays; recording waits until you are back at 100 %.)" },
        { "Effects", R"(# Effects
## Choosing them
- EFFECTS ⚙ (or SETUP → LOOK & EFFECTS): choose which cards are on the main window, and how many. More than 18 = pages (‹ ›).
- New in 1.2: FLANGER, PING-PONG, AUTO-WAH, OCTAVER, TAPE, FILTER, RING MOD, STUTTER, ROTARY, CRUSH - hidden until you choose them.

## Using them
- Choose a strip (click its name), then turn SEND on a card: that strip goes into the effect.
- Inserts (EQ, DRIVE, TREMOLO, PHASER, SWELL) change the strip itself; the AMOUNT knob is per strip.
- Times follow the tempo: DELAY, REVERSE, TREMOLO, PING-PONG, FILTER, STUTTER and the in-tempo half of FLANGER.
- HALO turns notes into chords, SHADOW sings intervals, RESONANCE adds strings, GRAIN a cloud, FREEZE holds the sound, SPACE is the big reverb, ORBIT moves sound over your speakers.

## New in 1.3
- PEDAL: like holding a piano's sustain pedal - 60 strings ring with what you play. Press PEDAL on the card (or a MIDI sustain pedal); AUTO lifts and presses it by itself.
- FEEDBACK: one note feeds itself and grows, never louder than safe. CATCH: it blooms and is cut before it gets loud. LISTEN: it waits, picks up a note and brings it up. HOLD: it grows and stays. LET GO releases it.
- Your own settings: the ▾ next to a card's name saves the knobs as a preset, opens them again, or makes them the start of every new song.)" },
        { "AUTO MIX", R"(# AUTO MIX
ARCO listens to every part, balances them like a mixing engineer and masters the piece. Say what you want - or just press MIX IT.

## What do you want?
- Type it in your words: "warm and spacious", "every voice together", "track 2 louder", "the bass a bit quieter", "less reverb", "punchy", "natural", "brighter", "wider", "tight timing". English, Portuguese or German. What it understood is shown under the box.
- Or choose: BALANCE (EVERY VOICE TOGETHER, MELODY IN FRONT, GROOVE), SOUND (NATURAL, BALANCED, PUNCHY), SPACE (DRY, ONE ROOM, HALL), TIMING (AS PLAYED, GENTLE, TIGHT).
- An instrument by name ("the cello louder"): ARCO asks which track it is - say "track 2 louder".

## How it balances
- It measures how loud each part sounds WHILE IT PLAYS (not counting its rests), after its EQ and compressor, so a melody with rests is not pushed up and long notes are not pushed down.
- EVERY VOICE TOGETHER: all parts within a few dB, the melody just in front. MELODY IN FRONT: the melody clearly leads. GROOVE: bass and drums carry.
- A part covered by louder parts in its own register comes up a little. Faders stay between -30 and +6 dB.
- THE BALANCE shows every part before (grey) and after (colour).

## What else it does
- EQ: a high-pass under each part's lowest note; cuts only where parts cover each other; presence for the melody.
- COMPRESSION: gentle, set for each kind of part. PANNING: bass and melody in the centre, the others spread evenly.
- ONE ROOM: every part in the same SPACE, closer or further. DELAY in the melody's rhythm.
- MASTER: tonal balance, then as loud as streaming wants (-14 LUFS; NATURAL -16, PUNCHY -11.5) - but never squashed: if the music would lose its breath, it stays a little quieter.

## Compare and undo
- BEFORE / AFTER at any time; UNDO puts everything back. DETAILS lists what it did to each part, and lets you leave LEVELS, EQ, COMPRESSION ... alone.)" },
        { "STUDIO", R"(# STUDIO
Turn what you played into a song - arrange, record, mix, export. It always plays at the right speed, whatever the sound card's rate.

## The top bar
- ⏮ back to the start, ▶ play, ● record. The display: bar.beat, time, BPM (drag it), beats per bar (tap).
- LOOP, CLICK, COUNT-IN · ↶ ↷ undo / redo · ARRANGE / MIXER · SAVE, EXPORT, ✕.

## Starting
- An empty song shows three big buttons: LAY OUT THE SCENES (everything you played, in order), RECORD THE STATION, IMPORT AUDIO.
- ◧ (top left of the lanes) opens the station drawer: drag a scene or a single loop onto the timeline, or add lanes.

## Lanes
- Each lane: its name, M (mute), S (solo), ● (arm for recording), the volume bar with its meter, and ⋯ for the rest - automation, what it records, hearing the input, rename, duplicate, colour, export a stem.
- REVERB and ECHO are shared by all lanes: turn up REVERB / ECHO in a lane's details.
- Devices on every lane: EQ, COMPRESSOR, SATURATION, DELAY, REVERB, CHORUS, GATE, UTILITY, LIMITER, FILTER (+ DEVICE).
- The details below show the clip, the lane or MASTER. HIDE ▾ gives the lanes the whole screen; DETAILS ▴ brings them back.

## Clips
- Drag to move (snaps - SNAP ▾ chooses to what; Shift is free; Alt-drag copies), drag the edges to trim, the top corners for fades. S splits, ⌘D duplicates, Delete removes. − / + zoom.
- Double-click an instrument lane: a MIDI clip with a piano roll.
- Right-click (or ⋯): reverse, mute, rename, transpose and more.

## Automation
- ⋯ → Automation: volume, pan, sends or any device knob. Click to add a point, drag it, Alt-click removes it.

## Export
- EXPORT: the mix or every lane as stems - WAV or FLAC, up to 96 kHz / 32-bit float. SAVE keeps the arrangement with the song.)" },
        { "SYNTH 2", R"(# SYNTH 2
## Sound
- Four oscillators, each WAVE (sine → saw → square → pulse, WARP bends it), FM, FOLD or SYNC. UNISON and DETUNE make it wide.
- The sounds (◀ ▶) start from ten famous synthesizers: Minimoog, Juno-60, DX7, TB-303, Prophet-5, OB-Xa, CS-80, Jupiter-8, MS-20, Odyssey.
- SUB and NOISE, a FILTER (low / band / high, 12 or 24 dB), VOICE: GLIDE, WIDTH, MONO, FOLLOW (a voice sings what your cello plays).

## Movement
- Three envelopes and three LFOs (free or in tempo).
- Drag a ⟿ dot (LFO, ENV, MACRO) onto any knob: that knob moves. The coloured ring shows how much.
- The MODULATION list shows every connection: drag the bar to change how much, ✕ removes it. Right-click a knob to remove its modulation.
- Four MACROS: one knob that moves several.

## Play
- ARPEGGIATOR: hold a chord - up, down, up-down, random, as played, or chords; 1/4 to 1/32 in the loop's tempo; 1-4 octaves; GATE; HOLD.
- Ways to play: KEYS (piano, computer keys, MIDI), SLIDE PAD (left-right pitch, up-down loudness, gliding), CHORDS (8 pads - SET THE CHORDS: pick any chord, or its exact notes), HARMONY (a grid where neighbours always sound good together - made for fingers on the iPad).
- HOLD: tap notes and they keep sounding - tap more to stack them; tap again to let one go, CLEAR to stop all. With the ARPEGGIATOR, every note you tap is added to the arpeggio.
- PLAYS INTO: the synth sounds through LIVE 2-4, so RECORD ON TRACK records it like any instrument.)" },
        { "Import files", R"(# Importing audio files
- Drop a file (WAV, AIFF, MP3, FLAC, M4A) on a track's screen, or press FILE, or File → Import.
- ARCO listens to it: its tempo, its beat, its key.
- KEEP AS IT IS: it plays as recorded. The first file of a piece sets its tempo.
- MATCH THE PIECE: it is stretched to the piece's tempo, starts on its first bar line, moves to the piece's key, and fills whole bars - repeated if it is shorter than the loop. High-quality time-stretch: no chipmunk, no wobble.
- Not the right tempo? ½ TEMPO / 2× TEMPO, or type it.)" },
        { "iPhones", R"(# iPhones: microphones and remotes
## Connecting
- Open ARCO on an iPhone on the same Wi-Fi as the station (Mac or iPad), or plug it into the Mac. The station finds it by itself: SETUP → iPHONES.
- Not found (some Wi-Fis block it)? Type the address the phone shows into SETUP → iPHONES.

## Microphones
- Choose L1-L4 for each phone (in SETUP or on the phone): its microphone plays as that LIVE input. Four phones = four microphones.
- The phone's microphone is used raw, full quality, with only a few milliseconds of delay.

## The remote
- PLAY: transport, REC, UNDO, scenes, and every track (REC, ▶ ■, CLR); record the show; save.
- MIX: every strip's level, pan, mute, solo, with meters.
- EFFECTS: every effect on the main window, its SEND for any strip, and all its knobs.
- SYNTH: slide pad, keys and chords that play the station's synth.
- Tap the phone's name to rename it. An iPad can be a phone too: View → Use This Device as a Phone.)" },
        { "MIDI & pedals", R"(# MIDI learn, pedals and keys
## MIDI LEARN
- Press MIDI LEARN (or ⌘L). Every knob, fader and button gets a frame.
- Click one, then move a knob or press a key / pad / pedal on your MIDI device. It is linked (green).
- Click a green one to unlink it. FORGET ALL unlinks everything. DONE or Esc finishes.

## Pedals
- SETUP → PEDALS & KEYS: LEARN a MIDI pedal for REC, PLAY / STOP, UNDO, NEXT SCENE.
- Page-turner pedals work too: they send arrow keys.)" },
        { "Keys", R"(# Keys
## The station
- → / Page Down / Return: REC        Space / ↓: PLAY / STOP
- ← / Page Up / Backspace: UNDO      ↑ / Tab: NEXT SCENE
- 1 - 4: scene A - D                 K: SYNTH
- ⌘S save, ⇧⌘S save as, ⌘O open, ⌘N new song, ⌘Z undo
- ⌘E export, ⌘R record the show, ⌘T STUDIO, ⇧⌘M AUTO MIX, ⌘L MIDI learn
- ⇧⌘L light / dark, ⌘, settings, ⌘M minimise, ⌃⌘F full screen, F1 help

## SYNTH
- A W S E D F T G Y H U J K O L P: notes      Z / X: octave      Esc: close

## STUDIO
- Space play / stop, S split, ⌘D duplicate, Delete remove, ⌘Z / ⇧⌘Z undo / redo, + / - zoom, Return to the start)" },
        { "KEYBOARD & instruments", R"(# KEYBOARD
A big keyboard and the instrument it plays - KEYS (top right) or View → KEYBOARD.

## The instrument
- SYNTH 2: ARCO's synthesizer. MODULAR: ARCO's modular synthesizer (patch it yourself).
- SAMPLED: real instruments recorded note by note. SEARCH: type what you want - cello, cello section, flute, piano, choir, harp, drums (also violoncelo, Flöte ...). 600+ free instruments: the Sonatina Symphonic Orchestra (solo and section strings, woodwinds, brass, choir, harp, percussion, piano, organ), Karoryfer Samples, Versilian, Salamander, Splendid and more.
- Each result shows its name, where it comes from, its size and licence. DOWNLOAD fetches only that instrument's files, inside ARCO - no web page, no account - then it plays. A stopped download goes on where it stopped.
- The shortcuts (STRINGS, WOODWINDS, PIANOS, DRUMS ...) list a family; ON THIS DEVICE lists what you have.
- ALSO FREE, ON OTHER PLATFORMS: Spitfire LABS, Pianobook, Komplete Start need their own free account and player. GET IT opens their page; install it, come back to ARCO - it finds the new plug-in by itself and opens it.
- ADD SFZ FILE: any SFZ instrument you have. PLUG-IN: your AU / VST3 (Mac) or AUv3 (iPad) instruments.

## Playing
- Tabs on top: SYNTH 2, MODULAR, SAMPLED, PLUG-IN - choose one and it plays.
- Below: KEYS, CHORDS or HARMONY, HOLD (notes keep sounding, tap more on top) and the octave. Computer keys (A W S E D ...), a MIDI keyboard and a sustain pedal work too.
- PLAYS INTO: the instrument sounds through LIVE 2-4, so loops, effects, RECORD ON TRACK and STUDIO all work with it.
- The instrument and its settings (the MODULAR patch, the plug-in's sound) are saved with the song.)" },
        { "MODULAR", R"(# MODULAR
A modular synthesizer: modules with knobs, connected by cables - in the spirit of a Eurorack system or VCV Rack.

## Patching
- Drag from an output (bottom row, coloured) to an input (top row). An input takes one cable; drag its plug away to unplug it.
- + MODULE adds one; ✕ removes it. PATCH opens a starting point: Basic subtractive, Generative sequence, FM bell, Noise drums, or an empty rack.
- Knobs: drag up / down (Shift = finer), double-click = back to start.

## The modules
- KEYS (the keyboard), VCO, LFO, ENV, VCF (ladder low-pass + band / high-pass), VCA, MIXER, NOISE, S&H, SEQ (8 steps), CLOCK (the loops' tempo), DELAY, ATTENUATE, QUANTIZER, SLEW, FOLDER, OUT.
- Pitch is 1 V per octave, gates are 10 V, sound is +/-5 V. OUT never lets the sound go above 0 dB.
- Play it right in the window: KEYS or HARMONY, HOLD and the octave are under the rack. CABLES makes the cables see-through.
- PLAYS INTO sends it into a LIVE strip like the SYNTH.)" },
        { "Speakers & Surround", R"(# OUTPUTS, SPEAKERS & SURROUND
View → Outputs, Speakers & Space. Two pages.

## OUTPUTS - to a mixing desk
- A grid: every sound (LIVE 1-4 - the SYNTH plays through its LIVE strip -, the tracks, drums, HALO) down the side, the sound card's outputs across. Tap a square: that sound goes out of that output. Several squares = several outputs.
- ROOM: the sound also stays in the main mix (the room's speakers).
- ONE OUTPUT EACH: every sound on its own output, ready for a desk. CLEAR ALL starts again.
- More outputs: plug in an interface (USB on the iPad) and choose it in SETUP → SOUND CARD.
- iPad: CHOOSE OUTPUT ▸ picks Bluetooth headphones, AirPlay, a cable or the speaker.

## SPEAKERS & SURROUND
Place every sound around the audience, still or moving.

## Your speakers
- Choose a layout (STEREO, QUAD, 5.0, 7.0, RINGS, DOME, CUBE, 5.0.4) or drag the speakers where they really are. Each one: direction, height, distance (ARCO evens out distance with delay and level).
- TEST plays a soft noise on one speaker.

## The sounds
- Choose a LIVE strip or a track, PLACE IT, and drag it in the circle: direction and distance. WIDTH spreads it.
- Motion: STILL, CIRCLE, SWING, WANDER, FLY OVER - SPEED and RANGE, or IN TEMPO with the loops.
- Up to 32 speakers. With headphones you hear a stereo version. Exports are always stereo.)" },
        { "What's new in 2.0", R"(# What's new in ARCO 2.0
- HOME: everything ARCO plays as big cards - drag them sideways, tap the one in the middle. WORKSTATION (the station), LIVE LOOPS, STUDIO, DRUM MACHINE, SYNTH, PHRASE, KEYBOARD, MODULAR, SPEAKERS.
- One bar on top of every screen: HOME, the transport, where you are, the tempo (drag or double-click), the bars' beats, CLICK, COUNT-IN, IN / OUT, UNDO, SAVE, SETUP. It plays what the screen is about: STUDIO's song in STUDIO, the pattern in DRUM MACHINE, the phrase in PHRASE, the cells in LIVE LOOPS.
- LIVE inputs: the SYNTH (and the KEYBOARD's instrument) no longer takes LIVE 2 at the first note - it plays through a LIVE strip that is not an input you use (LIVE 3 when you play into LIVE 1 and 2 ...).
- One clock for everything in tempo: the arpeggiator, the drum machine, PHRASE and LIVE LOOPS follow the metronome of the station, STUDIO's when STUDIO plays, or the tempo alone.
- DRUM MACHINE: twelve pads. JAZZ is a real kit (recorded close, overheads and room - Virtuosity Drums, CC0), 808, 909 and TRAP are modelled the way those machines make their sounds, LO-FI is the jazz kit on old tape. Each pad: level, pan, tune, decay, tone, space, mute / solo, its own kit. Eight patterns of up to 64 steps; every step: velocity, chance, repeats, nudge. Swing, humanise, drive, glue. Play the pads with the mouse, the keys (Z X C V / A S D F / Q W E R) or a MIDI keyboard; with REC they are written into the pattern.
- PHRASE: play a short phrase into it. It finds the notes and plays them back in tempo - faster or slower, in another order, higher, reversed, through a filter that opens with every note, spread left and right - and BUILD makes it grow over a few bars, then drop back.
- LIVE LOOPS: eight rows of cells. Tap an empty cell: it records from the next bar line (1, 2, 4, 8 bars or free); tap a loop to start or stop it, a scene to start a whole column. Drop audio files on a cell (fitted to the tempo), drag the station's loops in, send a loop to STUDIO.
- STUDIO lives inside the app now (its tools under the bar). Recording "the station" takes the real stereo mix (not the speaker outputs, which can carry the click).)" },
        { "What's new in 1.6", R"(# What's new in ARCO 1.6
- A new main window: cleaner and easier on the eyes, every button and knob still there. New knobs, buttons and colours in every window.
- The top bar in cards: PLAY / STOP / UNDO with BAR · BEAT · LOOP; TAP, BPM, BEATS, BARS, COUNT, CLICK; the song with SAVE · AS … · OPEN; the sound on / off.
- The strips fill the width. Each one shows its state inside the screen, big REC and PLAY / STOP, CLR · 1x · FILE, IN, and a long fader with a dB scale and the exact level above it (double-tap: back to 0 dB; shift-drag on the Mac: finer). M · S · listen · ROOM at the bottom. Tap a strip's name: the effects' SEND knobs edit that strip.
- MASTER is now a strip beside the tracks: REC SHOW, the time, MASTER, GLUE, IN / OUT and the CPU.
- EFFECTS in six families - TONE, MOVEMENT, SPACE, ECHOES, TEXTURE, RHYTHM. The effect map under the strips shows every effect you chose; the ones working are lit, with how many per family. Tap a family (or one effect) and its cards open below with big knobs; more than fit: the arrows on the map. Each card has an on / off switch for the selected strip (it remembers the level), its value at the top right, and a little live picture of its sound.
- LIGHT colours: warm grey instead of near-white, softer for the eyes.)" },
        { "What's new in 1.5", R"(# What's new in ARCO 1.5
- AUTO MIX, rebuilt: it balances by how loud each part sounds while it plays - every voice heard, nothing pushed far down or up. Tell it what you want in your words ("warm, spacious, track 2 louder"), or with three simple choices. THE BALANCE shows before / after. The master is as loud as it can be without squashing the music.
- KEYBOARD: search 600+ free instruments (cello, cello section, flute, choir ...) and DOWNLOAD them inside ARCO - no web page, no account. Spitfire LABS, Pianobook and Komplete Start are listed too: GET IT, install, and ARCO finds the plug-in by itself.
- The lists scroll with a finger on the iPad.
- iPad upright: everything of the top bar stays at the top, the window buttons across the whole width; the strips are not stretched any more and the effects grow into the room (also in landscape).)" },
        { "What's new in 1.4", R"(# What's new in ARCO 1.4
- iPad: CHOOSE OUTPUT ▸ sends the sound to Bluetooth headphones, AirPlay or a cable; Bluetooth headphones get the full-quality connection. Adding a file to a track no longer crashes.
- HOLD: tap a note, then more on top - they all keep sounding; with the arpeggiator every tap joins the arpeggio.
- HARMONY: a new way to play, made for fingers - neighbours always sound good together. CHORDS: choose any chord, or its exact notes.
- KEYBOARD and MODULAR: cleaner windows, play them right there.
- OUTPUTS: a simple grid to send each sound (live, tracks, synth ...) to one or more outputs - for a mixing desk.
- STUDIO, rebuilt: simpler and always at the right speed.
- New effects: VOCODER, PITCH, SPRING, TALK.
- The top bar: every button the same size; the station fills the screen in both directions.)" },
        { "What's new in 1.3", R"(# What's new in ARCO 1.3
- Colours: the original black, with Storm, Light and Chambray in the same calm contrast.
- STUDIO: a full studio - instrument lanes with MIDI and a piano roll, devices on every lane, automation, return reverb and delay, recording (inputs, the station, MIDI), loop, click, count-in, markers, mixer, stems.
- KEYBOARD: a big keyboard with free sampled instruments to download (pianos, Rhodes, basses, drums), SFZ files, your AU / VST3 / AUv3 plug-ins and MIDI keyboards.
- MODULAR: ARCO's own modular synthesizer.
- SYNTH 2: a fourth oscillator; ten classic synthesizers as starting sounds.
- AUTO MIX, rebuilt: it understands bass, drums, rhythm, melody and pads, mixes by masking, puts everything in one room and times the delay to the melody.
- New effects: PEDAL (a piano's sustain pedal) and FEEDBACK (a note that feeds itself, safely). Effect presets and new-song defaults.
- SPEAKERS & SURROUND: any speaker layout, sounds placed all around, still or moving.
- iPhone: choose the microphone and the output, and listen to the station on the phone. iPad: choose inputs and outputs (cable, USB, Bluetooth); the window turns with the iPad.)" },
        { "What's new in 1.2", R"(# What's new in ARCO 1.2
- New colours: Fig, Storm (dark), Manuscript, Chambray (light) - View → Colours. A calmer light mode with clear contrast.
- Choose which effects are on the main window; 10 new effects.
- AUTO MIX: a professional mix in one press - levels, EQ, compression, panning, effects in tempo, gentle timing, master. Before / after.
- STUDIO: the song as a timeline - edit, mix, export.
- SYNTH 2: three oscillators (FM, fold, sync), a 24 dB filter, three envelopes, three LFOs, drag-to-modulate, macros, arpeggiator, slide pad, chord pads, scale grid.
- iPhones: several phones as microphones on LIVE 1-4, and each one a remote for everything.
- Smart import: drop a file - keep it, or match the piece's tempo and key.
- MIDI learn for every control.
- A menu bar (File, Edit, View, Help), export of the mix and stems, this help.)" },
    };
    return s;
}
} // namespace arco
