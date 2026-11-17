# Routing results — M1

**Doc version:** v0.1 (2026-11-16) — empty template. Steps are in `docs/ROUTING_TEST.md`.
**Build tested:** v0.2 (tag) · Host versions: Live ____ · Logic ____ · Reaper ____ · macOS 15.6

## A — Native input buses

| Host | Format | Buses showing signal (of 8) | How Input 2 was fed | Notes |
|---|---|---|---|---|
| Ableton Live 12 | VST3 | | | |
| Ableton Live 12 | AU | | | |
| Logic Pro | AU | | | |
| REAPER | VST3 | | | |

## B — Sends

| Host | Send meters moving (of 8) | Reported latency | Underruns | Collision warning shown? | Notes |
|---|---|---|---|---|---|
| Ableton Live 12 | | | | | |
| Logic Pro | | | | | |
| REAPER | | | | | |

## C — Null test (look-behind 2048 unless noted)

| Host | Buffer 64 | Buffer 512 | Buffer 2048 | Offline bounce | Skew min / max | "Smallest look-behind" readout (buf 2048 / buf 64) |
|---|---|---|---|---|---|---|
| Ableton Live 12 | | | | | | |
| Logic Pro | | | | | | |
| REAPER | | | | | | |

## D — processBlock on empty tracks

| Host | Effect, playing | Effect, stopped | Instrument, playing | Instrument, stopped | Muted track |
|---|---|---|---|---|---|
| Ableton Live 12 | | | | | |
| Logic Pro | | | | | |
| REAPER | | | | | |

## Problems seen

_(crashes, scan failures, odd readouts — host, format, what you did)_

## Decision gate (Claude Code proposes, Seth approves)

- Plugin category: _pending_
- Send is the primary live transport in Live and Logic: _pending_
- Look-behind default: _pending_
- Host-specific notes for the user guide: _pending_

## Change log

- **v0.1 — 2026-11-16** — Empty template.
