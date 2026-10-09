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
- Top: transport (PLAY, STOP, UNDO), the tempo, the song, the show recorder, MASTER, and the windows: SYNTH, STUDIO, SETUP.
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
- HALO turns notes into chords, SHADOW sings intervals, RESONANCE adds strings, GRAIN a cloud, FREEZE holds the sound, SPACE is the big reverb, ORBIT moves sound over your speakers.)" },
        { "AUTO MIX", R"(# AUTO MIX
ARCO listens to every recorded track, understands what it is (drums, bass, chords, melody, texture) and mixes the piece.

## What it does
- LEVELS: a balance by role - the melody leads, drums and bass carry, pads sit behind.
- EQ: a high-pass below each part, less mud where parts crowd each other, less harshness, a ringing note tamed, a little air on the lead.
- COMPRESSION: gentle and different for each role.
- PANNING: bass, drums and the main melody in the centre; the rest spread so both sides weigh the same.
- EFFECTS IN TEMPO: no reverb on the bass; a shared SPACE glues the parts; effect speeds that are nearly in tempo are snapped to it; the SPACE decay follows the bar.
- TIMING: notes are pulled towards the beat - GENTLE, HALF WAY or TIGHT. The groove and swing stay.
- MASTER: streaming level (-14 LUFS), peaks never above -1 dB.

## Compare and undo
- BEFORE / AFTER switches between the two at any time (also Edit → Compare). UNDO goes back completely.
- CHARACTER: NATURAL, BALANCED or PUNCHY.)" },
        { "STUDIO", R"(# STUDIO
Your loops as a song, to edit and mix like in a DAW - in its own window.

## The timeline
- STUDIO lays out every scene with loops, one after the other (4 times each). REBUILD does it again from the station.
- Click a clip to choose it. Drag it to move it (it snaps to the beat; Shift = finer, Alt = free). Drag it to another lane.
- Drag a clip's left or right edge to trim it. Drag its top corners for fade in / fade out.
- S splits the chosen clip at the playhead. ⌘D duplicates it. Delete removes it. Double-click: loop on / off.
- Click the ruler to move the playhead. Space plays / stops. ⌘Z / ⇧⌘Z undo / redo. Scroll to move, ⌘ + scroll to zoom.
- Drop audio files on a lane to add them.

## The mixer
- Each lane: M, S, volume (drag the bar), and below: PAN, HIGH-PASS, LOW, MID, MID FREQ, HIGH, COMPRESS.
- The clip: GAIN, FADE IN, FADE OUT.
- MASTER and GLUE.
- PRINT FX: the chosen lane plays through the station's effects (reverbs and echoes included, in stereo). UNDO brings back the dry sound.

## Export and save
- EXPORT: WAV or FLAC, streaming level or as it is, up to 96 kHz / 32-bit float. The true peak stays below -1 dB.
- SAVE keeps the arrangement with the song (also when you save the song on the station).)" },
        { "SYNTH 2", R"(# SYNTH 2
## Sound
- Three oscillators, each WAVE (sine → saw → square → pulse, WARP bends it), FM, FOLD or SYNC. UNISON and DETUNE make it wide.
- SUB and NOISE, a FILTER (low / band / high, 12 or 24 dB), VOICE: GLIDE, WIDTH, MONO, FOLLOW (a voice sings what your cello plays).

## Movement
- Three envelopes and three LFOs (free or in tempo).
- Drag a ⟿ dot (LFO, ENV, MACRO) onto any knob: that knob moves. The coloured ring shows how much.
- The MODULATION list shows every connection: drag the bar to change how much, ✕ removes it. Right-click a knob to remove its modulation.
- Four MACROS: one knob that moves several.

## Play
- ARPEGGIATOR: hold a chord - up, down, up-down, random, as played, or chords; 1/4 to 1/32 in the loop's tempo; 1-4 octaves; GATE; HOLD.
- Ways to play: KEYS (piano, computer keys, MIDI), SLIDE PAD (left-right pitch, up-down loudness, gliding), CHORDS (8 chords of the key), SCALE GRID (only the notes of the key).
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
