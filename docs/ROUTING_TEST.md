# Routing test — M1 (Seth's checkpoint)

**Doc version:** v0.1 (2026-11-16)
**Companion:** `docs/ROUTING_RESULTS.md` (fill that in), `docs/SPEC.md` section 5.

About 30 minutes across Live, Logic and Reaper. The point is to replace the assumptions in
SPEC section 5 with measurements. Record everything in `ROUTING_RESULTS.md`, or just tell
Claude Code the results and it will fill the table in.

## What you are testing with

`scripts/build.sh` installs two plugins:

| Plugin | Formats | What it is |
|---|---|---|
| **Mosaic** | AU (effect), VST3 (Fx) | The real plugin, in its M1 probe form |
| **Mosaic Inst Probe** | AU (instrument), VST3 (Instrument) | Same code built as an instrument, only for test D |

Both open a plain probe window:

- **Role** — Mosaic (receives) or Send (transmits). **Group** A–D and **Send #** 1–8.
- **Probe source** — in Mosaic role, routes exactly one source (Input N or Send N) straight
  to the output. Nothing else is mixed in.
- **Look-behind** — in Mosaic role with a Send source, how many samples behind the
  playhead the Send is read, which is also the latency reported to the host.
- **Meters** — left bank: native input buses 1–8 (dimmed = the host did not connect that
  bus). Right bank: Sends 1–8 in the current group (or, in Send role, this Send's input).
- **Readout** — host block size, whether `processBlock` is being called, playhead
  (playing / timeInSamples / ppq / bpm), reported latency, Send skew, underruns, collisions.

Set every DAW to **48 kHz** for these tests unless noted. Make sure plugin delay
compensation is on: Live → Options → Delay Compensation; Logic → Settings → Audio →
General → Plug-in Latency Compensation: All; Reaper → on by default.

---

## Test A — Native input buses: how many can the host actually feed?

Goal: for each DAW, how many of **Input 1–8** light up.

### Ableton Live 12
1. Audio track 1: a clip playing. Insert **Mosaic** (try VST3 first, then AU). Role = Mosaic,
   Probe source = Input 1. Play: Input 1 meter moves.
2. Audio track 2: a different clip playing. In Mosaic's device title bar, expand the
   **sidechain / routing** section (the small triangle on the device) and set *Audio From*
   to track 2. Does the **Input 2** meter move?
3. Look for any way to feed Input 3. Record: how many buses show signal (expected: 1 or 2).

### Logic Pro
1. Audio track 1 with a region. Insert Mosaic (AU) as an audio FX. Play: Input 1 moves.
2. In the plugin header, open the **Side Chain** menu and choose track 2 (which has its
   own region playing). Does **Input 2** move?
3. Record how many buses show signal (expected: 2). Note whether Logic lists the plugin
   under Audio FX, and whether it also appears anywhere with more inputs.

### REAPER
1. Track 1 with an item. Insert Mosaic (VST3). Play: Input 1 moves.
2. Track 1 → **Route** button → set *Track channels* to **16**.
3. On tracks 2–8 (each with its own item), add a **send** to track 1; in the send's
   routing panel set the destination channels to 3/4, 5/6, 7/8, 9/10, 11/12, 13/14, 15/16.
4. In the FX window, click the **"2 in 2 out"** button (plugin pin connector). Enable
   plugin inputs for Input 2–8 and map them to track channels 3–16.
5. Record how many of the 8 meters move (expected: all 8). Repeat with the AU if you
   have the patience; VST3 is enough.

---

## Test B — Sends: do all 8 arrive?

Goal: 8 tracks each carrying a **Send** reach one **Mosaic** instance.

In each DAW:
1. Eight audio tracks, each with a clip playing. On each, insert Mosaic, set Role = **Send**,
   Group = **A**, Send # = **1 … 8** (one number per track). Leave *mute output* off for now.
2. A ninth audio track: insert Mosaic, Role = **Mosaic**, Group = A. Play.
3. Do all 8 Send meters move? Set Probe source to Send 1, then Send 8: do you hear them?
4. Readout check: *reported latency* should equal the look-behind (2048 by default) and
   *underruns* should stay at 0 while playing.
5. **Collision check:** on two of the Send tracks set the same Send # (e.g. both 4). Both
   Send windows and the Mosaic window should show the collision warning. Set it back.
6. Stop the transport: Send meters should fall to silence (Sends only transmit while
   playing — tell me if that feels wrong in practice).

Record per DAW: number of Send meters moving, latency readout, any underruns, whether the
collision warning showed.

---

## Test C — The null test (sample alignment)

Goal: the Send path is sample-exact in real time and in an offline bounce, at three buffer
sizes. Run this in **each** DAW.

Setup (once per DAW):
1. **Track A:** a short, bright clip (something with transients). Insert Mosaic: Role =
   Send, Group A, Send 1, **mute output ON** (so the dry signal is not also heard).
2. **Mosaic track:** an empty audio track. Insert Mosaic: Role = Mosaic, Group A, Probe
   source = **Send 1**, Look-behind = **2048**.
3. **Track B:** the same clip at the same position. Insert a utility / gain plugin with
   **polarity (phase) inverted**. (Live: Utility → Ø; Logic: Gain → Phase Invert; Reaper:
   track polarity button.)
4. All three tracks to the master. Master meter is your judge: **silence = pass**.

Runs:
5. Buffer **64**: play. Silence? Record pass/fail and the readout's *skew min/max* and
   *underruns*.
6. Buffer **512**: same. 7. Buffer **2048**: same.
8. **Offline bounce** at any buffer size: bounce / export the master. Is the result silent
   (peak at −inf, flat waveform)? Record pass/fail.
9. **Find the floor:** the readout line *"Smallest look-behind with no underruns so far"*
   is computed from the measured skew every block, whatever Look-behind is set to. Press
   *Reset stats*, play ~20 seconds (include a loop wrap and a locate), and record that
   number at buffer 2048 and again at buffer 64. If you want to see it fail, set
   Look-behind below that number: underruns should start counting and the sum stops
   nulling.
10. If it never nulls: lower the Look-behind to 0 and tell me what the skew readout shows.
    If the sum is quiet but not silent, say whether it sounds like a comb filter (offset by
    a few samples) or like a quiet copy (gain mismatch).

---

## Test D — Does the host keep calling us on an empty track?

Goal: decide the plugin category (SPEC section 8).

In each DAW:
1. Empty audio track (no clips at all). Insert **Mosaic** (effect). Play the transport.
   Readout line 2 says **BEING CALLED** or **NOT BEING CALLED**. Record it.
2. Stop the transport. Record it again.
3. Instrument track (Logic: Software Instrument; Live: MIDI track; Reaper: any track).
   Insert **Mosaic Inst Probe**. Play, then stop. Record both.
4. Also note: with the track **muted**, is it still called? With the track **frozen** or
   **track activator off** (Live), it will not be — that is expected.

---

## Anything else worth noting

- Any crash, hang, scan failure, or window that would not open: which DAW, which format,
  what you did.
- Whether the AU or the VST3 behaved differently in Live.
- Readout values that look wrong (ppq not advancing, bpm n/a, playhead MISSING).

## Change log

- **v0.1 — 2026-11-16** — First version for the M1 checkpoint.
