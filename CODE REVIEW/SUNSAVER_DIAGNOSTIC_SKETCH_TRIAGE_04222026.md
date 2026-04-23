# SunSaver / Opta RS-485 Diagnostic Sketch Triage — 2026-04-22

Date: 2026-04-22
Status: Recommendation — no files moved yet. User to confirm before any moves.

## Background

The 2026-04-21..22 SunSaver MPPT bring-up generated **18 diagnostic
sketches** under `firmware/sunsaver-*/`. Each was created to test a specific
hypothesis during a multi-day root-cause hunt that ultimately resolved with
two findings:

1. **Polarity** — Morningstar uses opposite A/B convention from modern
   adapters (Opta A → MRC-1 B, Opta B → MRC-1 A).
2. **Last-byte corruption** — Opta `RS485` library defaults silently corrupt
   the last byte of every TX frame at 9600 baud. Fix: `RS485.setDelays(0, 1200)`
   plus the forum TX bracket (`noReceive` → `beginTransmission` → `write` →
   `flush` → `delay(1)` → `endTransmission` → `receive`). See Arduino forum
   thread #1421875 post #18.

Most sketches are now obsolete — the answers they were searching for have
been found and folded into the production firmware and into
[firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino](../firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino).
This document classifies each for keep / archive.

## Classification

### KEEP (6) — these have ongoing utility

| Sketch | Why keep | Future use |
|--------|----------|------------|
| [firmware/sunsaver-modbus-test/](../firmware/sunsaver-modbus-test/) | The clean ArduinoModbus reference. Now corrected for register map, parity, and post-delay. Smallest possible "is Modbus working?" sketch. | First sketch to flash on any future Morningstar bring-up before touching production firmware. |
| [firmware/sunsaver-rs485-raw/](../firmware/sunsaver-rs485-raw/) | THE register-verification workhorse. Bypasses ArduinoModbus, manual frames, sweeps {8N1, 8N2} × ~22 candidate registers per cycle. Already extended on 2026-04-22 to cover all power-management candidate registers from `SOLAR_MODBUS_FUTURE_IMPROVEMENTS_04222026.md`. | Verifying any new register address before adding to production. |
| [firmware/sunsaver-rs485-passive-sniff/](../firmware/sunsaver-rs485-passive-sniff/) | Pure RX, no TX. Sweeps {2400..38400} × {8N1, 8N2, 8E1, 8O1}. Definitively answers "is anyone else talking on this bus?" | First step when joining an existing RS-485 segment, or to rule in/out the recurring-0x00 self-echo artifact. |
| [firmware/sunsaver-tx-stress/](../firmware/sunsaver-tx-stress/) | Continuous 0x55/0xAA TX. Smallest possible test that the Opta is electrically driving the bus, observable via MRC-1 LED color change. | When MRC-1 LED stays steady green: flash this and watch the LED. If still steady, transceiver / wiring / Opta variant is suspect. |
| [firmware/sunsaver-rs485-full-id-scan/](../firmware/sunsaver-rs485-full-id-scan/) | Sweeps slave IDs 1..247 with FC03 + FC04. | Bringing up a controller whose slave ID was changed from default (1) by a previous installer. |
| [firmware/serial-heartbeat-opta/](../firmware/serial-heartbeat-opta/) | Bare-bones USB-CDC heartbeat with no other dependencies. | Eliminating "is the host's serial pipe dead?" before chasing harder problems. |

### ARCHIVE to RecycleBin (12) — purpose-built for already-resolved issues

| Sketch | What it tested | Why archive |
|--------|----------------|-------------|
| [firmware/sunsaver-rs485-baseline/](../firmware/sunsaver-rs485-baseline/) | A/B comparison of no-TX vs with-TX phase RX | Resolved: confirmed recurring 0x00 was self-echo from DE flip. Finding folded into setDelays fix. |
| [firmware/sunsaver-rs485-windowed-probe/](../firmware/sunsaver-rs485-windowed-probe/) | Delayed-RX-open windows (0 vs 8 ms) | Resolved: was chasing the same self-echo artifact. |
| [firmware/sunsaver-rs485-boot-window/](../firmware/sunsaver-rs485-boot-window/) | First-60s rapid sweep to catch boot-only response window | Resolved: SunSaver does not have a boot-only response window. |
| [firmware/sunsaver-rs485-flush-ab/](../firmware/sunsaver-rs485-flush-ab/) | A/B test of explicit `RS485.flush()` before `endTransmission` | Resolved: flush() alone insufficient; `setDelays` post-delay is what matters. |
| [firmware/sunsaver-rs485-char-postdelay-probe/](../firmware/sunsaver-rs485-char-postdelay-probe/) | One-character post-delay (1146 µs at 9600 8N2) | Resolved: 1200 µs adopted in production. Finding folded into `sunsaver-rs485-raw`. |
| [firmware/sunsaver-rs485-postdelay-sweep/](../firmware/sunsaver-rs485-postdelay-sweep/) | Sweep of postDelay {1146, 2300, 4600, 9200} µs × 2 parities × 2 slaves | Resolved: 1200 µs adopted. Sweep no longer needed. |
| [firmware/sunsaver-rs485-baud-parity-sweep/](../firmware/sunsaver-rs485-baud-parity-sweep/) | {4800, 9600, 19200, 38400} × {8N1, 8N2, 8E1, 8O1} sweep | Resolved: 9600 8N2 confirmed. If ever needed again, easy to reproduce by editing `sunsaver-rs485-raw`'s parity loop. |
| [firmware/sunsaver-rs485-protocol-poke/](../firmware/sunsaver-rs485-protocol-poke/) | Multiple TX framing styles (contiguous, byte-paced, with preamble) | Resolved: framing style does not matter; post-delay does. |
| [firmware/sunsaver-rs485-coil-probe/](../firmware/sunsaver-rs485-coil-probe/) | FC01/FC05/FC15 coil probe, deliberately invalid coil | Resolved: SunSaver does not implement coils. Dead-end avenue. |
| [firmware/sunsaver-rs485-mrc1-probe/](../firmware/sunsaver-rs485-mrc1-probe/) | MRC-1's own diagnostic Modbus address | Resolved: MRC-1 is a transparent bridge, not a Modbus slave. Dead-end avenue. |
| [firmware/sunsaver-rs485-autoscan/](../firmware/sunsaver-rs485-autoscan/) | Software autoscan over (slave × baud × parity × FC) | Superseded by `sunsaver-rs485-raw` + `full-id-scan`. |
| [firmware/sunsaver-rs485-patient/](../firmware/sunsaver-rs485-patient/) | Long 5 s listen, 3 retries per cycle | Superseded — long-window logic absorbed into `sunsaver-rs485-raw`. |

## Recommended action

1. Move the 12 ARCHIVE-tier sketches into `RecycleBin/firmware/sunsaver-debug-2026-04/`
   (not deleted — preserved for forensic reference) once the user approves.
2. Add a one-paragraph `firmware/README.md` if one doesn't exist that points
   to the 6 KEEP sketches with one sentence each, so the next person knows
   which to flash first.
3. Update each KEEP sketch's header comment to add a one-line "Use when:"
   recipe so they're self-documenting.

## Lessons (verbatim from the failure log)

These are worth carrying into any future RS-485 bring-up on Opta or
Morningstar gear:

1. **Post-delay first, parity second, polarity third.** Most "Modbus
   silent" bugs on Opta are the last-byte corruption issue. Set
   `RS485.setDelays(0, ceil(11/baud * 1e6))` before anything else.
2. **MRC-1 LED is the cheapest diagnostic.** Steady green = no data,
   green-with-amber-flicker = bidirectional traffic, steady amber = A/B
   reversed, off = no power. Always look at the LED before instrumenting.
3. **Morningstar A = inverting (DATA-), B = non-inverting (DATA+).**
   Opposite of every modern USB-RS-485 adapter we own. Cross the wires.
4. **Recurring single 0x00 byte ≠ slave reply.** It is the Opta's own
   transceiver direction-flip self-echo. Use `sunsaver-rs485-passive-sniff`
   to confirm before chasing slave-side hypotheses.
5. **The published register map can be wrong for a given firmware
   revision.** SunSaver MPPT 0x0010..0x0013 are documented as charge/load
   current and battery/array voltage but were zero on this unit; the live
   values are at 0x0008..0x000C. Always sweep before trusting the spec.
6. **`flush()` is not enough.** The Opta `endTransmission()` already calls
   the underlying serial `flush()`; the bug is between flush returning and
   the DE pin dropping. Only `setDelays(0, ≥1 char-time)` fixes it.
7. **Don't rely on `millis()` for long campaigns** during a bring-up —
   the Opta's USB-CDC port re-enumerates (COM9 ↔ COM10 was observed)
   when the bootloader runs, breaking any hardcoded port path.

## Cross-references

- Bench memory: `sunsaver-modbus-bringup-2026-04-21.md`,
  `sunsaver-production-fix-2026-04-22.md`
- Forum thread (root cause): <https://forum.arduino.cc/t/opta-rs485-bug-last-byte-of-any-frame-is-modified-crc-always-wrong/1421875/18>
- Future improvements backlog: [SOLAR_MODBUS_FUTURE_IMPROVEMENTS_04222026.md](SOLAR_MODBUS_FUTURE_IMPROVEMENTS_04222026.md)
- New tool repo design: [OPTAVIEW_NEW_REPO_DESIGN_04222026.md](OPTAVIEW_NEW_REPO_DESIGN_04222026.md)
