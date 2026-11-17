# Mosaic — Product & Technical Spec

**Doc version:** v0.1 (working draft, 2026-11-16)
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

## 3. Decisions already made

Answered by Seth on 2026-11-16:

1. **Hosts:** Ableton Live, Logic Pro, Reaper. macOS first.
2. **Live mode = live switcher.** Mosaic jumps between incoming tracks on the grid as
   the session plays. Nothing is recorded.
3. **Slice position is switchable** between Locked and Free.
4. **Files are assumed to already be at session tempo.** No time-stretching in v1.

Assumed by Claude while drafting — **Seth to confirm or change** (see section 12):

5. One plugin binary with two roles (Mosaic / Send) rather than two separate plugins.
6. Any slot can be a File or a Live input; they can be mixed freely.
7. Random patterns are seeded and repeatable (same result every pass until "Reroll").
8. Formats: VST3 + AU (v2) + Standalone. No AAX / Pro Tools, no Windows in v1.
9. UI direction: clean and minimal, same family as Clear Space
   (Valhalla / Cradle / stock Ableton devices).

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

### 4.7 Global controls

Rate · Mode (Random / Sequence) · Position (Locked / Free) · Fade · Tile length ·
Seed + Reroll · Pattern length · Avoid repeats · Output gain.

All continuous and switch parameters are host-automatable via
`AudioProcessorValueTreeState`. Parameter IDs are permanent once a public build ships.

## 5. Live input — routing design

### 5.1 The problem

Getting 8 separate tracks into one plugin instance is a **host** limitation, not a
plugin-format limitation.

The AU API (`AUAudioUnitBus` / `AUAudioUnitBusArray` in AUv3, the element/bus scopes in
AUv2) lets a plugin publish as many input buses as it wants, and JUCE generates those
buses automatically from `BusesProperties` for both AU and VST3. The plugin side is easy.
What matters is how many of those buses each DAW will actually connect:

| Host | What it will feed a plugin | Source |
|---|---|---|
| Reaper | Any number of channels/buses via track channels + pin connector | Widely confirmed |
| Logic Pro | Main input + **one** sidechain, however many buses the AU publishes | SonoBus dev, JUCE forum |
| Ableton Live | Main input + sidechain. Reports that a third input bus is listed in routing but never receives audio | iPlug2 forum (Live 10); **unverified for Live 12** |

So native multi-bus input alone cannot deliver "8 live sources" in Live or Logic.
This is the single biggest technical risk in the project and is tested first
(Build Plan M1).

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
3. **Sample-accurate.** A null test (section 5.4) must cancel completely.
4. **Graceful discontinuities.** Loop wrap, locate, tempo change, a Send instance being
   removed or bypassed: at worst a short faded gap, never a click, glitch loop, or stale
   audio. A Send with no fresh data reads as silence.
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
- Plugin state stores each file's absolute path. On restore, a missing file shows a
  "missing — relink" state on that slot; nothing crashes and other slots still load.

## 7. Plugin identity and formats

- Plugin name: **Mosaic**. Company: **Richardson Media Solutions**.
- Bundle ID: `com.richardsonmediasolutions.mosaic`.
- Manufacturer code and plugin code: 4 characters each. **Must match the manufacturer
  code used by Seth's other plugins** (open question, section 12). These cannot change
  after a public release.
- Formats: VST3, AU (v2), Standalone. macOS universal binary (arm64 + x86_64).
- Plugin category: decided by the M1 spike. An audio effect is the natural fit for live
  switching, but some hosts stop calling effects on tracks with no audio, which would
  break file-only use. If so, ship as an instrument with audio inputs, or ship both.

## 8. UI (v1)

Single resizable window. Clean, flat, minimal; one accent color per slot.

- **Top bar:** Rate · Mode (Random/Sequence) · Position (Locked/Free) · Fade ·
  Tile length · Seed/Reroll · Pattern length · Output gain · Role switch (Mosaic/Send).
- **Slot area:** 8 rows (or a 2×4 grid). Each slot shows its color, source selector,
  a waveform overview with a playhead (File) or a level meter (Live), gain, mute, weight,
  and an "active" light that fires when that slot's tile is playing. Drop target for files.
- **Mosaic strip:** a horizontal lane across the bottom showing the tiles as colored
  blocks — the last bar or two played and the next bar or two coming (possible because
  the pattern is deterministic). In Sequence mode this lane is the editable step sequencer.
- **Send role view:** a deliberately tiny UI — Group, Send number, input meter, mute-output
  toggle, and a status line ("Connected to Mosaic in Group A" / "No Mosaic in this group").

Milestones before M6 use a plain generic/placeholder UI. Don't polish early.

## 9. State and presets

- Everything in sections 4.3–4.7, the sequence, slot sources, file paths, role, group,
  and send number are saved and restored with the DAW session.
- State is versioned (`stateVersion` attribute) so later builds can migrate old sessions.
- A preset system beyond the host's own is a later item.

## 10. Quality bar

- No allocation, locks, file I/O, or logging on the audio thread.
- No clicks at tile boundaries at default fade; no clicks on loop wrap, locate, bypass,
  or parameter changes.
- Deterministic: two renders of the same session region are bit-identical.
- Passes `auval` and `pluginval` at strictness level 10.
- Works at 44.1–192 kHz and buffer sizes 32–4096, including hosts that vary block size.
- CPU: negligible. Mosaic is a switcher, not a processor.

## 11. Out of scope for v1 (parking lot)

Not to be built until v1 ships. Listed so ideas have a home.

- Time-stretching or per-file BPM
- Pitch shifting, reverse tiles, stutter/repeat, per-step rate, swing
- Overlapping/polyphonic tiles
- Rolling buffer on live inputs (would enable Free mode and "pull from earlier" on live sources)
- Capture: bounce N bars of the mosaic to a WAV and drag it into the DAW
- MIDI: notes select slots or trigger tiles
- Run while the transport is stopped (free-running jam mode)
- Disk streaming for long files
- Cross-process Send transport (shared memory) for sandboxed hosts
- Windows, AAX / Pro Tools, CLAP
- Licensing / copy protection, installer branding, Lemon Squeezy integration

## 12. Open questions for Seth

| # | Question | Default if unanswered |
|---|---|---|
| 1 | One plugin with a Mosaic/Send role switch, or a separate "Mosaic Send" plugin? | One plugin, two roles |
| 2 | What 4-character manufacturer code do your other plugins use (Soft Delay, Clear Space)? | Claude Code proposes one; confirm before first public build |
| 3 | When the transport is stopped, silence — or pass Input 1 through? | Silence |
| 4 | Is 64 steps enough for the sequencer? | Yes |
| 5 | Is Mosaic a product to sell (signing, installer, licensing) or a personal tool first? | Personal tool first; signing/notarization in M7 |

## 13. Change log

- **v0.1 — 2026-11-16** — First draft from the planning conversation.
