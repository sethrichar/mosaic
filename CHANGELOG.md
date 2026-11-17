# Changelog

Every tag gets an entry: what changed, what was decided, and how to get back to the
previous version. Working builds use decimal versions (v0.1, v0.2 …); only builds handed
to other people get whole numbers (v1, v2 …). See `CLAUDE.md`, "Version control rules".

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
