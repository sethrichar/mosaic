# Mosaic — Product & Technical Spec

**Doc version:** v0.4 (working draft, 2026-11-16) — previous: `docs/archive/SPEC_v0.3.md`
**Owner:** Seth Richardson / Richardson Media Solutions LLC
**Repo:** https://github.com/sethrichar/mosaic.git
**Companion docs:** `CLAUDE.md` (rules for Claude Code), `docs/BUILD_PLAN.md` (milestones)

---

## 1. What Mosaic is

Mosaic is an audio plugin that builds a single "mosaic" sound out of up to 8 sources.
It chops the sources on a tempo-synced rhythmic grid and plays **one tile at a time**,
never overlapping, in random or sequenced order — a real-time, in-the-box version of
the manual "chop up stems in the same key and reassemble them" workflow, in the spirit
of musique concrète.

A source can be:

- a **file** loaded into the plugin (a stem, a loop, any audio at session tempo), or
- a **live input** — audio from another track in the session, switched in real time.

Press play in the DAW and Mosaic plays the mosaic in sync with the session.

Imported files have their musical **key detected** on load. Each file can be **snapped**
to the mosaic's key (transposed, tempo untouched) or left at its original pitch.

## 2. Vocabulary

| Term | Meaning |
|---|---|
| **Slot** | One of 8 source positions, numbered 1–8. Each slot has a color. |
| **Source** | What feeds a slot: a File, or a Live input. |
| **Grid / Rate** | The rhythmic subdivision that sets tile length (1/4, 1/8, 1/16 …). |
| **Step** | One grid position on the timeline. Step index is derived from the host's musical position. |
| **Tile** | The audio actually played during one step: one slice of one slot. |
| **Pattern** | The sequence of slot choices across steps (random or user-sequenced). |
| **Locked / Free** | Where a tile's audio is taken from within its source (section 4.4). |
| **Key** | The mosaic's target key: a root note plus Major or Minor. One per plugin instance. |
| **Snap / Original** | Per file slot: transpose the file to fit the Key, or leave its pitch alone (section 7). |

## 3. Decisions already made

Answered by Seth on 2026-11-16:

1. **Hosts:** Ableton Live, Logic Pro, Reaper. macOS first.
2. **Live mode = live switcher.** Mosaic jumps between incoming tracks on the grid as
   the session plays. Nothing is recorded.
3. **Slice position is switchable** between Locked and Free.
4. **Files are assumed to already be at session tempo.** No time-stretching in v1.

Proposed by Claude, **confirmed by Seth** on 2026-11-16:

5. One plugin binary with two roles (Mosaic / Send) rather than two separate plugins.
6. Any slot can be a File or a Live input; they can be mixed freely.
7. Random patterns are seeded and repeatable (same result every pass until "Reroll").
8. Formats: VST3 + AU (v2) + Standalone. No AAX / Pro Tools, no Windows in v1.
9. UI direction: clean and minimal, same family as Clear Space
   (Valhalla / Cradle / stock Ableton devices).

Added by Seth on 2026-11-16 (v0.2):

10. **Key detection and snap-to-key for imported files.** Files get their key detected
    on import and can be snapped to the mosaic's key or left to run at their original
    pitch. Applies to File slots only, not live inputs. Full behavior in section 7.

Assumed by Claude for the key feature — **Seth to confirm or change** (see section 13):

11. Pitch shifting is rendered **offline in the background** when a file is imported or
    the key changes, not processed live. Tempo and length never change.
12. "Snap" fits a file to the target key's **scale** (relative major/minor count as
    already in key), using the smallest transposition.
13. New files default to Snap when a key is confidently detected, and to Original when
    it isn't (drums, noise, atonal material).

## 4. Core behavior

### 4.1 Transport

- Mosaic runs only while the host transport is playing. When stopped, output is silent.
- All timing comes from the host playhead (`ppqPosition`, tempo, time signature,
  `timeInSamples`, `isPlaying`). There is no free-running internal clock in the plugin.
- The Standalone build has its own simple transport (play/stop, BPM) for testing.

### 4.2 Grid

- **Rate:** 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, each with straight, dotted and triplet variants.
- Step index = `floor(ppqPosition / stepLengthInQuarterNotes)`.
  Step boundaries must be sample-accurate inside a block (a block can contain several
  boundaries at fast rates or large buffer sizes).
- Changing Rate takes effect at the next step boundary.

### 4.3 Choosing which slot plays (selection modes)

One slot is chosen per step. Slots that are empty or muted are never chosen.

**Random mode**

- Weighted: each slot has a **Weight** (0–100%). Probability is proportional to weight.
- **Seeded and stateless:** the choice for a step is a pure function of
  `(seed, stepIndex)`. Consequences, all required:
  - Playing the same bars twice gives the same mosaic.
  - Starting playback mid-song gives the same tiles as playing through from bar 1.
  - Offline bounce matches real-time playback.
- **Reroll** button picks a new seed. Seed is saved in the plugin state and is automatable.
- **Pattern length:** Off (never repeats), or 1 / 2 / 4 / 8 bars (the random pattern
  loops with that period — `stepIndex` is wrapped before hashing).
- **Avoid repeats** toggle: prefer not to choose the same slot twice in a row. Must stay
  stateless (derived from seed and step index only).

**Sequence mode**

- A step sequencer lane, 1–64 steps (default 16). Loops against the timeline.
- Each step holds one of: a slot number (1–8), a **rest** (silence), or **"?"**
  (pick randomly for this step using the Random rules above).
- Sequence is saved in plugin state.

### 4.4 Where the tile's audio comes from (slice position)

**Locked (default)**
Every source is treated as if it were a track running in sync with the session, and
Mosaic is a rhythmic switcher between them. At bar 5 beat 3, you hear bar 5 beat 3 of
whichever slot was chosen. Stems of the same song stay musically aligned.

- File position = `(timelineSamplePosition − slotStartOffset) mod fileLength` when the
  slot's **Loop** is on; silence past the end of the file when Loop is off.
- Each File slot has a **Start offset** (in bars, positive or negative) to slide it
  against the timeline.

**Free**
Each tile is taken from a random position in the chosen file. The file is divided into
slices of the current step length starting at the file start; the slice index is a pure
function of `(seed, stepIndex, slot)`.

- Free applies to **File slots only**. Live slots can only ever play "now", so they
  behave as Locked regardless of this setting.

### 4.5 Tiles never overlap

- Exactly one slot is audible at a time (monophonic by design).
- **Fade:** a short declick fade at every tile edge, 0–20 ms, default 5 ms. Implemented
  as a fade-out of the outgoing tile and fade-in of the incoming tile; the brief
  crossfade is the only moment two sources coexist.
- **Tile length (gate):** 10–100% of the step, default 100%. Below 100% leaves a gap
  of silence before the next tile.

### 4.6 Per-slot controls

| Control | Range | Notes |
|---|---|---|
| Source | File / Input bus N / Send N | See section 5 |
| Gain | −inf to +12 dB | |
| Mute | on/off | Muted slots are skipped by Random; act as rests in Sequence |
| Weight | 0–100% | Random mode only |
| Start offset | ± bars | File slots only |
| Loop | on/off | File slots only |
| Key mode | Snap / Original | File slots only (section 7) |
| Source key | Detected, or user override | File slots only; shown with a confidence indicator |
| Transpose | −12 to +12 semitones | File slots only; added on top of any Snap shift |
| Fine | −50 to +50 cents | File slots only |

### 4.7 Global controls

Rate · Mode (Random / Sequence) · Position (Locked / Free) · Fade · Tile length ·
Seed + Reroll · Pattern length · Avoid repeats · Key · Output gain.

All continuous and switch parameters are host-automatable via
`AudioProcessorValueTreeState`, **except the key and pitch controls** (Key, Key mode,
Source key, Transpose, Fine): these trigger a background re-render and are stored in
plugin state, not exposed for automation. Parameter IDs are permanent once a public
build ships.

## 5. Live input — routing design

### 5.1 The problem

Getting 8 separate tracks into one plugin instance is a **host** limitation, not a
plugin-format limitation.

The AU API (`AUAudioUnitBus` / `AUAudioUnitBusArray` in AUv3, the element/bus scopes in
AUv2) lets a plugin publish as many input buses as it wants, and JUCE generates those
buses automatically from `BusesProperties` for both AU and VST3. The plugin side is easy.
What matters is how many of those buses each DAW will actually connect:

| Host | What it actually feeds (measured, M1, 2026-11-16) | How |
|---|---|---|
| Reaper | All 8 stereo buses | Track channels set to 16, plugin pin connector |
| Logic Pro | Main input + **one** sidechain (Input 1 and 2) | Plugin header Side Chain menu |
| Ableton Live 12 | Main input + **one** sidechain (Input 1 and 2), VST3 and AU | Device sidechain "Audio From" |

So native multi-bus input alone cannot deliver "8 live sources" in Live or Logic. The
M1 routing spike measured this (`docs/ROUTING_RESULTS.md`) and confirmed the Send design
below works in all three hosts.

### 5.2 The design: two transports behind one interface

Every slot reads audio through a `SlotSource` interface. Two live implementations:

**A. Native input buses**
- Main stereo input = `Input 1`. Seven more optional stereo input buses
  (`Input 2`–`Input 8`), disabled by default so the plugin still loads as plain stereo.
- Zero added latency. Works fully in Reaper; gives at least Input 1 (and probably
  Input 2 via sidechain) in Live and Logic.

**B. Mosaic Send (inter-instance audio)**
- The same plugin, switched to the **Send** role, is inserted on each source track.
  The user picks a **Group** (A–D) and a **Send number** (1–8). Audio passes through
  the Send instance unchanged (optional "mute output" so the dry track isn't also heard).
- Send instances write their audio into a process-wide hub (a singleton shared by all
  Mosaic instances loaded in the DAW process). The Mosaic-role instance in the same
  Group reads from it.
- **Measured (M1):** all 8 Sends reach one Mosaic instance in Live, Logic and Reaper,
  and the null test (5.4) cancels in real time at buffers 64/512/2048 and in an offline
  bounce, with a 2048-sample look-behind. Send is the primary live transport.
- Works in any host that loads all instances in one process: Live, Logic (AUv2),
  Reaper with default settings. Does **not** work where instances are sandboxed in
  separate processes (Bitwig; Reaper if "run as dedicated process" is enabled; AUv3) —
  this is why the AU target is AUv2 and why the role lives in one binary (statics are
  only shared within the same binary).

Per slot, the Source menu offers: `File`, `Input 1…8`, `Send 1…8`.

### 5.3 Send hub — requirements

The implementation is Claude Code's to design, but it must meet these:

1. **Lock-free and allocation-free on the audio thread.** One single-producer /
   single-consumer ring buffer per (Group, Send number), stereo, sized for at least
   2 seconds at 192 kHz. Write position published atomically after the data is written.
2. **Independent of host processing order.** The host may process the Send track before
   or after the Mosaic track, on the same or different threads, and the order can change
   block to block. The receiver must never read data that "might not be there yet".
   Proposed approach: each Send block is tagged with its timeline sample position; the
   receiver requests audio for `(its own timeline position − L)` where the look-behind
   `L` ≥ the maximum block size, and reports `L` via `setLatencySamples()` so the host's
   delay compensation puts the output back on the grid.
   **`L` must be measured, not assumed.** Once Mosaic reports latency, a host's delay
   compensation may process the Mosaic track *ahead* of the Send track by up to `L`, so
   the receiver could ask for samples the Send has not written yet.
   **Decided (M1):** `L = 2 × the host block size from prepareToPlay`, never below 256
   samples, recomputed on every `prepareToPlay`. The probe build showed 2048 is enough at
   every buffer size up to 2048 in all three hosts; the measured floor per host is read
   from the probe's "smallest look-behind" readout at the M5 checkpoint, and the factor
   is raised if any host needs more than 2× its block.
3. **Sample-accurate.** A null test (section 5.4) must cancel completely.
4. **Graceful discontinuities.** Loop wrap, locate, tempo change, a Send instance being
   removed or bypassed: at worst a short faded gap, never a click, glitch loop, or stale
   audio. A Send with no fresh data reads as silence. The hub tracks a valid region
   `[tail, head)`; a discontinuity on the Send side moves `tail`, so audio from before a
   locate or loop wrap is never returned.
   Sends transmit only while the transport is playing (4.1).
5. **Collision handling.** Two Send instances on the same Group + number: last one wins,
   and both UIs show a clear warning.
6. **Latency only when used.** If no slot uses a Send source, reported latency is 0.

### 5.4 How routing is verified (the null test)

Put the same audio clip on two tracks. Track A goes through a Send into a Mosaic
instance set to pass that slot straight through (probe mode). Track B is
polarity-inverted. If the sum is silence, the path is sample-aligned. This is run in
each DAW, at 2–3 buffer sizes, in real time and in offline bounce.

## 6. File handling

- Formats: WAV, AIFF, FLAC, MP3 (JUCE `AudioFormatManager` basic formats).
- Load by drag-and-drop onto a slot or via a file browser button.
- Files load on a background thread, fully into RAM as 32-bit float, resampled once to
  the session sample rate with a high-quality offline resampler. The finished buffer is
  handed to the audio thread with a lock-free pointer swap. The old buffer is released
  off the audio thread.
- Mono files play to both channels. Files with more than 2 channels use the first 2.
- If the session sample rate changes, files are re-resampled in the background.
- Memory: a 5-minute stereo stem at 48 kHz is ~115 MB as float; 8 of them is ~920 MB.
  v1 shows total memory use and warns above 1 GB. Disk streaming is a later item.
- Only the buffer that is actually played is kept in RAM. When a slot is snapped or
  transposed, that is the pitch-shifted buffer; the original is re-read from disk if a
  new render is needed. RAM use must not double because of the key feature.
- Plugin state stores each file's absolute path. On restore, a missing file shows a
  "missing — relink" state on that slot; nothing crashes and other slots still load.

## 7. Key detection and snap-to-key (File slots)

### 7.1 What the user gets

- Drop a file on a slot → Mosaic detects its key and shows it on the slot (e.g. "F# min").
- The plugin has one global **Key** (root + Major/Minor).
- Each File slot has a **Key mode**:
  - **Snap** — the file is transposed so its notes fit the global Key. Tempo, length and
    grid alignment are unchanged.
  - **Original** — the file plays at its own pitch ("run free").
- Live slots are never pitch-shifted.

### 7.2 Global Key

- Root: C, C#, D … B. Scale: Major or Minor.
- **Auto** (default): the Key follows the lowest-numbered File slot that has a confident
  detection or a user-set source key. In practice: the first pitched stem you drop sets
  the key and everything after it snaps to that.
- **Manual:** pick any key. All Snap slots re-render to it.

### 7.3 Per-slot key controls

- **Source key:** the detected key with a confidence indicator (high / low / none). The
  user can override it at any time (the detector will sometimes be wrong; the usual error
  is landing a fifth away). An override is treated as fully confident.
- **Key mode:** Snap / Original. Default on import: Snap if confidence is high, Original
  otherwise.
- **Direction** (Snap only): Nearest (default) / Up / Down — which way to transpose when
  both are possible.
- **Transpose** (±12 semitones) and **Fine** (±50 cents): manual offsets applied on top,
  available in both modes.

### 7.4 Snap math

Snap aligns **key signatures**, so relative major/minor pairs count as the same key.

1. Convert each key to its signature pitch class: a major key uses its root; a minor key
   uses root + 3 semitones (A minor → C, F# minor → A).
2. `shift = targetSignature − sourceSignature`, wrapped by Direction:
   - Nearest: −6 … +5 semitones (a tritone resolves downward)
   - Up: 0 … +11 · Down: −11 … 0
3. Total pitch change = shift + Transpose + Fine.

Examples (target key A minor): a C major file → 0. A D minor file → −5. An E major file
→ −4. Parallel-mode differences (C major into C minor) cannot be fixed by transposition;
the file is moved to the target's signature and that is accepted for v1.

### 7.5 Detection requirements

- Runs on a background thread after the file loads. Never on the audio thread.
- Implemented in-house in `source/analysis/` as plain C++ with unit tests. Suggested
  method: mono downmix → downsample → STFT → 12-bin pitch-class profile with tuning
  estimation → correlation against 24 rotated key profiles (keep the profile table
  swappable). Output: root, scale, and a confidence value from the strength of the best
  match and its margin over the next non-relative candidate.
- **No GPL/AGPL code.** libKeyFinder (GPL) and Essentia (AGPL) cannot be used in a
  closed-source plugin; do not copy from them.
- Material with no clear key (drums, noise, FX) must come back as "none", not a guess.
- The detected key, confidence, and any override are saved in plugin state. Restoring a
  session does not re-detect.

### 7.6 Pitch-shift requirements

- **Offline, per file.** Render the shifted buffer on a background thread, then swap it
  in lock-free. The engine and all position math are untouched: a shifted file is just
  another buffer of the same length.
- **Library:** Signalsmith Stretch (MIT licence, header-only C++) behind a small
  `PitchShifter` interface so it can be replaced later (Rubber Band and zplane élastique
  are the upgrade options; both need a paid commercial licence). Pin the exact commit.
- **Exact length and alignment.** Output length equals input length to the sample. The
  library's latency/pre-roll is compensated so transients stay on the grid.
- **Deterministic.** The same file and settings always produce the same buffer, so
  sessions restore and bounce identically.
- **While rendering:**
  - A newly imported slot stays silent (treated as empty) until its buffer is ready.
  - A slot that is already playing keeps its current buffer until the new render is done,
    then swaps at a tile boundary. No clicks.
  - Each slot shows a progress indicator. Rapid changes are debounced; a superseded
    render is cancelled.
- **Offline bounce safety.** When the host is rendering offline (`isNonRealtime()`),
  wait for pending loads and renders rather than outputting silence or stale pitch.
  This is the one sanctioned exception to the "no waiting on the audio thread" rule in
  `CLAUDE.md`: the wait is bounded and only ever happens when the host is not real-time.
- A shift of 0 with no Transpose/Fine bypasses the shifter entirely (bit-exact original).

### 7.7 Known limits (v1)

- Transposition artifacts grow with the interval. ±1–3 semitones should be clean on most
  material; ±5–6 will be audible on vocals and full mixes. Formant correction is a later
  item unless the pinned library version provides it cheaply.
- Key changes are not instant and not automatable; they re-render.
- Reopening a session re-renders every snapped file before it can play.
- One key per file. Files that modulate are treated as a single key.

## 8. Plugin identity and formats

- Plugin name: **Mosaic**. Company: **Richardson Media Solutions**.
- Bundle ID: `com.richardsonmediasolutions.mosaic`.
- Manufacturer code: **`Rmsl`** (same as Clear Space). Plugin code: **`Mosc`**.
  These cannot change after a public release.
- Licence posture: proprietary / all rights reserved, same as Clear Space. Third-party
  code must be under a permissive licence (see `CLAUDE.md`).
- Formats: VST3, AU (v2), Standalone. macOS universal binary (arm64 + x86_64).
- **Plugin category: audio effect** (AU `aufx`, VST3 `Fx`), one identity, one binary.
  Decided at the M1 gate. The Send hub is a per-binary singleton, so a second
  (instrument) binary would split it; one binary keeps Sends reaching every Mosaic.
- **Logic placement rule.** Logic does not process audio effects on an audio track that
  has no regions playing (measured, M1). Send instances are unaffected (their tracks have
  audio). The Mosaic-role instance must sit on an **Aux channel strip** (or in the audio
  FX slot of an instrument track), which Logic processes continuously. Live and Reaper
  keep processing effects on empty tracks. Goes in the user guide and the help overlay.
  If the Aux placement turns out not to work, the fallback is a shared-memory hub
  (`shm_open`) so an instrument build can be shipped alongside the effect.

## 9. UI (v1)

Single resizable window. Clean, flat, minimal; one accent color per slot.

- **Top bar:** Rate · Mode (Random/Sequence) · Position (Locked/Free) · Fade ·
  Tile length · Seed/Reroll · Pattern length · **Key (Auto / root + scale)** ·
  Output gain · Role switch (Mosaic/Send).
- **Slot area:** 8 rows (or a 2×4 grid). Each slot shows its color, source selector,
  a waveform overview with a playhead (File) or a level meter (Live), gain, mute, weight,
  and an "active" light that fires when that slot's tile is playing. Drop target for files.
  File slots also show a **key badge** (detected key, dimmed when confidence is low,
  click to override), a Snap/Original toggle, the resulting shift in semitones, and a
  render-progress indicator.
- **Mosaic strip:** a horizontal lane across the bottom showing the tiles as colored
  blocks — the last bar or two played and the next bar or two coming (possible because
  the pattern is deterministic). In Sequence mode this lane is the editable step sequencer.
- **Send role view:** a deliberately tiny UI — Group, Send number, input meter, mute-output
  toggle, and a status line ("Connected to Mosaic in Group A" / "No Mosaic in this group").

Milestones before M7 use a plain generic/placeholder UI. Don't polish early.

## 10. State and presets

- Everything in sections 4.3–4.7, the sequence, slot sources, file paths, role, group,
  send number, the global Key, and each slot's key data (detected key, confidence,
  override, Key mode, Direction, Transpose, Fine) are saved and restored with the DAW
  session.
- State is versioned (`stateVersion` attribute) so later builds can migrate old sessions.
- A preset system beyond the host's own is a later item.

## 11. Quality bar

- No allocation, locks, file I/O, or logging on the audio thread.
- No clicks at tile boundaries at default fade; no clicks on loop wrap, locate, bypass,
  or parameter changes.
- Deterministic: two renders of the same session region are bit-identical.
- Passes `auval` and `pluginval` at strictness level 10.
- Works at 44.1–192 kHz and buffer sizes 32–4096, including hosts that vary block size.
- CPU: negligible. Mosaic is a switcher, not a processor.

## 12. Out of scope for v1 (parking lot)

Not to be built until v1 ships. Listed so ideas have a home.

- Time-stretching or per-file BPM
- Reverse tiles, stutter/repeat, per-step rate, swing
- Pitch-shifting live inputs in real time
- Automating key changes mid-song; instant (real-time) key changes
- Formant-preserving shift, automatic tuning (cents) correction
- Reading key hints from file names ("Bass_Am_120.wav")
- Disk cache of rendered pitch-shifted files (faster session reload)
- Scale-aware retuning beyond simple transposition
- Overlapping/polyphonic tiles
- Rolling buffer on live inputs (would enable Free mode and "pull from earlier" on live sources)
- Capture: bounce N bars of the mosaic to a WAV and drag it into the DAW
- MIDI: notes select slots or trigger tiles
- Run while the transport is stopped (free-running jam mode)
- Disk streaming for long files
- Cross-process Send transport (shared memory) for sandboxed hosts
- Windows, AAX / Pro Tools, CLAP
- Licensing / copy protection, installer branding, Lemon Squeezy integration

## 13. Open questions for Seth

| # | Question | Default if unanswered |
|---|---|---|
| 1 | ~~One plugin or separate Send plugin?~~ | **Answered:** one plugin, two roles |
| 2 | ~~Manufacturer code?~~ | **Answered:** `Rmsl` (from the Clear Space project), plugin code `Mosc` |
| 3 | When the transport is stopped, silence — or pass Input 1 through? | Silence |
| 4 | Is 64 steps enough for the sequencer? | Yes |
| 5 | Is Mosaic a product to sell or a personal tool first? | Built to be sellable (clean licences, signing in M8); selling is a later decision |
| 6 | Should Snap treat relative major/minor as "already in key" (shift 0)? | Yes |
| 7 | Global Key defaults to Auto (first pitched file sets it)? | Yes |
| 8 | New files default to Snap when detection is confident? | Yes |
| 9 | While a newly imported file is still rendering, should its slot be silent, or play at original pitch until ready? | Silent |
| 10 | ~~Plugin category?~~ | **Answered (M1):** audio effect; Mosaic instance on an Aux strip in Logic |
| 11 | ~~Send look-behind?~~ | **Answered (M1):** 2 × host block size, min 256; floor re-checked at M5 |

## 14. Change log

- **v0.4 — 2026-11-16** — M1 decision gate: section 5 rewritten from measurements
  (`docs/ROUTING_RESULTS.md`); look-behind rule decided (5.3); plugin category decided as
  audio effect with the Logic Aux placement rule (8); questions 10–11 answered.
- **v0.3 — 2026-11-16** — M0 review: the Send look-behind `L` must be measured under
  delay compensation (5.3), and the offline-render wait is recorded as the one explicit
  exception to the real-time rules (7.6). No behavior change.
- **v0.2 — 2026-11-16** — Added key detection and snap-to-key for File slots (new
  section 7; later sections renumbered). Decisions 5–9 confirmed. Manufacturer code set
  to `Rmsl`, plugin code `Mosc`. Pitch shifting moved out of the parking lot.
- **v0.1 — 2026-11-16** — First draft from the planning conversation. Archived at
  `docs/archive/SPEC_v0.1.md`.
