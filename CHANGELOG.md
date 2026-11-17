# Changelog

Every tag gets an entry: what changed, what was decided, and how to get back to the
previous version. Working builds use decimal versions (v0.1, v0.2 …); only builds handed
to other people get whole numbers (v1, v2 …). See `CLAUDE.md`, "Version control rules".

## v0.4 — 2026-11-16 — M3: file mosaic (first playable build)

**What changed**
- **It plays.** Drop up to 8 files on the slots, press play in the host, and the engine
  switches between them on the grid. Random mode with all SPEC 4.6/4.7 controls except
  the key controls (M4) and the slot Source selector (M5).
- `source/sources/FileSlotSource` (plain C++): the audio thread owns the current buffer
  outright; new buffers arrive through a lock-free queue and old ones go back through
  another, so nothing is allocated, freed or shared across threads on the audio thread.
  Superseded buffers that never reached the audio thread are dropped immediately.
- `source/sources/FileLoader` (JUCE): background thread decodes WAV/AIFF/FLAC/MP3/
  CoreAudio formats, keeps the first two channels (mono to both), resamples once to the
  session rate with a windowed-sinc interpolator, builds a 1024-bin waveform overview,
  delivers on the message thread. Stale results (slot reloaded meanwhile) are dropped.
  Files are re-resampled automatically when the session sample rate changes.
- Parameters: `rate`, `mode`, `position`, `fade`, `tileLength`, `seed`, `patternLength`,
  `avoidRepeats`, `outputGain`, and per slot `slotNGain`, `slotNMute`, `slotNWeight`,
  `slotNStartOffset` (bars), `slotNLoop`. Pattern length and start offset use the host
  time signature. The M1 probe parameters remain; `probeSource` now defaults to None,
  which means "the engine".
- State version **2**: parameters plus a `files` child with each slot's absolute path.
  A missing file restores as "MISSING — relink" on that slot; other slots load normally.
  Version 1 state still loads.
- Standalone build: a simple transport (Play/Stop, Rewind, BPM) feeds the engine when
  there is no host playhead (SPEC 4.1).
- Placeholder UI (not the M7 design): global controls, Reroll, 8 slot rows with Load /
  Clear / drop target, waveform overview with playhead, active light, per-slot gain /
  mute / weight / offset / loop; memory readout with the 1 GB warning; status line; the
  M1 probe controls in the bottom strip. Dropping several files fills consecutive slots.
- Tests: `test_file_slot_source.cpp` (queue hand-off, replacement, concurrent
  publish/read), `test_plugin_mosaic.cpp` (loader decode/downmix/resample, state round
  trip with identical renders, missing file, offline bounce and variable block
  bit-identity, mute and gain). `test_plugin_routing.cpp` rigs now use distinct Send
  channels so they are independent in one process.

**Decisions**
- A replaced file swaps at a block boundary, not a tile boundary; replacing a file is a
  user action and a crossfade is not expected. Tile-boundary swaps are M4 (renders).
- Sequence mode is selectable but its lane is not editable until M6 (default: slots 1–8
  in order).
- The probe (M1) stays in the build until M5 so the null test can be repeated.

**Previous version:** `v0.3` (`git checkout v0.3`).

## v0.3 — 2026-11-16 — M2: engine core (headless)

**What changed**
- `source/engine/` is now the mosaic brain, plain C++ with no JUCE: `Rate` (18 grid
  rates), `GridClock` (sample-exact step boundaries, snap epsilon 1e-6 samples),
  `StepChooser` (weighted seeded random with pattern length and stateless avoid-repeats;
  sequence mode with rests and "?"), `SlotSource` interface, `SlicePosition` (Locked with
  start offset and loop, Free with hashed slices), `TileRenderer` (one tile at a time,
  crossfade at every edge, gate, same-slot seamless join in Locked mode).
- The renderer is stateless with respect to the timeline: the output of any sample is a
  pure function of host time and configuration, so block splits, locates and offline
  bounces render bit-identically. Blocks larger than `prepare()` are chunked.
- 31 engine tests (`tests/test_grid_clock.cpp`, `test_step_chooser.cpp`,
  `test_slice_position.cpp`, `test_tile_renderer.cpp`), covering every M2 acceptance
  check: boundaries at 44.1/48/96 kHz, odd tempos, block sizes 1–4096; bars 5–8 alone
  equal bars 1–8; weights within 3% over 10,000 steps; muted/empty never chosen;
  click bound at every tile edge; variable block sizes bit-identical.
- `tools/engine_demo`: renders a test-tone mosaic to a WAV without a host.

**Decisions**
- Avoid-repeats is stateless by walking a chain that restarts every 64 steps; a repeat
  can only slip through at a restart point. Exact enough, and seekable.
- Crossfades are linear and clamped to half of either neighbouring step. A gated tile
  (< 100%) fades out inside its own step instead of crossfading into the next one.
- Live sources read "now" in both position modes; Free mode falls back to Locked.
- Pattern length and start offset in bars are converted to steps/samples by the plugin
  layer (M3) using the host time signature; the engine only sees steps and samples.

**Previous version:** `v0.2` (`git checkout v0.2`).

## v0.2 — 2026-11-16 — M1: routing spike (probe build)

**What changed**
- `source/hub/SendHub` — process-wide, lock-free Send hub (SPEC 5.3): one timeline-tagged
  stereo ring per (Group, Send number), 524288 samples (> 2 s at 192 kHz), lazily
  allocated on the message thread, single producer / single consumer, collision counter.
  The valid region is tracked as [tail, head): a locate or loop wrap on the Send side
  invalidates everything before the jump, so stale audio is never returned (SPEC 5.3
  item 4). Plain C++, unit-tested (`tests/test_send_hub.cpp`).
- Probe readout *"Smallest look-behind with no underruns so far"*: the largest
  `blockSize − (sendHead − T)` seen, i.e. the look-behind the host actually needs.
- Processor: 8 stereo input buses (`Input 1` enabled, `Input 2`–`8` optional, mono or
  stereo accepted), **Role** (Mosaic / Send), **Group** (A–D), **Send #** (1–8),
  **Send: mute output**, **Probe source** (None / Input N / Send N), **Probe look-behind**
  (0–16384 samples, default 2048, reported as latency only while a Send source is probed).
  Sends transmit only while the transport is playing.
- Probe UI: meters for Input 1–8 and Send 1–8, playhead readouts, block size,
  "processBlock BEING CALLED / NOT BEING CALLED", reported latency, Send skew min/max,
  underrun and collision counters, Reset stats.
- Second plugin target **Mosaic Inst Probe** (`MosI`, AU instrument / VST3 Instrument)
  built from the same sources, for BUILD_PLAN M1 task 6.
- In-process plugin tests (`tests/test_plugin_routing.cpp`): Send → Mosaic nulls
  sample-exactly in both processing orders with look-behind ≥ block size, variable block
  sizes, zero look-behind underruns, stopped transport, all 8 buses, mono input, state.
- `docs/ROUTING_TEST.md` (Seth's click-by-click steps) and `docs/ROUTING_RESULTS.md`
  (empty results table). `scripts/validate.sh` validates both plugin targets.
- Parameters live in an `AudioProcessorValueTreeState`; state root stays `MosaicState`,
  `stateVersion` stays 1 (v0.1 state had no parameters and still loads).

**Decisions**
- Collision handling for M1: counter per channel, last writer wins, warning shown in
  both UIs for 2 s after each collision. SPEC 5.3 item 5 is met; M5 may refine.
- Look-behind is user-selectable in the probe so Seth can find the smallest value with
  zero underruns per host; the shipping default is set at the M1 decision gate.

**Validation notes**
- `auval -strict` and `pluginval` strictness 10 pass for Mosaic and Mosaic Inst Probe,
  AU and VST3. pluginval's AU host logs two warnings on both AUs ("Current program is
  -1" and "Disabling non-main buses failed"); the VST3s are clean and the AU default
  layout restores correctly. Revisit at M8.

**Previous version:** `v0.1` (`git checkout v0.1`).

## v0.1 — 2026-11-16 — M0: repo and skeleton

**What changed**
- Repo initialised; CMake project with JUCE `9.0.5` and Catch2 `v3.16.1` as pinned
  submodules in `libs/`.
- `Mosaic` plugin target: VST3, AU (v2, `aufx`), Standalone. Universal binary
  (arm64 + x86_64), deployment target macOS 11.0, C++20. Identity per SPEC section 8
  (`Rmsl` / `Mosc`, `com.richardsonmediasolutions.mosaic`).
- Stereo (or mono-in / stereo-out) pass-through processor with versioned empty state
  (`stateVersion` = 1). Placeholder editor.
- `mosaic_engine` static library (plain C++, no JUCE) with one Catch2 smoke test.
- `scripts/build.sh` (configure, build, test, install) and `scripts/validate.sh`
  (`auval` + `pluginval`).
- Docs bumped to v0.3: offline-render wait exception (SPEC 7.6 / CLAUDE.md real-time
  rules), Send look-behind must be measured under delay compensation (SPEC 5.3, M1 task).

**Decisions**
- JUCE licence: Starter tier (free; annual revenue cap applies). JUCE 9 has no splash screen.
- Plugin category starts as an audio effect; M1 decides whether that stays.
- Catch2 is a submodule rather than a Homebrew package so clean clones build offline.

**Previous version:** none (first tag).
