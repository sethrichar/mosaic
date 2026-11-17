# Mosaic — Build Plan

**Doc version:** v0.4 (working draft, 2026-11-16) — previous: `docs/archive/BUILD_PLAN_v0.3.md`
**Read first:** `CLAUDE.md`, then `docs/SPEC.md`.

## How to use this plan

- Work **one milestone at a time, in order**. Do not start the next milestone until the
  current one's acceptance checks pass and Seth has done his checkpoint.
- Every milestone ends with: all checks green, `CHANGELOG.md` updated, a git tag, a push,
  and a short **"What I need from you"** list for Seth (numbered, concrete, testable).
- If something in the spec turns out to be wrong or impossible, stop and say so. Propose
  the change, and update `docs/SPEC.md` (bump its version) only after Seth agrees.

## Milestone overview

| # | Milestone | Outcome | Seth's part | Tag |
|---|---|---|---|---|
| M0 | Repo + skeleton | Empty plugin builds, validates, loads in a DAW | Confirm it loads | v0.1 |
| M1 | Routing spike | Hard evidence of what each DAW can route | ~30 min test matrix | v0.2 |
| M2 | Engine core | Grid + selection + tile renderer, fully unit-tested | None | v0.3 |
| M3 | File mosaic | **First playable build:** load stems, press play | Make a mosaic, give notes | v0.4 |
| M4 | Key detect + snap | Files detect their key and snap to the mosaic's key | Supply test files; judge quality | v0.5 |
| M5 | Live switcher | Live slots via native buses and Send | Live-switch test in 3 DAWs | v0.6 |
| M6 | Sequence mode + strip | Step sequencer and tile preview lane | Musical test | v0.7 |
| M7 | Real UI | The designed interface | Design review | v0.8 |
| M8 | Hardening + release | Validated, signed, notarized installer | Final sign-off | v1 |

Working builds use decimal versions (v0.1, v0.2 …). Only a build handed to other people
gets a whole number (v1, v2 …). Nothing is deleted — see `CLAUDE.md`.

---

## M0 — Repo and skeleton

**Goal:** a do-nothing stereo plugin that builds and validates, so every later milestone
starts from a green baseline.

Tasks
1. `git init` in the project folder, add remote `https://github.com/sethrichar/mosaic.git`,
   default branch `main`. If the remote already has commits, pull first; never force-push.
2. Check the toolchain: Xcode + command line tools, CMake ≥ 3.24, Ninja (optional).
   Report anything missing as an action item rather than working around it.
3. CMake project using JUCE as a git submodule in `libs/JUCE`, pinned to the **latest
   stable JUCE release tag** (JUCE 9.x at time of writing — confirm on the JUCE GitHub
   releases page and record the exact tag in `CLAUDE.md`).
4. `juce_add_plugin` target: VST3, AU, Standalone. Universal binary. C++20.
   Plugin identity per SPEC section 8 (manufacturer `Rmsl`, plugin `Mosc`).
5. Repo layout per `CLAUDE.md`. Add `.gitignore`, `README.md`, `CHANGELOG.md`.
6. Test target with Catch2 (one trivial passing test).
7. Scripts: `scripts/build.sh` (configure + build + install to the user plugin folders),
   `scripts/validate.sh` (runs `auval` and `pluginval`).
8. Include `CLAUDE.md`, `docs/SPEC.md`, `docs/BUILD_PLAN.md` and `docs/archive/` in the
   first commit unchanged.

Acceptance
- `scripts/build.sh` succeeds from a clean clone.
- `auval` passes; `pluginval` passes at strictness 5 or higher.
- Unit test target runs and passes.

Seth checkpoint
1. Open Live, Logic and Reaper; confirm "Mosaic" appears and loads on a track.

---

## M1 — Routing spike (do this before any real DSP)

**Goal:** replace assumptions in SPEC section 5 with measurements. This milestone decides
the plugin category and confirms the Send design. The code is exploratory but stays in
the repo (it becomes the basis for M5).

Tasks
1. Add 7 optional stereo input buses (`Input 2`–`Input 8`) alongside the main input.
   Implement `isBusesLayoutSupported` so plain stereo still loads everywhere.
2. Add the **Role** switch (Mosaic / Send), **Group** (A–D), **Send number** (1–8).
3. Implement a first version of the Send hub (SPEC 5.3), including timeline-tagged
   blocks and the look-behind + reported-latency scheme. The probe records, per block,
   the gap between the newest Send write position and the Mosaic read position, so the
   look-behind `L` is sized from measured skew (including under delay compensation),
   not from an assumed block size.
4. Build a **probe UI**: a meter per native input bus, a meter per Send in the current
   group, the reported latency, the host block size, and live playhead readouts
   (isPlaying, ppq, timeInSamples, bpm).
5. **Probe passthrough:** a menu to route exactly one source (`Input N` or `Send N`)
   straight to the output, for null testing.
6. Log (off the audio thread) whether `processBlock` is called when the track has no
   audio, in each host, with the plugin built as an effect and as an instrument.
   If needed, add a second build target for the instrument variant just for this test.
7. Write `docs/ROUTING_TEST.md`: exact click-by-click steps for Seth for each DAW, and
   an empty results table in `docs/ROUTING_RESULTS.md`.

Acceptance
- Probe build passes `auval` and `pluginval`.
- In the JUCE AudioPluginHost (or Reaper), all 8 native buses and all 8 Sends show
  signal, and the Send null test cancels to silence.

Seth checkpoint (~30 minutes, steps in `docs/ROUTING_TEST.md`)
1. In **Live, Logic, Reaper**: how many native input buses can you actually feed? (meters)
2. In each: put Send on 8 tracks — do all 8 meters move in the Mosaic instance?
3. In each: run the **null test** at buffer sizes 64, 512, 2048, in real time and in an
   offline bounce. Record pass/fail.
4. In each: does the plugin keep running on a track with no audio clips while the
   transport plays? (effect build vs instrument build)
5. Fill in `docs/ROUTING_RESULTS.md` (or just tell Claude Code the results and it fills it in).

Decision gate — **done 2026-11-16, approved by Seth** (`docs/ROUTING_RESULTS.md`):
- Plugin category: **audio effect**, one binary. Logic: Mosaic instance on an Aux strip.
- Send **is** the primary live transport in Live and Logic. Native buses: 2 / 2 / 8.
- Look-behind: 2 × host block size, min 256, recomputed on `prepareToPlay`.
- SPEC updated to v0.4. Still to measure at the M5 checkpoint: the Logic Aux placement
  and each host's "smallest look-behind" readout.

---

## M2 — Engine core (headless)

**Goal:** the mosaic brain as a plain C++ library with no JUCE plugin dependencies, so it
can be unit-tested hard.

Tasks
1. `engine/GridClock` — from (ppq, bpm, sample rate, rate setting) produce the step index
   and the exact sample offsets of every step boundary inside a block.
2. `engine/StepChooser` — Random mode (weights, seed, pattern length, avoid repeats,
   stateless hashing) and Sequence mode (steps, rests, "?"). Skips empty/muted slots.
3. `engine/SlicePosition` — Locked and Free position math for File slots.
4. `engine/TileRenderer` — pulls audio from the chosen `SlotSource`, applies tile-edge
   fades and tile length (gate), mixes the brief crossfade, applies slot gain.
5. `engine/SlotSource` interface with a test implementation (sine at a per-slot
   frequency, or a ramp) so tests can identify which slot and which position played.

Acceptance (all as automated tests)
- Step boundaries are sample-exact for every rate at 44.1/48/96 kHz, odd tempos (e.g.
  93.7 BPM), and block sizes 1, 32, 512, 4096.
- Same (seed, step) → same slot, always. Rendering bars 1–8 in one pass equals rendering
  bars 5–8 alone, sample for sample.
- Weights: over 10,000 steps the distribution matches the weights within tolerance.
- Muted/empty slots are never chosen; all-empty produces silence without crashing.
- No discontinuity larger than the fade allows at any tile edge (click test).
- Variable block sizes produce bit-identical output to a single large block.

Seth checkpoint: none. (Optional: listen to the test-tone render Claude Code exports.)

---

## M3 — File mosaic (first playable build)

**Goal:** Seth can drop 2–8 stems in, press play, and hear a mosaic.

Tasks
1. `FileSlotSource`: background loading, resampling to session rate, lock-free buffer
   swap, mono/multichannel handling (SPEC section 6).
2. Wire the engine into the processor with all parameters in an
   `AudioProcessorValueTreeState` (SPEC 4.6, 4.7). Random mode only for now; the key
   controls in those tables arrive in M4.
3. Locked and Free positions, per-slot Start offset and Loop.
4. State save/restore including file paths; missing-file relink state.
5. Placeholder UI: 8 drop targets with file name, basic waveform overview, active light,
   plus generic controls for the parameters. Functional, not pretty.
6. Memory readout and the 1 GB warning.

Acceptance
- Unit tests for file position math (loop wrap, negative offset, file shorter than a step).
- Loading and replacing files during playback never glitches or blocks the audio thread.
- Save session → quit → reopen restores everything and sounds identical.
- Offline bounce is bit-identical to a second offline bounce.
- `auval` + `pluginval` pass.

Seth checkpoint
1. Load 4–8 stems from one song in Live. Try Locked at 1/8 and 1/16, then Free.
2. Tell Claude Code: does it feel like the mosaics you build by hand? What's missing
   that you reached for? (This feedback can reorder later milestones and the parking lot.)
3. Listen for clicks at tile edges with Fade at default and at 0.

---

## M4 — Key detection and snap-to-key

**Goal:** imported files show their key and can be snapped to the mosaic's key or left
at original pitch (SPEC section 7). File slots only.

Tasks
1. `analysis/KeyDetector` — plain C++, no plugin dependencies. Returns root, scale and
   confidence, or "none" for unpitched material (SPEC 7.5). No GPL/AGPL code.
2. `analysis/KeySnap` — the snap math: signature-based shift, Direction
   (Nearest/Up/Down), plus Transpose and Fine (SPEC 7.4).
3. `PitchShifter` interface with a Signalsmith Stretch implementation. Add the library
   under `libs/`, pin the exact commit, and record its licence in
   `THIRD_PARTY_LICENSES.md`.
4. Extend the file pipeline: load → resample → detect key → (if needed) render shifted
   buffer → lock-free swap. Re-render when Key, Source key, Key mode, Direction,
   Transpose or Fine change; debounce and cancel superseded renders (SPEC 7.6).
5. Global Key with Auto and Manual. Key data saved in state; no re-detection on restore.
6. Offline-bounce safety: when `isNonRealtime()` is true, wait for pending renders.
7. Placeholder UI: global Key selector; per-slot key badge with confidence, override
   menu, Snap/Original toggle, resulting shift readout, Transpose/Fine, progress indicator.
8. `tools/keydetect` — a small command-line tool that prints the detected key and
   confidence for every file in a folder, and scores them against an answer sheet.
9. Create `test-audio/key-set/` (git-ignored — never commit Seth's audio) with a
   `keys.csv` template: `filename, key`.

Acceptance (automated unless noted)
- Snap math: table-driven tests covering all 24×24 key pairs, relative keys (shift 0),
  the tritone case, and all three Directions.
- Detector: synthesized test material (scales and chord progressions) in all 24 keys is
  identified correctly; white noise and a drum-only test signal return "none".
- Detector on Seth's set (`tools/keydetect`): report exact-match and
  same-signature-match rates. Target: same-signature match on at least 75% of pitched
  files, and no unpitched file reported with high confidence. Misses are listed by name.
- Shifter pitch accuracy: a 440 Hz sine shifted +3 semitones measures 523.25 Hz ± 3 cents.
- Shifter alignment: a click track shifted +3 semitones keeps every click within 1 ms of
  its original position; output length equals input length exactly.
- Shift of 0 is bit-identical to the original buffer.
- Determinism: rendering the same file and settings twice is bit-identical.
- Changing Key during playback: no click, no dropout; swap happens on a tile boundary.
- An offline bounce started immediately after session load contains the shifted audio.
- RAM for a snapped slot is no higher than for an unsnapped one once rendering finishes.
- `auval` + `pluginval` pass.

Seth checkpoint
1. **Before this milestone starts:** copy 10–20 audio files whose key you already know
   into `test-audio/key-set/` and fill in `keys.csv` (file name, key). Include a few
   drum-only loops and at least 3 bass or vocal stems.
2. Load stems from two different songs in different keys. Set both to Snap. Does the
   result sound in key? Try Original on one and confirm it runs free.
3. Listen to shift quality at small (±1–3) and large (±5–6) intervals on a vocal, a bass,
   and a full mix. Report: acceptable / not acceptable for each.
4. Note any file where the detected key was wrong and what it should have been.

Decision gate
- If shift quality is not good enough, Claude Code proposes an upgrade path (Rubber Band
  or élastique, both paid licences) with cost and effort, and Seth decides.
- If detection accuracy misses the target, tune key profiles against Seth's set before
  moving on.

---

## M5 — Live switcher

**Goal:** live slots, using whatever M1 proved works in each host.

Tasks
1. `InputBusSlotSource` (native buses) and `SendSlotSource` (hub), behind `SlotSource`.
2. Harden the hub to the SPEC 5.3 requirements: discontinuities, collisions, removal of
   a Send mid-playback, latency reported only when a Send source is in use. Latency
   rule from the M1 gate: 2 × block size, min 256, recomputed on `prepareToPlay`.
3. Per-slot Source selector (File / Input N / Send N). Mixed File + Live slots.
4. Send-role UI (tiny) with connection status and collision warning.
5. Level meters on live slots.

Acceptance
- Null test still cancels with the full engine in the path (single slot, weight 100%).
- Loop wrap and locate while live-switching: no clicks, no stale audio.
- Mixed session (4 files + 4 live) stays grid-aligned; verified with a click track on
  both a file slot and a live slot.
- Changing host buffer size or sample rate mid-session recovers cleanly.
- `auval` + `pluginval` pass.

Seth checkpoint
1. In each DAW: route 8 tracks in (per the M1 results), press play, confirm switching is
   tight to the grid by ear and by recording the output and looking at the tile edges.
2. Bounce offline in each DAW and compare with what you heard.
3. Left over from M1: in Logic, confirm the Mosaic instance keeps running on an Aux
   channel strip with the transport playing. In each DAW, read the probe's "smallest
   look-behind with no underruns" at buffer 64 and 2048 so the 2× rule can be checked.

---

## M6 — Sequence mode and the mosaic strip

Tasks
1. Sequence mode end to end: 1–64 steps, slot / rest / "?", saved in state.
2. Mosaic strip: colored tile lane showing recent and upcoming tiles; in Sequence mode
   it is the step editor (click to cycle, drag to paint, right-click for rest/"?").
3. Reroll, Pattern length, Avoid repeats exposed and working with the strip preview.

Acceptance
- Sequence playback matches the lane exactly, including after locate and loop wrap.
- The "upcoming" tiles shown are the tiles that then play (deterministic preview).
- Editing the sequence during playback is glitch-free and thread-safe.

Seth checkpoint
1. Program a 16-step pattern across 4 slots and confirm it plays as drawn.
2. Decide: is anything from the parking lot (SPEC section 12) worth pulling in before UI work?

---

## M7 — Real UI

Tasks
1. Before coding: 2–3 static mockups (layout options) for Seth to pick from.
2. Build the chosen layout per SPEC section 9. Resizable. Slot colors. Retina-clean.
3. Waveform overviews with playheads; meters; active lights; drop feedback; key badges
   and render progress.
4. Tooltips or a one-screen help overlay explaining Send setup per DAW (from M1 results).

Acceptance
- UI never blocks or allocates on the audio thread; repaint is timer-driven from atomics.
- Opening/closing the editor repeatedly leaks nothing (checked with pluginval).
- All controls are reachable by automation and reflect automation.

Seth checkpoint
1. Pick a mockup (before build). 2. Design review of the built UI; list changes.

---

## M8 — Hardening and release

Tasks
1. `pluginval` strictness 10 and `auval` clean on both architectures.
2. Stress matrix: 44.1–192 kHz, buffers 32–4096, variable block size, bypass toggling,
   rapid automation, missing files, 8 long files (all snapped), rapid key changes,
   session reload ×20.
3. Freeze manufacturer/plugin codes (`Rmsl` / `Mosc`) and parameter IDs.
4. Code-sign and notarize with Seth's Apple Developer ID; build a `.pkg` installer.
5. `docs/USER_GUIDE.md`: setup per DAW, the Send workflow, key and Snap, known limitations.
   Ship `THIRD_PARTY_LICENSES.md` with the installer.
6. Tag **v1**.

Seth checkpoint
1. Provide/confirm the Developer ID certificates on the build machine.
2. Install from the `.pkg` on a clean user account; run one real session in each DAW.
3. Final sign-off.

---

## Kickoff prompt for Claude Code

Paste this as the first message in Claude Code, opened in the Mosaic project folder:

```
Read CLAUDE.md, docs/SPEC.md and docs/BUILD_PLAN.md in full before doing anything.
Then start Milestone M0 from the build plan. Before writing code, tell me:
(1) anything in the spec you think is wrong, risky or underspecified,
(2) what's missing from my toolchain, and
(3) the exact JUCE release tag you plan to pin.
Work one milestone at a time and stop at each "Seth checkpoint" with a numbered
"What I need from you" list.
```

## Change log

- **v0.4 — 2026-11-16** — M1 decision gate recorded; M5 inherits the latency rule and the
  two leftover measurements.
- **v0.3 — 2026-11-16** — M1 task 3 now measures Send/Mosaic timeline skew. M0 done
  (tag v0.1).
- **v0.2 — 2026-11-16** — Added M4 (key detection and snap-to-key); later milestones
  renumbered (Live switcher M5, Sequence M6, UI M7, Release M8). Plugin codes set.
- **v0.1 — 2026-11-16** — First draft. Archived at `docs/archive/BUILD_PLAN_v0.1.md`.
