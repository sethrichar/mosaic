# Mosaic

A JUCE audio plugin (VST3 / AU / Standalone, macOS) that chops up to 8 sources — loaded
files or live tracks — on a tempo-synced grid and plays one tile at a time in random or
sequenced order. Imported files get their key detected and can be snapped to the mosaic's
key.

Owner: Seth Richardson, Richardson Media Solutions LLC. Proprietary — all rights reserved.

## Docs

- `CLAUDE.md` — rules and context for Claude Code working in this repo
- `docs/SPEC.md` — product and technical spec
- `docs/BUILD_PLAN.md` — milestones and acceptance checks
- `CHANGELOG.md` — what changed at every tag, and how to get back to the previous one
- `THIRD_PARTY_LICENSES.md` — every dependency, pinned, with its licence

## Building

Requirements: macOS, Xcode (command line tools), CMake ≥ 3.24, Ninja (optional),
pluginval at `/Applications/pluginval.app` for validation.

```bash
git clone --recurse-submodules https://github.com/sethrichar/mosaic.git
cd mosaic
scripts/build.sh            # configure + universal Release build + unit tests + install
scripts/validate.sh         # auval + pluginval (strictness 10)
```

`scripts/build.sh Debug` builds a Debug configuration into the same `build/` folder.
Built plugins are copied to `~/Library/Audio/Plug-Ins/Components` and `.../VST3`.
During M1 a second plugin, **Mosaic Inst Probe**, is built and installed alongside it
(disable with `-DMOSAIC_BUILD_INST_PROBE=OFF`).

## Layout

See the "Repo layout" section of `CLAUDE.md`.
