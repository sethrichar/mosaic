# Routing results — M1

**Doc version:** v0.2 (2026-11-16) — previous: v0.1 (empty template, in git history at tag `v0.2`).
Steps are in `docs/ROUTING_TEST.md`.
**Build tested:** v0.2 (tag) · Hosts: Ableton Live 12 Suite, Logic Pro, REAPER (versions not
recorded) · macOS 15.6, Apple Silicon · Reported by Seth 2026-11-16, recorded by Claude Code.

## A — Native input buses

| Host | Format | Buses showing signal (of 8) | How Input 2 was fed | Notes |
|---|---|---|---|---|
| Ableton Live 12 | VST3 / AU | **2** | Device sidechain ("Audio From") | No route to Input 3+ |
| Logic Pro | AU | **2** | Plugin header Side Chain menu | No route to Input 3+ |
| REAPER | VST3 | **8** | Track channels 16 + pin connector | All 8 meters moved |

Conclusion: matches SPEC 5.1. Native buses give 2 live sources in Live and Logic, 8 in
Reaper. Native buses alone cannot deliver 8 live sources.

## B — Sends

| Host | Send meters moving (of 8) | Reported latency | Underruns | Collision warning shown? | Notes |
|---|---|---|---|---|---|
| Ableton Live 12 | 8 | 2048 | 0 | yes | |
| Logic Pro | 8 | 2048 | 0 | yes | |
| REAPER | 8 | 2048 | 0 | yes | |

Conclusion: the in-process Send hub reaches all 8 Sends in all three hosts.

## C — Null test (look-behind 2048)

| Host | Buffer 64 | Buffer 512 | Buffer 2048 | Offline bounce | Skew min / max | "Smallest look-behind" readout (buf 2048 / buf 64) |
|---|---|---|---|---|---|---|
| Ableton Live 12 | pass | pass | pass | pass | not noted | **not noted — re-run step 9** |
| Logic Pro | pass | pass | pass | pass | not noted | **not noted — re-run step 9** |
| REAPER | pass | pass | pass | pass | not noted | **not noted — re-run step 9** |

Conclusion: the timeline-tagged Send path is sample-exact in real time and offline in all
three hosts with a 2048-sample look-behind, up to a 2048-sample buffer. The *smallest*
look-behind each host needs is still unknown; that number sets the shipping default.

## D — processBlock on empty tracks

| Host | Effect, playing | Effect, stopped | Instrument, playing | Instrument, stopped | Muted track |
|---|---|---|---|---|---|
| Ableton Live 12 | called | not noted | called | not noted | not noted |
| Logic Pro | **NOT called** | not noted | called | not noted | not noted |
| REAPER | called | not noted | called | not noted | not noted |

Conclusion: Logic does not process audio effects on an audio track that has no regions
playing. Live and Reaper keep calling the effect. The instrument build is called in all
three. **Only the Mosaic-role instance is affected:** Send-role instances sit on tracks
that have audio, so they are always processed.

## Problems seen

None reported. All plugins scanned, loaded and opened their window in every host and
format tried.

## Decision gate — proposed by Claude Code, awaiting Seth's approval

1. **Send is the primary live transport in Live and Logic.** Confirmed by B and C. Native
   buses stay as a bonus: Input 1 always, Input 2 via sidechain, Input 3–8 in Reaper.
   → SPEC 5.1 table becomes measurements; 5.2 unchanged.

2. **Plugin category: stay an audio effect (`aufx` / `Fx`), one identity, one binary.**
   Reason: the Send hub is a per-binary singleton. Shipping an instrument build next to
   the effect would split the hub, so a Send (effect, on an audio track) could not reach
   a Mosaic (instrument). Keeping one binary keeps the Send design working everywhere.
   The Logic problem is solved by *where the Mosaic-role instance is placed*, not by a
   second plugin:
   - **Logic:** put the Mosaic-role instance on an **Aux channel strip** (or an instrument
     track's audio FX slot) rather than an empty audio track. Logic processes aux strips
     continuously. **Needs a 2-minute check (see "What I need from you").**
   - **Live / Reaper:** any track works.
   Fallback if the Aux check fails: move the hub to POSIX shared memory (`shm_open` +
   mmap), which works across binaries and across processes, then ship both an effect and
   an instrument. That is the parked "cross-process Send transport" item pulled forward;
   roughly a day of work plus re-running test C.

3. **Look-behind (reported latency).** Pending the step-9 numbers. Working proposal:
   `L = 2 × host block size` from `prepareToPlay`, never below 256 samples, recomputed on
   every `prepareToPlay`, and reported only while a Send source is in use (SPEC 5.3 item 6).
   If the measured "smallest look-behind" readouts come back at or under 1× block size in
   every host, 2× leaves a full block of headroom for PDC reorder. If any host reports
   more than 2× block, the factor goes up to match, with headroom.

4. **Sends transmit only while the transport is playing** (SPEC 4.1). Keep for v1; Seth
   did not flag it as a problem in testing.

5. **User-guide notes (M8):** Live — Send on each source track, sidechain for Input 2.
   Logic — Mosaic-role instance on an Aux strip; Side Chain for Input 2. Reaper — native
   buses through the pin connector are an alternative to Sends.

Once approved: SPEC → v0.4 (section 5 rewritten from these measurements, section 8
category decided), BUILD_PLAN → v0.4 (M5 scope: Send primary, Logic placement note).

## Change log

- **v0.2 — 2026-11-16** — Seth's results for A–D recorded; decision-gate proposals added.
  Look-behind floor (step 9) still to be measured.
- **v0.1 — 2026-11-16** — Empty template.
