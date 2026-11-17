# Changelog

Every tag gets an entry: what changed, what was decided, and how to get back to the
previous version. Working builds use decimal versions (v0.1, v0.2 …); only builds handed
to other people get whole numbers (v1, v2 …). See `CLAUDE.md`, "Version control rules".

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
