# CLAUDE.md — Mosaic

Rules and context for Claude Code working in this repo. Read this, then `docs/SPEC.md`,
then `docs/BUILD_PLAN.md`, before changing anything.

## What this is

Mosaic is a JUCE audio plugin (VST3 / AU / Standalone, macOS) that chops up to 8 sources
— loaded files or live tracks — on a tempo-synced grid and plays one tile at a time in
random or sequenced order. Full behavior is in `docs/SPEC.md`.

- Owner: Seth Richardson (Richardson Media Solutions LLC)
- Remote: https://github.com/sethrichar/mosaic.git

## How to work with Seth

- Follow `docs/BUILD_PLAN.md` one milestone at a time. Stay inside the current milestone.
  Ideas outside it go in the parking lot (SPEC section 11), not into code.
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
- Plugin codes (provisional until M7): manufacturer `____`, plugin `____` _(fill in at M0;
  must match Seth's other plugins — ask him)_

## Repo layout

```
CLAUDE.md
README.md
CHANGELOG.md
CMakeLists.txt
docs/
  SPEC.md
  BUILD_PLAN.md
  ROUTING_TEST.md        (M1)
  ROUTING_RESULTS.md     (M1)
  archive/               (superseded doc versions — never deleted)
libs/JUCE/               (submodule)
source/
  engine/                (pure C++: grid, chooser, slice position, tile renderer — no JUCE plugin deps)
  sources/               (SlotSource implementations: file, input bus, send)
  hub/                   (inter-instance Send hub)
  plugin/                (AudioProcessor, parameters, state)
  ui/                    (editor and components)
tests/
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
(SPEC 4.3, section 10). Never use unseeded randomness or wall-clock time in the audio path.

## Engine boundaries

- `source/engine/` must compile and test without the JUCE plugin modules. Keep host and
  UI concerns out of it. It receives time (ppq, bpm, sample position) and returns audio.
- All audio sources go through the `SlotSource` interface. The engine must not know
  whether a slot is a file, a native bus, or a Send.

## Definition of done (every milestone)

- Acceptance checks in the build plan pass.
- `scripts/validate.sh` is green.
- `CHANGELOG.md` updated, milestone tagged and pushed.
- "What I need from you" list delivered to Seth.

## Don't

- Don't add time-stretching, pitch shifting, AAX, Windows, licensing, or anything else in
  SPEC section 11 without Seth explicitly pulling it into a milestone.
- Don't polish the UI before M6.
- Don't change parameter IDs, plugin codes, or the bundle ID once M7 freezes them.
- Don't delete anything (see version control rules).
