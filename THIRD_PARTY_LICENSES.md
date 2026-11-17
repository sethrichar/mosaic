# Third-party code and licences

Mosaic is proprietary (all rights reserved). Every dependency is pinned to an exact tag
or commit and listed here with its licence. Only permissive licences are allowed, plus
JUCE under its commercial terms (see `CLAUDE.md`). No GPL / LGPL / AGPL code.

| Dependency | Where | Pinned | Licence |
|---|---|---|---|
| JUCE | `libs/JUCE` (submodule) | tag `9.0.5` (commit `d60b7a78`) | JUCE 9 commercial licence, **Starter** tier (not the AGPLv3 option) |
| Catch2 | `libs/Catch2` (submodule) | tag `v3.16.1` | Boost Software License 1.0 — tests only, not shipped |
| Signalsmith Stretch | `libs/signalsmith-stretch` | _(added at M4)_ | MIT |

## JUCE

JUCE is dual-licensed under the AGPLv3 and the commercial JUCE 9 licence
(https://juce.com/legal/juce-9-licence/). Mosaic uses the **commercial** licence at the
Starter tier (free; annual revenue or funding limit of USD 20,000 at the time of writing).
If Richardson Media Solutions' revenue passes that limit, the licence must be upgraded to
Indie or Pro before further distribution. JUCE 9 does not require a splash screen.

Full licence text: `libs/JUCE/LICENSE.md`.

## Catch2 — Boost Software License 1.0

Used for unit tests only; no Catch2 code ships in the plugin.

Full licence text: `libs/Catch2/LICENSE.txt`.

## Signalsmith Stretch — MIT

_(To be filled in at M4 with the pinned commit and the full MIT licence text.)_
