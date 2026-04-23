# Solar Modbus — Future Improvements Backlog

Date: 2026-04-22
Status: Backlog (not yet scheduled). Production code is currently good for
battery V / array V / charge I / load I; this document lists *additional*
SunSaver MPPT registers worth pulling and the client-side power-management
features they unlock.

## Context

After the 2026-04-22 SunSaver bring-up we now have a known-good Modbus path
to the SS-MPPT-15L (see [TankAlarm-112025-Common/src/TankAlarm_Solar.cpp](../TankAlarm-112025-Common/src/TankAlarm_Solar.cpp)
and the bench memory note `sunsaver-modbus-bringup-2026-04-21.md`). The client
currently consumes only 5 of the ~70 documented registers. The list below
ranks the rest by power-management value.

> All addresses below are from the published Morningstar SunSaver MPPT PDU.
> The 0x0010..0x0013 surprise during bring-up proved that the published map
> and the live map can disagree on a given firmware revision. **Every register
> below MUST be bench-verified via [firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino](../firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino)
> before being wired into production.** The sweep probe was extended on
> 2026-04-22 to cover all of them.

## Tier 1 — high-value, low-cost additions

### 1.1 Net Ah / Wh balance (THE most valuable single addition)

| Reg | Name | Notes |
|-----|------|-------|
| `0x0015` | `Ahc_daily` | Amp-hours **charged** today |
| `0x0016` | `Ahl_daily` | Amp-hours **loaded** today |

**Derived:** `netAhToday = Ahc_daily - Ahl_daily`.

**Client power-management use:**
- If `netAhToday < 0` for **N consecutive days** (suggested: N=2), drop the
  client into a **power-save profile**:
  - Increase tank-poll interval from 5 min → 30 min
  - Suppress non-critical SMS (daily report, info notifications)
  - Keep critical alerts (tank low/high, fault) on the normal cadence
- If `netAhToday > 5 Ah` for 3 days, return to normal profile.
- This is materially better than voltage-only logic because it accounts for
  load draw (e.g., a stuck pump) that voltage hides until the battery sags.

**Storage cost:** 1 float in `SolarData`, persistent rolling 7-day ring buffer
in EEPROM (~28 bytes).

### 1.2 Filtered net battery current

| Reg | Name | Notes |
|-----|------|-------|
| `0x0010` | `Ib_f_1m` | 1-minute filtered battery current, **signed** |

**Why it beats `chargeCurrent`:** `chargeCurrent` (`adc_ic_f`, 0x000B) is
charge current *into* the battery. It does not subtract load draw. `Ib_f_1m`
is the actual net flow at the battery terminals — the only register that
answers "am I winning right now?" with one number.

**Client use:**
- Replace `isCharging` derivation: `isCharging = (Ib_f_1m > +0.05 A)`
- New SolarData field: `netBatteryCurrent`
- Trigger a "net-discharge during full sun" alert (pump anomaly) if
  `Ib_f_1m < 0` while `arrayVoltage > 16 V` for >5 minutes.

### 1.3 1-minute filtered battery voltage

| Reg | Name | Notes |
|-----|------|-------|
| `0x000F` | `vb_f_1m` | 1-minute filtered battery voltage |

**Why:** `adc_vb_f` (the one we currently read) is a fast filter that
oscillates with each pump start. `vb_f_1m` debounces transients and is the
correct signal for the "low battery" alert gate.

**Client use:** route low-battery threshold logic through `vb_f_1m` instead
of `batteryVoltage`. Keep `batteryVoltage` for the live dashboard.

## Tier 2 — useful for daily reporting / scheduling

### 2.1 Daily V min/max

| Reg | Name |
|-----|------|
| `0x003D` | `vb_min_daily` (already in code, not yet bench-verified) |
| `0x003E` | `vb_max_daily` (already in code, not yet bench-verified) |

**Use:** include in daily SMS report. `vb_min_daily` is a far better
"battery health" indicator than instantaneous V at report time.

### 2.2 Hourmeter (controller uptime)

| Reg | Name |
|-----|------|
| `0x0018` (lo) + `0x0019` (hi) | `hourmeter` (32-bit hours since reset) |

**Use:**
- A drift-free wall clock that survives Opta reboots and `millis()` overflow
- "Battery age" / "controller age" estimation for daily report
- Pair with `Ahc_daily` to compute lifetime-Ah throughput

### 2.3 Instantaneous output power

| Reg | Name |
|-----|------|
| `0x002D` | `power_out` (charge W, computed by controller) |

**Use:** human-readable status field in the daily SMS — "currently charging
at 23 W" reads better than "0.27 A at 13.5 V". One register vs two reads
plus a multiply.

## Tier 3 — diagnostic / commissioning

### 3.1 MPPT sweep telemetry

| Reg | Name |
|-----|------|
| `0x0033` | `sweep_pmax` (peak W from last MPPT sweep) |
| `0x0034` | `sweep_vmp` (MPP voltage) |

**Use:** log `sweep_pmax` once per day. Alert when daily peak drops
>30% below the trailing 30-day rolling average — flags shading/dirty
panel/failed cell long before the battery dies.

### 3.2 Status register address verification (open)

The 2026-04-22 production capture showed `cs=Unknown` and `faults=0x4235`
(implausible) using the published addresses 0x002B / 0x002C / 0x002E.
**These are still suspect.** The extended sweep probe now covers them
plus the alternate-address candidates 0x002F (load_state) and 0x0030
(Ilh_max_daily). Bench-verify which addresses return sane values
(charge_state ∈ 0..8, faults bits matching observed conditions) before
relying on them.

Until verified, the production code's `cs=` and `faults=` values should
be treated as advisory only and NOT gated against alarms.

## Implementation order (suggested)

1. **Bench sweep** — flash extended `sunsaver-rs485-raw` and capture which
   addresses respond with plausible values. Update bench memory note.
2. **Add Tier 1.2 (`Ib_f_1m`)** — smallest, highest-ROI change. New field
   in `SolarData`, one extra read (already in the contiguous 5-reg block? no,
   0x0010 is just past the current block at 0x0008..0x000C, so a 9-reg read
   from 0x0008 covers vb/va/vl/ic/il and skips 0x000D/0x000E to land on 0x000F
   `vb_f_1m` and 0x0010 `Ib_f_1m` in one bus turnaround).
3. **Add Tier 1.1 (`netAhToday`)** — needs the daily Ah pair plus a small
   N-day ring buffer in EEPROM and a power-profile state machine.
4. **Fix Tier 3.2 (status register addresses)** — block on bench results.
5. **Add Tier 2 fields** — straightforward once Tier 1 plumbing exists.
6. **Add Tier 3.1 MPPT diagnostics** — daily-only poll, low priority.

## Bus-load budget

Current production poll reads 5 + 2 + 5 + 1 + 2 = **15 registers** per
60 s in five Modbus transactions. Adding all Tier 1 + Tier 2 fields would
take it to ~22 registers in **six transactions** — still under 5% bus
duty cycle at 9600 baud. No need to slow the poll interval.

## Non-goals

- Modbus **writes** to the SunSaver from the TankAlarm client. All write
  capability belongs in OptaView (see [OPTAVIEW_NEW_REPO_DESIGN_04222026.md](OPTAVIEW_NEW_REPO_DESIGN_04222026.md)),
  not in the unattended field client.
- Multi-controller polling. The TankAlarm client is one-controller, one-tank.
- High-rate (sub-minute) polling. The 60 s default is appropriate for
  battery management; faster polling adds bus contention with no telemetry
  benefit.

## Cross-references

- Production header: [TankAlarm-112025-Common/src/TankAlarm_Solar.h](../TankAlarm-112025-Common/src/TankAlarm_Solar.h)
- Production reader: [TankAlarm-112025-Common/src/TankAlarm_Solar.cpp](../TankAlarm-112025-Common/src/TankAlarm_Solar.cpp)
- Sweep probe: [firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino](../firmware/sunsaver-rs485-raw/sunsaver-rs485-raw.ino)
- Bench memory: `sunsaver-modbus-bringup-2026-04-21.md`,
  `sunsaver-production-fix-2026-04-22.md` (repo memory)
- OptaView design (Modbus *write* features live there): [OPTAVIEW_NEW_REPO_DESIGN_04222026.md](OPTAVIEW_NEW_REPO_DESIGN_04222026.md)
