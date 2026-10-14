# BowString — guitar to violin / sarangi (standalone app + plugin)

BowString listens to your guitar, tracks the note you're playing, and drives a bowed-string voice
(violin or sarangi) with slow bow swells, glide, delayed vibrato, bow noise and body resonances.
After that it runs through a pedal chain: Gate → Comp → Drive → EQ → Chorus → Delay → Reverb → Output.

## 1. Get the app

BowString runs **on its own as a standalone app** (no DAW needed), and also as a plugin
(AU for Logic, VST3 for FL Studio, Ableton, Reaper, Cubase and others).

### Option A: download ready-made builds (no Xcode or Visual Studio needed)

1. Create a free GitHub account and make a new repository.
2. Upload everything in this folder, **including the hidden `.github` folder** (or push it with Git).
3. Open the repository's **Actions** tab. The "Build BowString" workflow runs automatically (about 10–15 minutes).
4. When it shows a green tick, open the run and download, under "Artifacts":
   - **BowString-macOS**: the Mac app, the AU for Logic, and the VST3
   - **BowString-Windows**: `BowString.exe` and the VST3

### Option B: build it yourself

**Mac** (needs Xcode and `brew install cmake`):

```bash
cmake -B build -G Xcode
cmake --build build --config Release --target BowString_Standalone BowString_AU
```

The app ends up in `build/BowString_artefacts/Release/Standalone/BowString.app`, and the AU
is copied into Logic's plug-in folder automatically.

**Windows** (needs Visual Studio 2022 with "Desktop development with C++"; open the
Developer Command Prompt **as Administrator**):

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --target BowString_Standalone BowString_VST3
```

The app ends up in `build\BowString_artefacts\Release\Standalone\BowString.exe`.

## 2. Using the standalone app

1. Plug your guitar into your audio interface and connect headphones or speakers to the interface.
2. Open BowString and click **Options → Audio/MIDI Settings** at the top of the window.
3. Choose your interface as both input and output, enable the input channel your guitar is on,
   and set the buffer size to 128 samples or lower.
   - On Windows, pick the **ASIO** driver type for low latency.
4. If you see "Audio input is muted to avoid feedback loop", untick **Mute audio input**.
   (Use headphones if you're on laptop speakers, or it may howl.)
5. On Mac, allow microphone access the first time it asks. That is how macOS lets the app hear the guitar input.

**First launch warnings** (the builds aren't signed with a paid developer certificate):
- Mac: right-click `BowString.app` → **Open** → Open. If it still refuses, run `xattr -cr BowString.app` in Terminal.
- Windows: if SmartScreen appears, click **More info → Run anyway**.

### Using it as a plugin in Logic

1. Logic → Settings → Plug-in Manager → find **Ali Hassan → BowString** → Reset & Rescan Selection if needed.
2. Create an Audio track with your guitar input and turn on Input Monitoring.
3. Audio FX slot → Audio Units → Ali Hassan → **BowString**.

## 3. Playing tips

- It's **monophonic**: play single notes, not chords. Let one note stop before the next, or slide.
- Use the **neck pickup** with the tone knob rolled back a little. Cleaner input = more stable tracking.
- If notes cut out or flicker, lower **Gate → Threshold**. If it triggers on string noise, raise it.
- Violin sits higher than guitar: try **Octave +1** for real violin range.
- For sarangi: Instrument = Sarangi, turn **Sympathetic** up (40–70%), **Glide** up (150–300 ms) and slide into notes.
  The sympathetic strings are chromatic, so they ring along with whatever note you play.
- **Pick harder for a biting bow attack, softer for a slow swell.** The Swell knob sets how slow the softest notes are.
- New notes change pitch instantly (like a violinist's finger). Slides and bends on the guitar glide; the Glide knob sets how smoothly.
- **Bow movement:** on held notes the bow changes direction every **Stroke** seconds (a short dip with a
  rise in rosin noise, alternating down-bow and up-bow). **Bow motion** sets how audible the changes and
  stroke swells are; **Rosin** sets the scratchy bow-hair texture. Every new picked note is also a new bow stroke.
  For a more aggressive, "close-mic" bow: Rosin 70–90%, Bow motion 80–100%, Stroke 0.5–0.8 s.
- **Articulation → Pizzicato** turns every note into a plucked violin note. Pizz decay sets how long it rings;
  lifting your fretting finger (muting the guitar) stops it, like a real player damping the string.
- **Blend** = 100% is pure bowed voice; lower it to mix in your real guitar (with its pick attack removed).
- **Attack** is how slowly the bow swells in. **Dynamics** = how much your picking strength controls volume.

## Real violin engine

**Instrument → Engine** switches between:
- **Modeled** – the synthesized bowed string (violin or sarangi), as before
- **Real violin** – recordings of a real violin (University of Iowa Musical Instrument Samples), 241 notes
  from G3 to E7: arco at pp / mf / ff and pizzicato at pp / mf / ff

How it plays:
- Every note uses the nearest recorded note, retuned to exactly the pitch you play, so **bends, slides and meend**
  follow your guitar. On wide slides it hands over to a closer recording so no note is stretched far.
- **Pick strength and your guitar's volume = bow pressure.** Soft playing uses the pp recordings, medium mf,
  hard ff, with short crossfades between them.
- **Long notes loop seamlessly**, so you can hold a note as long as your guitar sustains.
- A new pick is a new bow stroke (the recording's own bow attack). Hammer-ons, pull-offs and fast fingered
  notes change note *without* re-bowing.
- **Articulation → Pizzicato** plays real plucked-violin recordings, chosen by how hard you pick.
- Bow changes (Stroke, Bow motion), Rosin, Glide, Swell, Release, Sympathetic and all the pedals still work on top.
- **Vib depth jumps to 0 when you switch to Real violin**, because the player's own vibrato is in the recordings.
  Turn it up if you want extra vibrato; switching back to Modeled restores it.
- **Bright** works as a treble tilt (3 kHz shelf) in this engine.
- The Instrument selector (Violin / Sarangi) only affects the Modeled engine: the recordings are a violin.
- A real violin can't play below G3. Lower notes are pitched down from G3 and sound thicker, so with guitar
  in standard tuning try **Octave +1** (preset "Real violin - Octave up").
- The samples load in the background when the plugin opens (about a second). The note display says
  "Loading violin samples..." until they are ready. They use about 100 MB of RAM, shared by all instances.

Presets: Real violin - Expressive · Real violin - Octave up · Real violin - Slow & lyrical · Real violin - Pizzicato

### How the samples were prepared (`tools/sample_pipeline/`)

`build_samples.sh` downloads the pre-2012 violin scale recordings (arco + pizz, pp/mf/ff) and the 2012
ff per-string zips, then:
1. slices the scale files at the silences between notes,
2. measures the pitch of every note (the file names are not reliable: several pre-2012 files hold different
   notes than their names say, and some 2012 pizzicato files are labelled up to 3 semitones too high), and tunes
   each sample to its measured pitch,
3. trims each note, finds a loop in the sustain by waveform correlation, level-matches across the loop and bakes a
   crossfade into it,
4. loudness-matches every note within each dynamic layer,
5. picks the best take per note (in tune, steady, clean loop, natural string position, same string across pp/mf/ff),
6. packs everything into one Ogg Vorbis file (`Samples/violin_pack.ogg`, ~6 MB) plus `Source/SampleMap.h`,
   which are compiled into the plugin.

Samples: University of Iowa Electronic Music Studios, Musical Instrument Samples
(theremin.music.uiowa.edu), free to use without restriction.

### Offline test tool

`cmake -B build -DBOWSTRING_BUILD_TESTS=ON` adds `BowStringRender`:
- `BowStringRender --check` decodes the samples and checks every loop seam
- `BowStringRender guitar.wav out.wav preset=10 octave=2 revmix=0.2` runs a recording through the plugin

`tools/sample_pipeline/guitar.py` makes the simulated Strat melody used for the demos.

## Presets

Pick one from the **Preset** menu at the top, then tweak anything you like:
Violin - Bowed (realistic) · Violin - Bowed, more bow · Violin - Soft & smooth · Violin - Aggressive, close mic ·
Violin - Octave up · Violin - Pizzicato · Violin - Wide dynamic range · Violin - Even dynamics ·
Sarangi - Bowed with meend · Sarangi - Pizzicato ·
Real violin - Expressive · Real violin - Octave up · Real violin - Slow & lyrical · Real violin - Pizzicato

## Rosin type

Changes how the bow hair grips the string, like using a different rosin:
- **Light** – smooth and clean, very little scratch
- **Amber** – balanced (default)
- **Dark** – grippy and warm, more grit and a stronger bite
- **Gold** – brighter, sparkly texture
- **Old** – harsh and scratchy, like old or cheap rosin

The **Rosin** knob sets how much bow-hair noise you hear; **Rosin type** sets its character.

## Controls

| Section | Controls |
|---|---|
| Preset | 14 ready-made setups (also in Logic's preset menu) |
| Instrument | Engine (Modeled/Real violin), Instrument (Violin/Sarangi), Articulation (Arco/Pizzicato), Octave, Rosin type, Blend, Bright, Rosin, Sympathetic |
| Playing | Swell, Release, Dynamics, Glide, Vib rate, Vib depth, Bow motion, Stroke, Pizz decay |
| Gate | Threshold |
| Comp | Sustain |
| Drive | Drive, Level |
| EQ | Bass (120 Hz shelf), Mid (900 Hz), Treble (3.5 kHz shelf) |
| Chorus | Rate, Depth, Mix |
| Delay | Time, Feedback, Mix (ping-pong stereo) |
| Reverb | Size, Damping, Mix |
| Output | Volume |

All controls are automatable in Logic and saved with your project.

## Files

- `Source/BowDSP.h` — pitch tracker (YIN), bowed voice, pizzicato string, sympathetic strings, delay
- `Source/RealViolin.h` — Real violin engine: sample loading and playback
- `Source/SampleMap.h`, `Samples/violin_pack.ogg` — the prepared violin samples (generated)
- `tools/sample_pipeline/` — scripts that download and prepare the samples
- `Tests/RenderDemo.cpp` — offline render / self-test tool
- `Source/PluginProcessor.*` — parameters and the full signal chain
- `Source/PluginEditor.*` — the pedalboard interface
