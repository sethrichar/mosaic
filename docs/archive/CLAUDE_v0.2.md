# CLAUDE.md — Mosaic

Rules and context for Claude Code working in this repo. Read this, then `docs/SPEC.md`,
then `docs/BUILD_PLAN.md`, before changing anything.

**Doc version:** v0.2 (2026-11-16) — previous: `docs/archive/CLAUDE_v0.1.md`

## What this is

Mosaic is a JUCE audio plugin (VST3 / AU / Standalone, macOS) that chops up to 8 sources
— loaded files or live tracks — on a tempo-synced grid and plays one tile at a time in
random or sequenced order. Imported files get their key detected and can be snapped to
the mosaic's key. Full behavior is in `docs/SPEC.md`.

- Owner: Seth Richardson (Richardson Media Solutions LLC)
- Remote: https://github.com/sethrichar/mosaic.git

## How to work with Seth

- Follow `docs/BUILD_PLAN.md` one milestone at a time. Stay inside the current milestone.
  Ideas outside it go in the parking lot (SPEC section 12), not into code.
- End every working session and every milestone with a numbered **"What I need from you"**
  list: specific, concrete actions (what to open, what to click, what to listen for, what
  to report back). No open-ended summaries.
- Seth is an experienced audio engineer (strong on signal flow, routing, DAWs) and has
  built plugins before. Explain C++/build-system decisions briefly; don't explain audio.
- When something in the spec is wrong or blocked, stop and say so with a proposed fix.
  Don't quietly work around it.

## Version control rules (important)

1. **Never delete work.** Don't delete files, branches, or tags to "clean up". Superseded
   docs move to `docs/archive/` with their version in the filename (e.g.
   `SPEC_v0.1.md`). Superseded code stays reachable through git history and tags.
2. **Version numbering:**
   - Working/internal revisions increment by decimal: v0.1, v0.2 … v1.1, v1.2.
   - Only builds or documents published/exported for other people increment by whole
     number: v1, v2, v3.
3. **Tag every milestone** (`v0.1`, `v0.2` …) and push tags.
4. **`CHANGELOG.md`** gets an entry for every tag: what changed, what was decided, and how
   to get back to the previous version (the tag name).
5. Each doc in `docs/` carries its own version line and change log at the bottom. Bump it
   when the content changes meaningfully.
6. Commit small and often with clear messages. Never force-push. Never rewrite history on
   `main`. Use a branch per milestone (`m1-routing-spike`), merge to `main` when its
   acceptance checks pass.
7. Plugin state has a `stateVersion`. Once a build has been used in a real session, later
   builds must still load its state.

## Tech stack

- C++20, CMake (≥ 3.24), JUCE as a git submodule in `libs/JUCE`.
- **JUCE version:** pin the latest stable release tag (9.x as of Nov 2026).
  Pinned tag: _(fill in at M0)_
- Formats: VST3, AU (v2 — not AUv3; the Send hub relies on in-process instances), Standalone.
- macOS universal binary (arm64 + x86_64). Deployment target: decide at M0, record here.
- Tests: Catch2. Validation: `auval`, `pluginval`.
- Plugin identity: company `Richardson Media Solutions`, manufacturer code `Rmsl` (shared
  with Seth's other plugins — do not change), plugin code `Mosc`, bundle ID
  `com.richardsonmediasolutions.mosaic`.
- Pitch shifting: Signalsmith Stretch (MIT) in `libs/`, pinned commit: _(fill in at M4)_

## Third-party code and licences

Mosaic is proprietary (all rights reserved) and may be sold.

- Only permissively licensed dependencies: MIT, BSD, ISC, zlib, Boost, Apache-2.0.
- **No GPL, LGPL or AGPL code**, and no code copied or adapted from such projects
  (this includes libKeyFinder, Essentia, Rubber Band's GPL edition, SoundTouch).
  If one looks like the only option, stop and ask Seth.
- Every dependency is pinned to an exact tag or commit and listed in
  `THIRD_PARTY_LICENSES.md` with its licence text.

## Repo layout

```
CLAUDE.md
README.md
CHANGELOG.md
THIRD_PARTY_LICENSES.md
CMakeLists.txt
docs/
  SPEC.md
  BUILD_PLAN.md
  ROUTING_TEST.md        (M1)
  ROUTING_RESULTS.md     (M1)
  archive/               (superseded doc versions — never deleted)
libs/JUCE/               (submodule)
libs/signalsmith-stretch/  (M4, pinned)
source/
  engine/                (pure C++: grid, chooser, slice position, tile renderer — no JUCE plugin deps)
  analysis/              (pure C++: key detector, snap math, PitchShifter interface) (M4)
  sources/               (SlotSource implementations: file, input bus, send)
  hub/                   (inter-instance Send hub)
  plugin/                (AudioProcessor, parameters, state)
  ui/                    (editor and components)
tests/
tools/                   (keydetect command-line tool) (M4)
test-audio/              (git-ignored; Seth's local test files — never commit audio)
scripts/                 (build.sh, validate.sh)
```

## Commands

_(Claude Code: fill these in at M0 and keep them current.)_

- Build + install: `scripts/build.sh`
- Validate: `scripts/validate.sh`
- Tests: `ctest --test-dir build --output-on-failure`

## Real-time audio rules (non-negotiable)

On the audio thread (`processBlock` and anything it calls):

- No memory allocation or deallocation. No `std::vector` growth, no `juce::String`,
  no `std::function` captures that allocate, no smart-pointer releases of large objects.
- No locks, no waiting, no file or network I/O, no logging, no Objective-C messaging.
- Communicate with other threads through atomics and lock-free FIFOs only.
- Large objects (file buffers) are swapped in by pointer and released on another thread.
- Handle any block size, including 0 and sizes larger than `prepareToPlay` promised.
- Never assume the playhead is valid; handle a missing position gracefully (silence).
- Denormals: use `juce::ScopedNoDenormals`.

Determinism is a feature: the same session region must render bit-identically every time
(SPEC 4.3, section 11). Never use unseeded randomness or wall-clock time in the audio path.
This includes background renders: a pitch-shifted buffer must come out identical every time.

## Engine boundaries

- `source/engine/` must compile and test without the JUCE plugin modules. Keep host and
  UI concerns out of it. It receives time (ppq, bpm, sample position) and returns audio.
- All audio sources go through the `SlotSource` interface. The engine must not know
  whether a slot is a file, a native bus, or a Send.
- `source/analysis/` follows the same rule as the engine: plain C++, unit-tested, no
  plugin or UI dependencies. Key detection and pitch shifting never run on the audio
  thread; the engine only ever sees a finished buffer.

## Definition of done (every milestone)

- Acceptance checks in the build plan pass.
- `scripts/validate.sh` is green.
- `CHANGELOG.md` updated, milestone tagged and pushed.
- "What I need from you" list delivered to Seth.

## Don't

- Don't add time-stretching, real-time pitch shifting, AAX, Windows, licensing, or
  anything else in SPEC section 12 without Seth explicitly pulling it into a milestone.
- Don't polish the UI before M7.
- Don't change parameter IDs, plugin codes, or the bundle ID once M8 freezes them.
- Don't commit audio files to the repo.
- Don't delete anything (see version control rules).
