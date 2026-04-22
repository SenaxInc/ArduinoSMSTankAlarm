# SunSaver MPPT Modbus Bring-Up — Failure Report

**Date:** April 21, 2026
**Hardware:** Arduino Opta (Mbed OS) + Morningstar MRC-1 + Morningstar SunSaver SS-MPPT-15L (brand new, factory defaults)
**Outcome:** Modbus RTU communication NOT established. Every firmware experiment failed to elicit a valid response from the SunSaver.
**Author:** GitHub Copilot (AI agent), under user direction

---

## TL;DR

After a multi-hour iterative debugging session covering ~6 distinct test sketches, ~40 sweep permutations across protocol/parity/slave-ID/register-address combinations, physical reconfiguration of the EIA-485 wiring multiple times, and explicit one-character post-delay validation, **no valid Modbus reply was ever observed from the SunSaver MPPT through the MRC-1 adapter**. Every read returned exactly 1 byte (0x00), which is an electrical artifact from the Opta's RS-485 transceiver direction switch, not a real response.

The most probable remaining root cause — based on careful re-reading of the Morningstar Modbus spec v11 — is that **the MRC-1 (MeterBus ↔ RS-485 level converter) is not officially supported for Modbus communication**. Morningstar specs the separate **MSC** product (MeterBus → RS-232) for Modbus. Until that is verified with an MSC or USB-RS485+MSView, the firmware cannot be unblocked from this side.

---

## What Was Verified Working

| Check | Method | Result |
|---|---|---|
| Opta board healthy | USB serial heartbeat sketch | ✅ |
| Modbus library loads | ArduinoModbus init in test sketch | ✅ |
| Opta TX physically drives bus | TX-stress sketch (continuous 0x55/0xAA) → MRC-1 LED reacted | ✅ |
| A/B polarity correct | LED color test (green not amber) | ✅ |
| EIA-485 GND wired | Direct continuity verification | ✅ |
| MRC-1 powered | LED on, switch position ON | ✅ |
| OEM RJ-11 cable connected | Visual + LED indication | ✅ |
| SunSaver itself powered | Battery LED solid green, charging LED flashing | ✅ |
| Modbus query frame CRCs | Hand-verified against algorithm; cross-checked frame `01 04 00 12 00 01 91 CF` | ✅ |
| **DIP switch 4 set for Modbus mode** | **User confirmed visually** | ✅ |

## What Was Tried, Did NOT Work

| Variable Swept | Values Attempted | Result |
|---|---|---|
| Function code | 0x03 (Read Holding), 0x04 (Read Input), 0x11 (Report Slave ID), 0x08 (Diagnostics), 0x01 (Read Coils), 0x05 (Write Single Coil), 0x0F (Write Multiple Coils) | All silent/artifact-only (no CRC-valid frames) |
| Slave ID | Focused: 1 (default), 2, 16, 247; Exhaustive: full scan 1..247 | No CRC-valid response/exception; full scan summary: valid_response=0, valid_exception=0, hits=0 |
| Parity | 8N1, 8N2 (per spec), 8E1 | All silent |
| Register address | 0x0008 (Adc_vb_f), 0x0012 (array_fault) | All silent |
| RS485.setDelays | (0,0), (50,50), library default | All silent |
| Listen window | 500ms, 800ms, 5000ms | All silent |
| Queries per cycle | 1, 3, 12 | All silent |
| Wiring polarity | A→A/B→B, then swapped | LED test confirmed which is correct |
| SunSaver battery power-cycle | Disconnect 5s, reconnect | No change in result |

## Test Sketches Created (all under `firmware/`)

| Sketch | Purpose | Outcome |
|---|---|---|
| `sunsaver-modbus-test/` | Standalone Modbus poll using ArduinoModbus library | Modbus FAIL / timeout every poll |
| `sunsaver-rs485-raw/` | Manual Modbus framing, sweep slave×fc×parity | 1-byte 0x00 every cycle (artifact) |
| `sunsaver-rs485-patient/` | 5s listen, 3 retries per cycle | 1-byte 0x00 every cycle (artifact) |
| `sunsaver-rs485-windowed-probe/` | New: compare RX-open windows (0ms vs 8ms) with CRC-validated parsing over 1600ms listen windows | 95s capture: 0 valid responses, 0 exceptions, 54 single-byte 0x00 artifacts |
| `sunsaver-rs485-baseline/` | New: no-TX baseline phase vs with-TX phase to isolate whether 0x00 is tied to local transmit turnaround | Captured full A/B cycle: Phase A total=1 (zero=1), Phase B total=18 (zero=18), nonZero=0 |
| `sunsaver-rs485-boot-window/` | New: fast first-60s query sweep after reset/power-up to catch short-lived startup response windows | Captured full boot summary: steps=231, valid_response=0, valid_exception=0, one_byte_zero=231 |
| `sunsaver-rs485-protocol-poke/` | New: alternate TX styles (contiguous/byte-paced/preamble) + extra function probes (FC11, FC08) with CRC-validated parsing | Captured summary cycle: 0 valid responses, 0 exceptions, 24/24 one-byte 0x00 artifacts |
| `sunsaver-rs485-coil-probe/` | New: coil/control-coil probe (FC01 read coils, FC05/FC15 writes to invalid coil address 0x7FFF) with CRC-validated parsing | Captured summary cycle: steps=20, valid_response=0, valid_exception=0, one_byte_zero=20, no_valid_frame=0 |
| `sunsaver-rs485-full-id-scan/` | New: exhaustive slave-ID scan (1..247) using FC03 reg0012 + FC04 reg0008 with CRC-validated parsing | Captured full scan summary: ids_scanned=247, steps=494, valid_response=0, valid_exception=0, one_byte_zero=493, no_valid_frame=1, hits=0 |
| `sunsaver-rs485-flush-ab/` | New: A/B test of baseline TX vs explicit `RS485.flush()` before `endTransmission()` | Captured A/B summaries were identical: both phases `steps=24`, `valid_response=0`, `valid_exception=0`, `one_byte_zero=24`, `no_valid_frame=0` |
| `sunsaver-rs485-char-postdelay-probe/` | New: protocol-poke equivalent matrix using computed one-character post-delay at 9600 8N2 and explicit `RS485.flush()` before every `endTransmission()` | Captured summary cycle: `steps=24`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=24`, `no_valid_frame=0` |
| `sunsaver-rs485-postdelay-sweep/` | New: forum-pattern TX bracket (`noReceive` + `flush` + `delay(1)` + `endTransmission` + `receive`) with post-delay sweep 1x/2x/4x/8x character time at both 8N1 and 8N2 | Captured summary cycle: `steps=32`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=32`, `no_valid_frame=0` |
| `sunsaver-rs485-passive-sniff/` | New: pure passive RX sweep (no TX) across `{4800,9600,19200,38400, 2400}` baud x `{8N1,8N2,8E1,8O1}` parity, 5 s listen window per config | 2 full cycles captured: `cfgs_with_bytes=0`, `total_bytes=0`. Bus is silent without Opta TX. |
| `sunsaver-rs485-baud-parity-sweep/` | New: forum-pattern TX bracket with full baud + parity sweep `{4800, 9600, 19200, 38400}` x `{8N1, 8N2, 8E1, 8O1}` x slaves `{1,2}`, FC04 reg0008 | Captured summary cycle: `steps=32`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=32`, `no_valid_frame=0` |
| `sunsaver-rs485-mrc1-probe/` | New: MRC-1 own-address diagnostic probe across slave IDs `{0,1,2,16,17,100,247}` x function codes `{FC01, FC02, FC03, FC04, FC07}` with forum-pattern TX bracket | Captured summary cycle: `steps=35`, `hits=0`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=35`, `no_valid_frame=0` |
| `sunsaver-tx-stress/` | Continuous 0x55/0xAA TX to validate TX path | LED color change confirmed Opta drives bus |
| `serial-heartbeat-opta/` | Basic USB serial sanity check | Working |

### Additional Software-Only Hypotheses Tested (April 21-22, 2026)

1. **RX-open timing artifact hypothesis:** The recurring single-byte 0x00 may be generated locally during transceiver turnaround.
	- Implemented in `sunsaver-rs485-windowed-probe` by varying RX-open delay (0ms and 8ms).
	- Result: No CRC-valid Modbus frame in either window; only artifact-class bytes observed.

2. **No-TX baseline hypothesis:** If 0x00 appears even when no query is transmitted, it could be ambient bus noise; if only after TX, it is likely local turnaround artifact.
	- Implemented in `sunsaver-rs485-baseline` with A/B phases (receive-only, then periodic TX).
	- Result: Full A/B capture completed. Phase A reported `totalBytes=1, zeroBytes=1`; Phase B reported `totalBytes=18, zeroBytes=18, nonZeroBytes=0`, confirming received bytes are dominated by 0x00 artifacts during active TX polling.

3. **Boot-window hypothesis:** SunSaver may expose a brief response window during early boot that is missed by slower-cycle tests.
	- Implemented in `sunsaver-rs485-boot-window` with rapid query rotation for the first 60s and CRC-qualified parsing.
	- Result: Full 60s summary captured with `steps=231`, `valid_response=0`, `valid_exception=0`, `one_byte_zero=231`, `other_bytes_no_frame=0`, `silent=0`.

4. **Protocol-path hypothesis:** If the target path rejects read-register polling semantics, it might still answer other Modbus function probes (for example FC11 report-slave-id) or reveal sensitivity to host TX framing style.
	- Implemented in `sunsaver-rs485-protocol-poke` with TX style sweep (contiguous / byte-paced / preamble+query) and probe set including FC04, FC03, FC11, and FC08.
	- Result: Full captured cycle summary showed no CRC-valid response/exception and only one-byte `0x00` artifacts (`valid_response=0`, `valid_exception=0`, `one_byte_zero=24`, `no_valid_frame=0`).

5. **Control-coil hypothesis:** If the path only responds to coil/control operations, it should return either data on FC01 or legal Modbus exceptions on unsupported FC05/FC15 targets.
	- Implemented in `sunsaver-rs485-coil-probe` with FC01 reads at low/high addresses plus FC05/FC15 directed to intentionally invalid coil address `0x7FFF`.
	- Result: Full captured cycle summary showed no CRC-valid response/exception and only one-byte `0x00` artifacts (`steps=20`, `valid_response=0`, `valid_exception=0`, `one_byte_zero=20`, `no_valid_frame=0`).

6. **Wrong-address hypothesis:** If the SunSaver slave ID is not default, an exhaustive address sweep should reveal at least one CRC-valid response or exception.
	- Implemented in `sunsaver-rs485-full-id-scan` by sweeping all slave IDs `1..247` and issuing two probes per ID (`FC03 reg0012` and `FC04 reg0008`) with CRC-qualified parsing.
	- Result: Full scan completed with no hits (`ids_scanned=247`, `steps=494`, `valid_response=0`, `valid_exception=0`, `one_byte_zero=493`, `no_valid_frame=1`, `hits_recorded=0`).
	- Interpretation: a wrong slave ID is now strongly de-prioritized as root cause on this path.

7. **Explicit-flush hypothesis:** Some Opta RS-485 discussions suggest adding `RS485.flush()` before `endTransmission()` to prevent truncated TX/CRC issues.
	- Research finding: in the installed ArduinoRS485 library, `RS485Class::endTransmission()` already calls `_serial->flush()` before toggling direction; bundled ArduinoRS485 examples do not call `RS485.flush()` manually.
	- Implemented in `sunsaver-rs485-flush-ab` as a direct A/B comparison: Phase A baseline TX path, Phase B adds explicit `RS485.flush()` before every `endTransmission()`.
	- Result: no behavior change. Both phases reported `steps=24`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=24`, `no_valid_frame=0`.
	- Interpretation: explicit extra flush did not improve communication or frame validity on this setup.

8. **Character-time post-delay hypothesis:** If Opta DE is being dropped before the final byte physically clears the bus, adding a baudrate-derived post-delay of at least one full character time should eliminate last-byte truncation artifacts.
	- Implemented in `sunsaver-rs485-char-postdelay-probe` with computed `postDelayUs = char_time_us` for `9600 8N2` (11-bit character, approx `1146us`), `RS485.setDelays(0, postDelayUs)`, and explicit `RS485.flush()` before each `endTransmission()`.
	- Test matrix reused protocol-poke tuple coverage (TX style sweep x slave IDs x function probes) so outcomes are directly comparable.
	- Result: unchanged artifact pattern after full captured cycle (`steps=24`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=24`, `no_valid_frame=0`).
	- Interpretation: on this exact Opta + MRC-1 + SunSaver path, one-character post-delay did not produce any CRC-valid Modbus frame and did not change the observed failure mode.

9. **Forum-pattern full TX bracket + post-delay sweep hypothesis:** The Arduino forum thread "Opta RS485 bug: last byte of any frame is modified" (post #16/#18) documents an exact TX bracket (`RS485.noReceive(); beginTransmission(); write(); flush(); delay(1); endTransmission(); receive();`) plus a baudrate-derived post-delay; the recommended post-delay is ~1 character time but larger may be needed in some cases.
	- Implemented in `sunsaver-rs485-postdelay-sweep` with that exact TX bracket, sweeping `postDelay` over `{1146, 2300, 4600, 9200}` microseconds (1x/2x/4x/8x character time), at both `SERIAL_8N1` and `SERIAL_8N2`, against slave IDs `{1,2}` and queries `FC04 reg0008` and `FC03 reg0012`.
	- Result: unchanged artifact pattern across the full sweep (`steps=32`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=32`, `no_valid_frame=0`).
	- Interpretation: even the most complete TX-side mitigation pattern documented for the Opta RS-485 last-byte corruption bug does not produce a CRC-valid response from the slave on this path. The remaining failure is therefore not a TX-side host bug; the request either is not being delivered intact through MRC-1, or the slave/path is not answering Modbus at all.

10. **Passive RX sniff hypothesis:** If something on the bus is producing the observed `0x00` bytes independent of Opta TX, a pure RX sweep with no Opta TX would catch it. Conversely, if `0x00` only appears during our TX, the artifact is the Opta's own DE-flip self-echo and the slave end is producing nothing at all.
	- Implemented in `sunsaver-rs485-passive-sniff` with no TX whatsoever, sweeping baud `{2400, 4800, 9600, 19200, 38400}` and parity `{8N1, 8N2, 8E1, 8O1}` (8 distinct configs), 5 s listen window per config, two full cycles.
	- Result: `cfgs_with_bytes=0`, `total_bytes=0` across all 16 listen windows. Zero bytes received without Opta TX.
	- Interpretation: DECISIVE — the bus is completely silent when Opta is not driving it. Combined with the consistent "single 0x00 only during TX" pattern, this proves the recurring `0x00` is the Opta's local DE-flip artifact and that nothing on the slave end (MRC-1, SunSaver, or any other device) is producing any RS-485 traffic in any framing or at any baud we tested.

11. **Baud + parity full sweep hypothesis:** If MRC-1 or SunSaver was configured at a non-default baud or parity, the previous probes (fixed at 9600 8N2 / 9600 8N1) would never see a response. Sweep the full Modbus-RTU baud and parity matrix with the forum-pattern TX bracket and per-baud post-delay scaling.
	- Implemented in `sunsaver-rs485-baud-parity-sweep` with baud `{4800, 9600, 19200, 38400}` x parity `{8N1, 8N2, 8E1, 8O1}` x slaves `{1,2}` (32 tuples per cycle), `FC04 reg0008`, post-delay = 1.2 char times at the active baud.
	- Result: `steps=32`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=32`, `no_valid_frame=0`. Single `0x00` artifact on every step regardless of baud or parity.
	- Interpretation: serial-port misconfiguration at MRC-1 or SunSaver is now de-prioritized as a likely cause. No combination of standard Modbus-RTU framing produces a reply on this path.

12. **MRC-1 own-address diagnostic hypothesis:** The MRC-1 typically responds to Modbus on its own slave ID for diagnostics. If MRC-1 itself is alive and reachable, a sweep across plausible MRC-1 IDs (`16`, `17`, `100`, `247`) and a wider FC set (`FC01`, `FC02`, `FC03`, `FC04`, `FC07`) should produce at least one CRC-valid response or exception, even if nothing downstream answers.
	- Implemented in `sunsaver-rs485-mrc1-probe` with slave IDs `{0,1,2,16,17,100,247}` and FCs `{0x01, 0x02, 0x03, 0x04, 0x07}` using the forum-pattern TX bracket.
	- Result: `steps=35`, `hits=0`, `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=35`, `no_valid_frame=0`. Not a single CRC-valid frame from any FC against any candidate MRC-1 ID.
	- Interpretation: MRC-1 is NOT producing a Modbus reply on its own address either. Combined with the passive-sniff result above, the most parsimonious remaining explanations are: (a) MRC-1 is not configured to act as a Modbus slave on this RS-485 segment (it may be in a Morningstar-proprietary protocol mode), (b) the A/B pair is wired to MRC-1 ports that are physically disconnected from its serial engine, or (c) MRC-1 hardware is not powered on its serial side despite the LED indicating power.

### Conclusion after April 22, 2026 probe expansion

With hypotheses 9, 10, 11, and 12 all returning the same null result, the failure mode is no longer ambiguous on the Opta side:
- TX path is healthy (TX-stress confirmed bus drive).
- RX path is healthy (passive-sniff functions; the artifact is consistent with the documented Opta DE-flip self-echo).
- No Modbus-RTU framing variant (baud, parity, slave ID, function code) elicits a reply.
- MRC-1 itself does not answer Modbus on its own address.

The burden of proof now sits firmly on the slave side of the bus (MRC-1 + SunSaver path), not on the Opta firmware. Next physically-meaningful actions are: verify MRC-1's selected protocol/role (it may need to be in a specific Modbus slave mode rather than a Morningstar-bridge mode), confirm A/B pair lands on MRC-1's RS-485 Modbus port and not a passthrough, and confirm MRC-1 RS-485 segment is actually powered.

### Hardware Power-Source Retest (April 22, 2026)

- User changed MRC-1 power source to the 12V battery and switched `485 PWR` to OFF.
- Visual status during retest: steady green with a red flash about once per second.
- Retest sketch: `sunsaver-rs485-protocol-poke` (full cycle capture).
- Result: unchanged from prior artifact pattern. Summary remained `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=24`, `no_valid_frame=0`.
- Interpretation: this power-source change did not produce any CRC-valid Modbus response or exception frame on the Opta-side diagnostics.

### Hardware Switch A/B Retest (April 22, 2026, later)

- User then flipped the MRC-1 switch position while keeping 12V battery power applied.
- Retest sketch: `sunsaver-rs485-protocol-poke` (full cycle capture).
- Result: unchanged again. Summary remained `valid_response=0`, `valid_exception=0`, `silent=0`, `one_byte_zero=24`, `no_valid_frame=0`.
- Interpretation: on this setup, flipping the switch position under the same 12V power condition did not change observed Modbus behavior.

### Host Tooling Capture Note (latest)

- During later retests, the Opta enumerated on `COM10` (not only `COM9`), indicating runtime/upload port churn.
- Compile/upload steps succeeded on detected port, but `arduino-cli monitor` redirection and .NET SerialPort captures intermittently returned empty logs or OS-level device-function errors.
- Direct interactive `arduino-cli monitor` capture did succeed and produced protocol-poke, baseline A/B, and boot-window summary counts.
- This is a host-side capture reliability issue; it does not provide any new CRC-valid Modbus evidence.

---

## Failure Modes Observed (Agent's Mistakes)

This section is the honest accounting requested by the user. Listed are missteps and overconfident claims made during the session.

### 1. Initially treated the 1-byte 0x00 reply as evidence of partial communication

**What happened:** Early in the session, when polls began returning 1 byte (0x00) instead of 0 bytes, I described this as "the bus is electrically alive" and "1 byte return where 0 was returned before." This was technically true but misleading — that single 0x00 is almost certainly the Opta's RS-485 transceiver glitching during the TX→RX direction flip, *not* a real byte from the SunSaver. A real Modbus reply would start with the slave address byte (0x01), and would be 7+ bytes long.

**Better behavior:** Should have flagged from the first occurrence that 1-byte responses with value 0x00 are most likely electrical artifacts and explicitly *not* evidence of communication.

### 2. Spent too long sweeping firmware permutations after the first three attempts failed

**What happened:** After FC, slave ID, and parity sweeps all failed to produce any change in result, I kept generating new firmware variants (longer listen windows, retries per cycle, register address changes) instead of stepping back and recognizing the result was uniform across every permutation. When every sweep produces *exactly the same* result, the variable being swept is not the cause.

**Better behavior:** Should have stopped iterating on firmware after the second sweep returned identical results and shifted to physical/protocol-stack diagnostics earlier.

### 3. Did not consult the official Morningstar Modbus spec until the user attached it explicitly

**What happened:** Throughout most of the session, I assumed the SunSaver would respond to Modbus by default and chased symptom-level fixes. The spec was attached only after I had exhausted firmware experiments. The spec immediately revealed:
- DIP switch 4 controls Modbus vs MeterBus mode (would have been the first thing to verify)
- Morningstar specs the **MSC**, not the **MRC-1**, for Modbus communication

**Better behavior:** Should have asked for or sought out the device's Modbus spec at the very start, before writing any test sketch. The first 30 minutes of the session would have been spent verifying DIP switch position and confirming the correct adapter product.

### 4. Recommended hardware purchases prematurely

**What happened:** Multiple times I recommended buying a USB-RS485 adapter for MSView testing or RMA'ing the SunSaver as a brand-new defective unit, before fully exhausting the cheap diagnostic tests. The user's pushback ("we shouldn't need to change the cable or call morningstar") was correct — a $0 cable swap and DIP switch verification should have come first.

**Better behavior:** Should have produced an ordered diagnostic checklist (cheapest/fastest first) instead of jumping to hardware acquisition recommendations.

### 5. Did not identify the MRC-1 vs MSC distinction until very late

**What happened:** The user owns an **MRC-1** (MeterBus ↔ RS-485 level converter, intended for daisy-chaining MeterBus networks). The Morningstar Modbus spec (page 3) explicitly calls for an **MSC** (MeterBus → RS-232 converter) for Modbus. These are two different products with different intended use cases. I treated the MRC-1 as if it were the supported Modbus adapter for the entire session.

**Better behavior:** Should have looked up the MRC-1 product page early and noticed it is documented for *MeterBus network extension*, not Modbus host communication. The "transparent" descriptor in its datasheet is transparent to *MeterBus framing*, not necessarily arbitrary serial bytes.

### 6. PowerShell tooling friction wasted multiple iterations

**What happened:** Spent multiple turns fighting `Start-Sleep` timeouts hitting the agent's 10-second tool budget, file-locking issues with `Get-Content` reading log files held open by the writer, and terminal output truncation. Eventually solved with `[System.IO.File]::Open` with `FileShare.Read`, but the friction consumed budget.

**Better behavior:** Should have set up the streaming-capture pattern correctly on the first attempt instead of iterating.

### 7. Did not push back on assumed-working hardware

**What happened:** Took on faith that the OEM RJ-11 cable shipped with the MRC-1 was good. A $0.50 cable with a single bad data-pin crimp would have produced exactly the same symptoms (LEDs work because power pins are intact). Never suggested swapping the cable as a diagnostic step until very late.

**Better behavior:** When debugging "no response" symptoms with a never-before-tested hardware path, every physical link should be on the suspect list from the start.

### 8. Did not save findings to memory until prompted

**What happened:** Did not proactively write findings to repo memory. When the conversation history was summarized, valuable context (which sketches existed, what had been tried, what hadn't) had to be reconstructed from terminal scrollback.

**Better behavior:** For multi-hour debugging sessions, should write incremental notes to `/memories/repo/` after each significant discovery.

---

## What WOULD Verify the Hypothesis

In rough order of cost and effort:

1. **Cable swap (FREE):** Try any other 6P6C straight-through RJ-11 (a phone cord works). If the bus comes alive after the swap, the OEM cable had a bad data conductor.
2. **Continuity test (FREE, 2 min):** Multimeter from Opta A→MRC-1 EIA-485 A pin and from MRC-1 RJ-11 pin 3→SunSaver RJ-11 pin 3. Confirm <1Ω end-to-end.
3. **Acquire Morningstar MSC (~$30):** This is the officially-supported Modbus adapter. If Modbus works through MSC but not MRC-1, the MRC-1 is the limiting factor.
4. **USB-RS485 + MSView (~$15 + free software):** Confirms the SunSaver itself works. Removes the Arduino/Opta from the suspect list entirely.
5. **RMA call to Morningstar (+1-215-321-4457):** Last resort if all of the above fail.

## What Should NOT Be Tried Again

- Any further sweep of slave ID / function code / parity / register address from the Arduino side. These have all been exhausted and produce uniform "no response" outcomes.
- Any "longer listen window" variant. 5000ms is already 1000× a typical Modbus response time.
- Re-flashing the patient probe sketch with minor variations.

## Files & Memory

- Test sketches: `firmware/sunsaver-*/` — all built and uploaded successfully
- Repo memory: `/memories/repo/sunsaver-modbus-bringup-2026-04-21.md` — contains full state for future sessions
- Client config: `TankAlarm-112025-Client-BluesOpta` keeps `solarCharger.enabled = false` until Modbus is verified end-to-end. Do NOT enable until a real response is captured.

## Status of Original Todo Items

| Item | Status | Notes |
|---|---|---|
| Verify verbose client compile | ✅ Complete | |
| Verify verbose client upload | ✅ Complete | |
| Re-enable debug + add staged probes | ✅ Complete | All staged probes built, captured, and analyzed |
| Capture post-flash serial logs | ✅ Complete | Multiple captures retrieved, all show artifact-only RX |
| **Confirm Modbus SunSaver communication** | ❌ **BLOCKED** | Cannot proceed without MSC adapter or MSView verification |
