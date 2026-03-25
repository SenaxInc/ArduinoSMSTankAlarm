# Logic & Decision Pathway Review — TankAlarm v1.1.9

**Document**: `LOGICREVIEW-20260324-2-Copilot-v2.0`  
**Author**: GitHub Copilot (Claude Opus 4.6)  
**Date**: March 24, 2026  
**Scope**: All decision-making functions across Server, Client, Viewer, and Common library  
**Firmware Version Reviewed**: 1.1.9 (Server, Client, Viewer)  
**Supersedes**: `LOGICREVIEW-20260324-1-Copilot-v1.0` (reviewed firmware 1.1.0)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Methodology](#2-methodology)
3. [What Changed Since v1.1.0](#3-what-changed-since-v110)
4. [Architecture-Level Decision Analysis](#4-architecture-level-decision-analysis)
5. [Client Decision Pathways](#5-client-decision-pathways)
   - 5.1 [Sensor Reading & Dispatch](#51-sensor-reading--dispatch)
   - 5.2 [Sensor Validation & Failure Detection](#52-sensor-validation--failure-detection)
   - 5.3 [I2C Bus Management (Three-Tier Escalation)](#53-i2c-bus-management-three-tier-escalation)
   - 5.4 [Alarm Evaluation & Hysteresis](#54-alarm-evaluation--hysteresis)
   - 5.5 [Alarm Rate Limiting (Three-Layer)](#55-alarm-rate-limiting-three-layer)
   - 5.6 [Power Conservation State Machine](#56-power-conservation-state-machine)
   - 5.7 [Battery Failure Fallback & Solar-Only Mode](#57-battery-failure-fallback--solar-only-mode)
   - 5.8 [Notecard Communication & Exponential Backoff](#58-notecard-communication--exponential-backoff)
   - 5.9 [Note Buffering & Offline Resilience](#59-note-buffering--offline-resilience)
   - 5.10 [Daily Report Generation](#510-daily-report-generation)
   - 5.11 [Relay Control & Command Cooldown](#511-relay-control--command-cooldown)
   - 5.12 [Sleep/Wake Cycle](#512-sleepwake-cycle)
   - 5.13 [Config Application & Validation](#513-config-application--validation)
   - 5.14 [DFU (Firmware Update)](#514-dfu-firmware-update)
   - 5.15 [Unload Event Detection](#515-unload-event-detection)
   - 5.16 [RPM/Pulse Counting](#516-rpmpulse-counting)
6. [Server Decision Pathways](#6-server-decision-pathways)
   - 6.1 [SMS Dispatch Gates & Rate Limiting](#61-sms-dispatch-gates--rate-limiting)
   - 6.2 [Stale Client Detection & Auto-Pruning](#62-stale-client-detection--auto-pruning)
   - 6.3 [24-Hour Change Tracking](#63-24-hour-change-tracking)
   - 6.4 [Daily Email Scheduling (UTC)](#64-daily-email-scheduling-utc)
   - 6.5 [Calibration Learning System](#65-calibration-learning-system)
   - 6.6 [Config Dispatch & ACK Lifecycle](#66-config-dispatch--ack-lifecycle)
   - 6.7 [Server-Down Detection](#67-server-down-detection)
   - 6.8 [Authentication & Session Management](#68-authentication--session-management)
   - 6.9 [Viewer Summary Publishing](#69-viewer-summary-publishing)
   - 6.10 [FTP Backup & Archival](#610-ftp-backup--archival)
   - 6.11 [Daily Report Reconciliation](#611-daily-report-reconciliation)
   - 6.12 [Polling Loop Timing](#612-polling-loop-timing)
7. [Viewer Decision Pathways](#7-viewer-decision-pathways)
   - 7.1 [Summary Fetch & Scheduling](#71-summary-fetch--scheduling)
   - 7.2 [Notecard Health & I2C Recovery](#72-notecard-health--i2c-recovery)
   - 7.3 [Web Server & HTTP Handling](#73-web-server--http-handling)
   - 7.4 [DFU Update Flow](#74-dfu-update-flow)
   - 7.5 [Time Synchronization](#75-time-synchronization)
   - 7.6 [Memory Management](#76-memory-management)
8. [Common Library Decision Points](#8-common-library-decision-points)
   - 8.1 [Shared I2C Recovery (TankAlarm_I2C.h)](#81-shared-i2c-recovery-tankalarm_i2ch)
   - 8.2 [Shared Diagnostics (TankAlarm_Diagnostics.h)](#82-shared-diagnostics-tankalarm_diagnosticsh)
   - 8.3 [Atomic Write Infrastructure](#83-atomic-write-infrastructure)
   - 8.4 [Schema Versioning](#84-schema-versioning)
9. [Cross-System Decision Interactions](#9-cross-system-decision-interactions)
10. [Findings Summary](#10-findings-summary)
    - 10.1 [Important Findings](#101-important-findings)
    - 10.2 [Moderate Findings](#102-moderate-findings)
    - 10.3 [Minor Findings](#103-minor-findings)
    - 10.4 [Previously Reported — Now Resolved](#104-previously-reported--now-resolved)
11. [Scorecard](#11-scorecard)

---

## 1. Executive Summary

This document is a scholarly review of every decision-making function in the TankAlarm firmware suite (v1.1.9), spanning the Client (~7,571 lines), Server (~13,400 lines), Viewer (~997 lines), and shared Common library (~10 header files). The review evaluates whether each logical branch is tied to a reasonable foundation and whether the code is making the best defensible decision at every fork.

**Overall assessment: 4.5/5 — Production-quality embedded firmware with strong defensive patterns.**

The v1.1.9 release represents a significant maturation over v1.1.0. Two entirely new shared headers (`TankAlarm_I2C.h`, `TankAlarm_Diagnostics.h`) have been introduced, extracting I2C bus recovery and health telemetry into reusable, testable components. The I2C failure handling has been redesigned from a flat counter into a three-tier escalation with exponential backoff and a circuit breaker — a textbook embedded-systems pattern. The previously reported `delay()` issue in the Viewer has been fully resolved.

This review identifies **2 important findings**, **8 moderate findings**, and **8 minor findings**. No critical safety vulnerabilities were found — the alarm, relay, and power-management decision paths are all well-guarded with hysteresis, debounce, and fail-safe defaults.

---

## 2. Methodology

Every `.ino` file and every shared header was read in full. Decision points were cataloged by:

1. **Tracing every conditional branch** (if/else, switch, ternary) in functions with side-effects (I/O, state mutation, communication).
2. **Mapping threshold constants** back to `TankAlarm_Config.h` to verify they have sensible defaults and are documented.
3. **Checking for feedback loops** — sequences where output feeds back to input (alarm → relay → measurement → alarm).
4. **Checking for race conditions** — shared state accessed from ISRs, timers, or multiple code paths.
5. **Evaluating recovery paths** — what happens when I2C fails, cellular fails, flash fails, power drops.
6. **Assessing cross-system consistency** — do the Client's outbound assumptions match the Server's inbound parsing?

Each finding is rated:

| Level | Meaning |
|-------|---------|
| **Important** | Could cause incorrect behavior in a plausible field scenario. Should address. |
| **Moderate** | An inconsistency, gap, or fragility that may manifest under edge conditions. Consider addressing. |
| **Minor** | A stylistic, documentation, or theoretical concern with low practical risk. |

---

## 3. What Changed Since v1.1.0

| Area | v1.1.0 | v1.1.9 |
|------|--------|--------|
| I2C error handling | Flat failure counter, single recovery | Three-tier: Notecard-only, sensor-only (with circuit breaker + exponential backoff), dual-failure escalation (recovery → forced reset) |
| I2C functions | Duplicated per-sketch | Shared library (`TankAlarm_I2C.h`) with `tankalarm_recoverI2CBus()`, `tankalarm_scanI2CBus()`, `tankalarm_readCurrentLoopMilliamps()` |
| Diagnostics | Ad-hoc heap prints | Shared `TankAlarm_Diagnostics.h` with `TankAlarmHealthSnapshot` struct |
| Relay command safety | No cooldown | `RELAY_COMMAND_COOLDOWN_MS` (5s) prevents toggling from stale queued notes |
| Schema versioning | None | `NOTEFILE_SCHEMA_VERSION = 1` stamped on every outbound note |
| Voltage divider monitoring | Not present | `VinMonitorConfig` struct for direct ADC battery monitoring |
| Solar-only mode | Not present | `SolarOnlyConfig` with startup debounce, sensor gating, sunset protocol |
| Viewer `delay()` | Blocking calls in main loop | Fully non-blocking; all delays via `safeSleep()` with watchdog kicks |
| Viewer I2C recovery | No recovery mechanism | Full exponential-backoff health check with bus recovery |
| Wire timeout | Not set | `I2C_WIRE_TIMEOUT_MS = 25` prevents indefinite clock stretching |

---

## 4. Architecture-Level Decision Analysis

The TankAlarm system operates on a **publish-subscribe cellular mesh** via Blues Wireless Notehub:

```
Client(s) ──[telemetry.qo]──→ Notehub ──[Route Relay]──→ Server ──[viewer_summary.qo]──→ Viewer(s)
Server ──[config.qo]──→ Client(s)           Client ──[config_ack.qo]──→ Server
```

**Key architectural decisions:**

1. **Fire-and-forget with reconciliation.** Notes are enqueued locally and may be delivered out-of-order or not at all. The daily report serves as a reconciliation checkpoint — the Server compares alarm states in the daily report against its own records and resolves discrepancies. *This is well-suited to lossy cellular links.*

2. **No direct Client-to-Client communication.** Relay commands route through the Server. This centralizes authorization but adds latency. *Appropriate for the security model — the Server is the trust boundary.*

3. **UTC everywhere.** All epoch values are UTC. Local time is never used in firmware. The UI presents UTC without labeling it — a known friction point (see §6.4).

4. **Hold-last-value on sensor failure.** When a current-loop read fails, the Client retains the previous measurement rather than zeroing it. *Correct for preventing false low-level alarms during transient I2C glitches.*

5. **Conservative power: step-up only.** Recovery from low-power states goes one level at a time (CRITICAL → LOW → ECO → NORMAL), never jumping directly to NORMAL. *Prevents oscillation on marginal battery.*

---

## 5. Client Decision Pathways

### 5.1 Sensor Reading & Dispatch

**Function:** `readMonitorSensor()` (~L4159)

| Sensor Type | Handler | Averaging | Retry |
|-------------|---------|-----------|-------|
| `SENSOR_DIGITAL` | `readDigitalSensor()` | None (binary) | None |
| `SENSOR_ANALOG` | `readAnalogSensor()` | 8-sample, 2ms settling | None |
| `SENSOR_CURRENT_LOOP` | `readCurrentLoopSensor()` | **None** | Hold-last-value on failure |
| `SENSOR_PULSE` | `readPulseSensor()` | ISR-accumulated | None |

**Current Loop Reading** (`readCurrentLoopSensor()`, ~L4027):

The shared `tankalarm_readCurrentLoopMilliamps()` performs up to 3 I2C retries with 5ms inter-retry delay. On persistent failure, it returns a negative sentinel. The calling function detects the negative and holds the previous reading.

Conversion to level depends on `currentLoopType`:
- **Ultrasonic:** `mountHeight − distanceInches` (distance-to-level inversion)
- **Pressure:** Linear map from 4–20 mA to configured range, plus mount height offset
- **Generic:** `(mA − 4) / 16 × rangeMax`

All paths clamp the result to `≥ 0`.

**Finding F-1 (Moderate):** The analog sensor path performs 8-sample averaging for noise rejection, but the current-loop path accepts a single I2C reading (with retry on failure, but not multi-sample averaging). A transient noisy reading that passes the retry check is accepted verbatim. Consider 2–4 sample averaging to match the analog path's robustness.

### 5.2 Sensor Validation & Failure Detection

**Function:** `validateSensorReading()` (~L3818)

| Check | Threshold | Action |
|-------|-----------|--------|
| Out of `[minValid, maxValid]` range | 5 consecutive failures | `sensorFailed = true`, send `sensor-fault` alarm |
| Stuck reading (`|reading − lastValid| < 0.05`) | 10 consecutive identical | `sensorFailed = true`, send `sensor-stuck` alarm |
| Valid reading after failure | 1 valid reading | Clear `sensorFailed`, send `sensor-recovered` |

**Finding F-2 (Moderate):** Sensor recovery is instantaneous — a single valid reading after 10 stuck readings clears `sensorFailed`. A flaky sensor oscillating between stuck and not-stuck would generate a `sensor-stuck` / `sensor-recovered` alarm pair every 10+1 readings. Consider requiring 2–3 consecutive valid readings before clearing (recovery debounce).

**Finding F-3 (Minor):** The stuck-detection tolerance of `0.05` is hard-coded. For high-resolution sensors with very small signal ranges (e.g., 0–1 PSI), 0.05 units may be within normal noise, producing false stuck alarms. Making this configurable or proportional to `(rangeMax − rangeMin)` would improve universality.

### 5.3 I2C Bus Management (Three-Tier Escalation)

This is the most architecturally significant change in v1.1.9. The I2C failure handling is now a three-tier escalation ladder:

#### Tier 1 — Notecard-Only Failure

**Location:** `checkNotecardHealth()` (~L3068) and main loop health check block (~L1431)

| Trigger | `gNotecardFailureCount ≥ NOTECARD_FAILURE_THRESHOLD (5)` |
|---------|----------------------------------------------------------|
| Action | Mark `gNotecardAvailable = false`, begin exponential-backoff health probes |
| Recovery probe | `card.version` (lightweight I2C round-trip) |
| Probe schedule | 5 min → 10 min → 20 min → 40 min → 80 min (capped) |
| Floor in ECO | 10 min |
| Floor in LOW_POWER+ | 20 min |
| Bus recovery trigger | After 10 cumulative probe failures → `tankalarm_recoverI2CBus()` + rebind |
| On recovery | Reset all counters, resume normal operation |

**Assessment:** Sound exponential backoff. The power-state-aware floor prevents battery drain from frequent probes in degraded states.

**Modem stall detection** (~L3119): If `millis() − gLastSuccessfulNoteSend > 4 hours` and at least one note has been sent, forces `card.restart`. Catches stuck-modem scenarios where I2C responds but cellular is dead.

#### Tier 2 — Sensor-Only I2C Failure

**Location:** Main loop (~L1540–1596)

| Phase | Condition | Action |
|-------|-----------|--------|
| Detection | All current-loop sensors failed while Notecard is OK | Increment `consecutiveSensorOnlyFailLoops` |
| Recovery attempt | `loops ≥ threshold × backoff` (10 × 1, 10 × 2, 10 × 4, 10 × 8) | `recoverI2CBus()`, reset sensor failure counters |
| Circuit breaker | `sensorRecoveryTotalAttempts ≥ 5` | Permanent fault declaration, stop retrying |
| Full recovery | Any sensor comes back online | Reset all backoff, counters, and circuit breaker |

**Assessment:** Textbook exponential backoff with circuit breaker. The full reset on recovery prevents stale state from poisoning future failure handling.

**Finding F-4 (Moderate):** The dual-failure and sensor-only thresholds are measured in **loop iterations**, not wall-clock time. In `NORMAL` state (100ms sleep), 30 loops = 3 seconds. In `CRITICAL_HIBERNATE` (5-minute sleep), 30 loops = 2.5 hours. The recovery timing varies by 3,000× depending on power state. Consider using a millis-based threshold for deterministic behavior.

#### Tier 3 — Dual-Failure (Notecard AND Sensors Down)

**Location:** Main loop (~L1500–1535)

| Trigger | Both Notecard and current-loop(s) failed simultaneously |
|---------|--------------------------------------------------------|
| At 30 loops | `recoverI2CBus()` + Notecard rebind |
| At 120 loops | Infinite `delay(100)` → watchdog triggers forced hardware reset |

**Assessment:** The nuclear option at 120 loops is the correct last resort — if both communication and sensing are down, the device is non-functional and a hardware reset is the best chance of recovery. The `delay(100)` intentionally starves the watchdog (30-second timeout → fires within 30 seconds of entering the spin loop).

### 5.4 Alarm Evaluation & Hysteresis

**Function:** `evaluateAlarms()` (~L4225–4345)

**Analog/Current-Loop alarms:**

```
Entry condition:  reading ≥ highThreshold  (3× debounce)
Exit condition:   reading < (highThreshold − hysteresis) AND reading > (lowThreshold + hysteresis)  (3× debounce)
```

- High alarm latching clears low alarm (mutual exclusion — prevents simultaneous high+low).
- Skipped entirely if `sensorFailed == true` — prevents alarms from stale held-last-value data.
- Digital sensors use the same debounce (3 samples) but only `highAlarmLatched` (binary: trigger/not).

**Assessment:** The hysteresis model is well-designed. The dead zone between exit threshold and trigger threshold is exactly `hysteresis` — a reading hovering in this band triggers neither entry nor exit, which is the correct behavior for a latched alarm. A latched alarm persists until the reading moves clearly out of the alarm band.

### 5.5 Alarm Rate Limiting (Three-Layer)

**Function:** `checkAlarmRateLimit()` (~L4425)

| Layer | Threshold | Scope |
|-------|-----------|-------|
| Per-sensor interval | 300 seconds (5 min) | Per monitor |
| Per-sensor hourly cap | 10 alarms/hour | Per monitor, sliding window |
| Global hourly cap | 30 alarms/hour | All monitors combined |
| Clear/recovery notes | **Never rate-limited** | — |

Clear and recovery notes bypass all rate limiting — the Server always sees alarm resolution. This is critical for operational safety.

The alarm timestamps use a sliding-window array pruned of entries >1 hour old. State is not persisted across reboots, which means a reboot clears rate-limiting counters. This is acceptable — a reboot implies a power event that justifies fresh alarm evaluation.

### 5.6 Power Conservation State Machine

**Function:** `updatePowerState()` (~L5611)

| State | Voltage Entry | Voltage Exit | Sleep | Sample Multiplier |
|-------|--------------|-------------|-------|-------------------|
| NORMAL | — | — | 100 ms | 1× |
| ECO | < 12.0 V | ≥ 12.4 V | 5 s | 2× |
| LOW_POWER | < 11.8 V | ≥ 12.3 V | 30 s | 4× |
| CRITICAL_HIBERNATE | < 11.5 V | ≥ 12.2 V | 5 min | ∞ (skip sampling) |

**Key design decisions:**
- **Step-up recovery only.** CRITICAL → LOW → ECO → NORMAL — never skipping a level. Prevents oscillation on marginal battery.
- **3-reading debounce** on all transitions. Prevents flicker from transient voltage dips.
- **Hysteresis bands** of 0.4–0.7 V between entry and exit voltages — well-suited for lead-acid battery characteristics.
- **Relay safety:** All relays de-energized on entering CRITICAL. Not re-energized until exiting CRITICAL.
- **Conservative voltage source:** Uses the **minimum** of all available voltage sources (SunSaver Modbus, Notecard `card.voltage`, VIN divider).

**Finding F-5 (Moderate):** Remote-tunable power thresholds (`gConfig.powerEcoEnterV`, etc.) are accepted from the Server without cross-field validation. A misconfigured push where `ecoEnter > ecoExit` or thresholds are out of order could cause the state machine to behave unpredictably. Consider adding validation: `criticalEnter < lowEnter < ecoEnter < ecoExit < lowExit`.

### 5.7 Battery Failure Fallback & Solar-Only Mode

**Solar-Only Sensor Gating** (`isSensorVoltageGateOpen()`, ~L5480):
When in solar-only mode (no battery), sensor reading is gated by voltage — prevents garbage readings during brownout/sunrise. The startup debounce waits for `startupDebounceVoltage` (default 10V) to stabilize for `startupDebounceSec` (default 30s) before allowing sensor reads. A 5-minute safety timeout prevents infinite wait.

**Sunset Protocol** (`checkSolarOnlySunsetProtocol()`, ~L5500):
Requires *both* conditions: `VIN < sunsetVoltage` AND `VIN ≤ previousVin` (declining). Prevents false trigger from momentary cloud cover. After confirmation delay, the Client saves state to flash, flushes pending data, and sends a sunset notification before power is lost. State is persisted atomically.

**Battery Failure Fallback** (~L5750):
In CRITICAL state, a counter increments. After `batteryFailureThreshold` (default 10) entries into CRITICAL, declares battery failure and enables solar-only behaviors. A 24-hour decay mechanism (`SOLAR_BAT_FAIL_DECAY_MS`) prevents slow accumulation from sporadic CRITICAL dips. Full recovery (ECO or better) resets the fallback.

**Assessment:** The dual-condition sunset trigger and the 24-hour decay on the battery failure counter are well-reasoned defenses against false positives. The startup debounce with safety timeout is a good balance of caution and liveness.

### 5.8 Notecard Communication & Exponential Backoff

**Function:** `publishNote()` (~L6187)

Every failure path (null request, null response, error string in response) buffers the note to flash for later retry. On success, the Client opportunistically flushes up to 20 buffered notes.

Every outbound note body is stamped with `_sv` (schema version), enabling the Server to detect Client firmware age and handle protocol differences.

**Telemetry outbox trimming** (`trimTelemetryOutbox()`, ~L6013):
Before each telemetry publish, the Client queries the Notecard outbox and deletes oldest notes beyond `TELEMETRY_OUTBOX_MAX_PENDING` (15). Multi-pass retry with a zero-deletion abort guard prevents infinite loops. Watchdog kick per pass and per delete.

### 5.9 Note Buffering & Offline Resilience

**Write path** (`bufferNoteForRetry()`, ~L6262): Append-only `.log` file, tab-delimited.

**Read path** (`flushBufferedNotes()`, ~L6350): Read line-by-line, attempt re-publish, failures rewritten to `.tmp` file, atomic rename at end. Capped at 20 notes per flush call.

**Pruning** (`pruneNoteBufferIfNeeded()`): Over `NOTE_BUFFER_MAX_BYTES` (16 KB), keeps last `16 KB − 2 KB` headroom.

**Assessment:** The atomic rename on flush prevents corruption. The 20-per-call cap prevents watchdog starvation. LittleFS handles wear leveling internally, so the append-prune cycle is acceptable for the target hardware.

### 5.10 Daily Report Generation

**Function:** `sendDailyReport()` (~L5802)

**Three independent trigger paths ensure daily reports never go missing:**
1. **Standard:** `currentEpoch() ≥ nextDailyReportEpoch` — epoch-aligned scheduling.
2. **Solar-only:** If `hoursSinceLastReport ≥ opportunisticReportHours` — reports whenever power is available.
3. **Millis fallback:** If `currentEpoch() ≤ 0` (no time sync) and `millis() > 24h` — fires every ~24h based on hardware timer.

**Multi-part splitting:** Each part targets 960 bytes. Part 0 includes VIN voltage, solar data, battery data, power state, signal strength, and an active alarm summary. The alarm summary acts as a **backup notification path** — if original alarm notes were lost in transit, the daily report reconciles alarm state with the Server.

**I2C error alerting:** At the end of each daily report, if `gCurrentLoopI2cErrors ≥ 50` in the past 24 hours, publishes an `i2c-error-rate` alarm note. Counters are then reset.

**Finding F-6 (Minor):** The millis-based fallback counter wraps at ~49.7 days. After wrap, `millis() > 24h` is false until millis accumulates 24 hours again, creating a ~24-hour reporting gap once every ~50 days. Extremely low practical risk.

### 5.11 Relay Control & Command Cooldown

**Relay activation via alarm** (`sendAlarm()`, ~L4530): Activate-once guard (`!gRelayActiveForMonitor[idx]`) prevents re-triggering while active. In `UNTIL_CLEAR` mode, deactivation is automatic when the alarm clears.

**Momentary timeout** (`checkRelayMomentaryTimeout()`, ~L6853): Per-relay independent duration (default 1800s). Uses unsigned subtraction for millis overflow safety.

**Command cooldown** (`processRelayCommand()`, ~L6715): `RELAY_COMMAND_COOLDOWN_MS` (5000ms) global cooldown between any relay command. Prevents rapid toggling from stale queued notes or route replays.

**CRITICAL_HIBERNATE safety:** Relays are de-energized on entering CRITICAL and not re-activated while in CRITICAL.

**Assessment:** Comprehensive relay safety. The cooldown is globally shared (relay 1 command blocks relay 2 for 5 seconds), which is slightly aggressive but errs on the side of safety.

### 5.12 Sleep/Wake Cycle

`safeSleep()` (~L7505) chunks all delays into segments ≤ half the watchdog timeout (15 seconds), kicking the watchdog between chunks. Uses `rtos::ThisThread::sleep_for()` on Mbed OS for true low-power RTOS sleep.

Power-state-aware sample intervals: ECO = 2× normal, LOW_POWER = 4×, CRITICAL = skip entirely. Inbound polling frequency is similarly multiplied (ECO = 4×, LOW_POWER = 12×, CRITICAL = skip).

**Finding F-7 (Minor):** In CRITICAL_HIBERNATE, the Client sends no telemetry — the Server has no visibility into whether the Client is alive or dead. A single daily heartbeat note (at minimal cost: one I2C transaction + one cellular send) would maintain server awareness without significant power impact.

### 5.13 Config Application & Validation

**Function:** `applyConfigUpdate()` (~L3494)

"Update if present" semantics — absent JSON fields retain current values. Key validation guards:

| Field | Guard |
|-------|-------|
| `sensorMountHeight` | `fmaxf(0.0f, ...)` |
| `unloadDropPercent` | `constrain(10.0f, 95.0f)` |
| `relayMomentarySeconds[]` | Capped at 86400 |
| `vinMonitor.analogPin` | > 7 → default |
| `vinMonitor.r1Kohm/r2Kohm` | ≤ 0 → default |
| `pulsesPerUnit` | `max(1, ...)` |

**Hardware change detection** triggers `reinitializeHardware()` → Wire.end/begin, Notecard rebind, hub.set reconfigure, power state reset to NORMAL.

**Config schema versioning** (`CONFIG_SCHEMA_VERSION`): On mismatch, logs warning, loads with defaults for missing fields (via initial `memset`), re-saves with current version. Forward and backward compatible.

**Finding F-8 (Minor):** `reinitializeHardware()` resets all monitor debounce and alarm state, even for monitors that weren't changed. If the server pushes a config change affecting only monitor B, monitor A's in-progress alarm debounce is also reset. Generally acceptable since "hardware changed" implies a system-wide reset, but could cause a transient alarm gap for unrelated monitors.

### 5.14 DFU (Firmware Update)

**Decision gate:** Only in NORMAL or ECO power state (`powerState ≤ ECO`), Notecard available, not already in progress.

**Finding F-9 (Minor):** No automatic rollback mechanism. If the new firmware is broken, the device enters a crash-loop with no revert path. This is a known limitation of the Blues DFU model. A boot-success flag (set after N successful loop iterations, checked by early boot code) could provide crash-loop detection and potential revert.

### 5.15 Unload Event Detection

Tracks fill-and-empty cycles. Detects peak and trough levels, timestamps, raw mA. SMS sent if requested. Logged to a ring buffer. The final (empty) level updates the sensor record.

### 5.16 RPM/Pulse Counting

Cooperative non-blocking state machine with 50ms polling bursts. Three sub-modes: accumulated (ISR-backed), time-based, pulse-counting. Edge debounce at 2ms. Accumulated mode uses `noInterrupts()` for safe ISR counter reads.

**Finding F-10 (Minor):** The pulse sampler is only polled during `sampleMonitors()` calls. Between sample intervals, edge detection is lost unless `pulseAccumulatedMode` (ISR-backed) is enabled. For low-RPM applications, consider recommending ISR mode.

---

## 6. Server Decision Pathways

### 6.1 SMS Dispatch Gates & Rate Limiting

**Function:** `handleAlarm()` (~L8236)

**Decision chain for sensor alarms:**
1. `smsEnabled` starts `false` — forcibly set to `true` for high/low/clear/digital alarms.
2. `smsAllowedByServer` gates on `gConfig.smsOnHigh` / `smsOnLow` / `smsOnClear`.
3. Final gate: `!isDiagnostic && smsEnabled && smsAllowedByServer && checkSmsRateLimit(rec)`.

**Rate limiting** (`checkSmsRateLimit()`, ~L8996): Per-sensor minimum 300s interval + per-sensor 2/hour cap. Sliding window with explicit timestamp arrays. State persisted across reboots (loaded from sensor registry) — prevents post-reboot SMS storms.

**Recipient resolution:** Contacts config JSON → `smsAlertRecipients` → phone numbers. Falls back to legacy `gConfig.smsPrimary`/`smsSecondary`.

**Finding F-11 (Moderate):** `sensor-recovered` alarms are blocked from SMS by the `isDiagnostic` guard (same as `sensor-fault` and `sensor-stuck`). Operators may want to know when a sensor recovers — the recovery notification closes the alert loop. Consider exempting `sensor-recovered` from the diagnostic guard, or adding a separate `smsOnRecovered` config option.

**Finding F-12 (Minor):** If `currentEpoch() ≤ 0.0` (no time sync), `checkSmsRateLimit()` returns `true` (allow). This prevents missed critical alerts at startup, but if the Notecard fails permanently, SMS bypasses rate limiting indefinitely. Low risk — the Notecard failure would itself be detected by the health check.

### 6.2 Stale Client Detection & Auto-Pruning

**Function:** `checkStaleClients()` (~L9934)

**Five-phase architecture:**
1. **Deduplication** — proactive `deduplicateSensorRecordsLinear()`.
2. **Per-sensor freshness** — finds latest update, counts orphan sensors (stale > 72h).
3. **Orphan pruning** — if some sensors are fresh and some stale beyond 72h, prune the stale ones. Safety guard: won't prune if *all* sensors are stale (defers to dead-client removal).
4. **Stale alert** — if client offline > 49 hours, send one-time SMS. Reset flag when client resumes.
5. **Auto-removal** — if offline > 7 days, archive to FTP and remove. Deferred removal avoids iterator invalidation.

The 49-hour stale threshold allows for two missed daily reports before alerting — reasonable for field equipment with intermittent cellular. The staleAlertSent flag is runtime-only (not persisted), intentionally — a reboot implies a possible power event, and re-alerting is appropriate.

### 6.3 24-Hour Change Tracking

**Location:** `handleTelemetry()` (~L8200) and `handleDaily()` (~L8750)

```c
if (now − lastUpdateEpoch ≥ 22 hours) {
    previousLevelInches = levelInches;
    previousLevelEpoch = lastUpdateEpoch;
}
```

The 22-hour threshold (not 24) prevents clock drift from missing the rollover window when clients report approximately every 24 hours.

**Finding F-13 (Moderate):** For high-frequency reporters (e.g., 15-minute intervals), `now − lastUpdateEpoch` is always << 22 hours, so `previousLevel` is never rolled. The 24h delta shown on the dashboard is actually "level at first boot or first 22h+ gap" vs "current level." For continuously-reporting sensors, the delta becomes increasingly stale over time. Consider a secondary timer that rolls `previousLevel` every 24 hours regardless of reporting frequency.

### 6.4 Daily Email Scheduling (UTC)

**Function:** `scheduleNextDailyEmail()` (~L5560)

The user enters `dailyHour`/`dailyMinute` via a web UI `<input type="time">`, which stores the value as-is. The scheduling math treats it as UTC offset from midnight UTC.

**Finding F-14 (Moderate):** The web UI label says "Daily Email Time (HH:MM)" without specifying timezone. Users will likely enter local time and be surprised when the email arrives at the wrong hour. The Mbed OS platform doesn't support timezones, so the simplest fix is adding "(UTC)" to the UI label. Alternatively, the UI could provide a timezone offset dropdown and adjust the stored hour at save time.

### 6.5 Calibration Learning System

**Function:** `recalculateCalibration()` (~L12136)

**Decision tree for regression type:**
1. Need ≥ 5 entries with valid temperature AND ≥ 10°F temperature range → multiple linear regression (Cramer's rule on 3×3 normal equations): `level = offset + slope × mA + tempCoef × (temp − 70°F)`.
2. Otherwise → simple linear regression: `level = slope × mA + offset`.
3. Singular matrix (`|det| < 0.0001`) → fall back to simple regression.

**Conversion priority** (`convertMaToLevelWithTemp()`, ~L9430):
1. Learned calibration (if available)
2. Config-based theoretical (pressure, ultrasonic, or generic range mapping)
3. Fallback: `(mA − 4) / 16 × 100`

**Finding F-15 (Minor):** Negative level is clamped to 0 for the learned calibration path and for the ultrasonic config path, but not for the generic pressure config path. A sensor reading below `rangeMin` could produce a negative level that is passed through to the dashboard. Low practical risk but inconsistent with the other paths.

### 6.6 Config Dispatch & ACK Lifecycle

**Function:** `dispatchClientConfig()` (~L7830)

1. Serialize to 8 KB buffer → cache locally (survives Notecard downtime).
2. Prune orphaned sensors from old config.
3. Clear sensor-stuck alarms if stuck detection disabled.
4. Generate config version hash (djb2) for ACK tracking.
5. Set `pendingDispatch = true`, `dispatchAttempts = 1`.
6. Send via Notecard.

**Auto-retry** (`dispatchPendingConfigs()`, ~L7932): Every 60 minutes, up to 5 attempts. Attempt counter incremented *before* send (ensures progress even if I2C hangs). Watchdog kicked before each client.

**ACK handling** (`handleConfigAck()`, ~L10170): Clears `pendingDispatch` when version hash matches. Triggers orphan pruning on status `"applied"`.

**Finding F-16 (Moderate):** The manual retry endpoint (`handleConfigRetryPost()`) clears `pendingDispatch = false` immediately on successful send, *before* the client ACKs. This is inconsistent with the automatic path (which leaves `pendingDispatch = true` until ACK). If the note is lost between Notecard and Notehub, the auto-retry system won't re-send because `pendingDispatch` is already false. The manual path should match the automatic path: leave pending until ACK.

### 6.7 Server-Down Detection

**Location:** Main loop (~L2676), one-time boot check.

Compares `currentEpoch()` vs `gLastHeartbeatFileEpoch` (persisted hourly). If gap ≥ 24 hours, sends SMS. Gate flag `gServerDownChecked` prevents re-triggering.

The inherent imprecision is at most ~59 minutes (heartbeat persisted hourly), which is acceptable for a 24-hour detection threshold.

### 6.8 Authentication & Session Management

Exponential backoff: 1s → 2s → 4s → 8s → 16s, lockout at 5 failures for 30 seconds. PIN comparison uses constant-time comparison (prevents timing attacks). Session tokens generated from 64-bit entropy (ADC noise + timing jitter via LCG PRNG). Single-session enforcement — new login invalidates previous.

**Assessment:** Well-hardened. Previous reviews identified auth issues that have been resolved.

### 6.9 Viewer Summary Publishing

**Function:** `publishViewerSummary()` (~L9290)

Gated by `gConfig.viewerEnabled`. Serializes all sensor records + 24h deltas + VIN voltage. Uses `sendRequest()` (non-blocking) rather than `requestAndResponse()`.

### 6.10 FTP Backup & Archival

**Function:** `archiveClientToFtp()` (~L10220)

Eligibility: `MIN_ARCHIVE_AGE_SECONDS` (30 days) — prevents archiving short-lived test clients. Filename built from sanitized site name + date range + UID suffix. Local manifest updated in `/fs/archived_clients.json`.

**Finding F-17 (Minor):** The archived clients manifest file is written with `fopen("w")` directly, not via `tankalarm_posix_write_file_atomic()`. Power failure during write corrupts the manifest. Other critical files use atomic writes — the manifest should too.

### 6.11 Daily Report Reconciliation

**Function:** `handleDaily()` (~L8560)

**Two-way alarm reconciliation:**
1. **Missed alarm detection:** If client's daily report shows an active alarm that the server doesn't know about → update server state, log "Missed alarm detected."
2. **Orphaned alarm clearing:** If server has an active alarm but client's daily report shows no alarm → clear server-side alarm. Skips system alarms (solar/battery/power).

**Part-loss detection:** Tracks received parts as a bitmask. On final part, verifies all parts 0..N were received. Detects new report batches by 30-minute epoch gap.

**Assessment:** This reconciliation layer is one of the strongest architectural decisions in the system. It turns the daily report into a reliable checkpoint that compensates for the inherently lossy cellular transport.

### 6.12 Polling Loop Timing

**Function:** `loop()` (~L2615)

| Task | Interval | Notes |
|------|----------|-------|
| Watchdog kick | Every iteration | — |
| Ethernet maintain | Every iteration | DHCP lease renewal |
| Link check | 5 s | — |
| Web requests | Every iteration | Non-blocking |
| Notecard health | Exponential backoff (5 min base, 80 min max) | — |
| `pollNotecard()` | 5 s | Skipped if paused |
| Config retry | 60 min | If pending configs exist |
| Time sync | Periodic (common header) | — |
| Server-down check | One-time boot | — |
| Heartbeat persist | 1 hour | — |
| Daily email | Scheduled epoch | `epoch ≥ gNextDailyEmailEpoch` |
| Viewer summary | Scheduled epoch | `epoch ≥ gNextViewerSummaryEpoch` |
| History maintenance | 1 hour | Multiple sub-tasks |
| DFU check | `DFU_CHECK_INTERVAL_MS` | — |
| Voltage check | 15 min | — |
| Sensor registry save | 5 min | If dirty |
| Metadata save | If dirty | — |
| Stale client check | 1 hour | — |
| Config save | If dirty | — |
| FTP auto-backup | If dirty + enabled | — |

`processNotefile()` caps at 10 notes per file per poll cycle with 1ms yield between notes. Notecard failure tracking sets `gNotecardAvailable = false` at threshold.

---

## 7. Viewer Decision Pathways

### 7.1 Summary Fetch & Scheduling

**Function:** `fetchViewerSummary()` (~L733)

Drain loop: reads ALL queued `viewer_summary.qi` notes (up to 20), deleting each after reading. Last-write-wins — each note overwrites `gSensorRecords[]` entirely. The 20-note cap prevents unbounded iteration. Watchdog kick per iteration.

Schedule uses `tankalarm_computeNextAlignedEpoch()` to align fetches to the Server's known publication cadence (default: every 6 hours starting at 06:00 UTC).

**Finding F-18 (Important):** If `currentEpoch()` returns 0.0 at boot (cellular not yet connected), `gNextSummaryFetchEpoch` is set to 0.0 by `scheduleNextSummaryFetch()`. The loop guard `gNextSummaryFetchEpoch > 0.0 && currentEpoch() >= gNextSummaryFetchEpoch` then fails permanently. `scheduleNextSummaryFetch()` is never re-called after a later successful time sync unless a fetch succeeds first — a chicken-and-egg deadlock. The Viewer would serve an empty dashboard until rebooted with cellular available. **Recommendation:** Re-schedule on first successful time sync, or add a millis-based fallback fetch check.

### 7.2 Notecard Health & I2C Recovery

Same pattern as Client — exponential backoff from 5 min to 80 min. Bus recovery via shared `tankalarm_recoverI2CBus()` after 10 probe failures. DFU guard prevents recovery during firmware transfer.

**Previous issue — `delay()` blocking:** ✅ **Fully resolved.** The main loop is entirely non-blocking. All delays use `safeSleep()` with watchdog kicks. The only raw `delay()` calls are in `initializeEthernet()` during `setup()` (before watchdog is started) and in `tankalarm_recoverI2CBus()` (microsecond-level for I2C bit-banging).

### 7.3 Web Server & HTTP Handling

| Method | Path | Response |
|--------|------|----------|
| GET | `/` | PROGMEM HTML dashboard (streamed in 128-byte chunks) |
| GET | `/api/sensors` | JSON with all sensor data + metadata |
| * | * | 404 |

**No authentication** — read-only kiosk on local LAN. Acceptable for private industrial network; would need auth for any internet-facing deployment.

HTTP read timeout: 5 seconds per phase (headers, body). Line cap: 512 bytes. Body cap: 1024 bytes. Single-client processing per loop iteration.

**JSON serialization** (`sendSensorJson()`): Heap-allocated `JsonDocument` via `nothrow new` with OOM fallback to HTTP 500. Serialized to Arduino `String`, then sent in 512-byte chunks.

**Finding F-19 (Important):** The Viewer never inspects `NOTEFILE_SCHEMA_VERSION` (`_sv` field) on incoming summaries. If the Server is upgraded to schema v2 with different field structures, the Viewer would silently mis-parse data. Consider adding a version check with a warning log when `_sv > NOTEFILE_SCHEMA_VERSION`.

### 7.4 DFU Update Flow

Checks hourly via `dfu.status`. `DFU_AUTO_ENABLE` is never `#define`d, so the Viewer logs available updates but never self-applies them. Manual trigger via Notehub console required. This is a conscious safety choice for an unattended kiosk.

### 7.5 Time Synchronization

Syncs from Notecard every 6 hours via `card.time`. Between syncs, `currentEpoch()` interpolates using `millis()` delta. The 6-hour re-sync bounds drift to ~0.5 seconds (crystal oscillator accuracy).

### 7.6 Memory Management

Static allocations: `gSensorRecords[64]` (~10.7 KB), `VIEWER_DASHBOARD_HTML[]` in PROGMEM (~4+ KB). All heap `JsonDocument` allocations use `nothrow new` with null checks. Arduino `String` for JSON serialization could cause fragmentation over weeks of operation — acceptable given the STM32H747's 512 KB SRAM.

---

## 8. Common Library Decision Points

### 8.1 Shared I2C Recovery (TankAlarm_I2C.h)

**`tankalarm_recoverI2CBus()`:**
1. DFU guard → skip if firmware transfer in progress.
2. Watchdog callback → kick before the 160μs bit-bang sequence.
3. `Wire.end()` → GPIO bit-bang (16 SCL toggles + STOP condition) → `Wire.begin()` → `Wire.setTimeout(25ms)`.
4. Increments global `gI2cBusRecoveryCount`.

**`tankalarm_scanI2CBus()`:**
Probes expected devices with retry logic (`I2C_STARTUP_SCAN_RETRIES = 3`, 2s delay between retries). Reports unexpected devices (addresses not in the expected set). Results stored in `I2CScanResult` struct for boot telemetry.

**`tankalarm_readCurrentLoopMilliamps()`:**
Up to 3 retries (`I2C_CURRENT_LOOP_MAX_RETRIES`), 5ms delay between retries. Returns negative sentinel on failure. Error counter incremented for each failed attempt.

### 8.2 Shared Diagnostics (TankAlarm_Diagnostics.h)

`tankalarm_freeRam()` uses Mbed's `mbed_stats_heap_get()` for accurate heap measurement. `TankAlarmHealthSnapshot` struct captures: free heap, uptime, watchdog resets, notecard errors, storage errors — published as health telemetry.

### 8.3 Atomic Write Infrastructure

**POSIX path** (`tankalarm_posix_write_file_atomic()`): Write to `.tmp` → `remove()` original → `rename()` tmp to original. Boot recovery: `recoverOrphanedTmpFiles()` handles interrupted rename.

**LittleFS path** (for non-POSIX API users): Similar pattern using LittleFS file handles.

All critical state files (config, sensor registry, solar state, client metadata) use atomic writes. The archived clients manifest is an exception (see F-17).

### 8.4 Schema Versioning

`NOTEFILE_SCHEMA_VERSION = 1` is stamped as `_sv` on every outbound note body via `JAddIntToObject`. This allows the Server to detect Client firmware version and handle protocol differences. The config flash format also has a `CONFIG_SCHEMA_VERSION` — mismatch triggers migration (load with defaults for missing fields, re-save with current version).

---

## 9. Cross-System Decision Interactions

| Interaction | Assessment |
|-------------|------------|
| **Client alarm → Server SMS** | Client sends alarm notes with `sms: true/false`. Server respects the flag AND applies its own SMS policy. Double-gating is correct — neither side can force SMS alone. |
| **Server config → Client ACK** | Config version hash (djb2) ensures ACK matches the dispatched config, not a stale one. Robust. |
| **Server summary → Viewer display** | Server serializes sensor records + 24h deltas. Viewer parses and overwrites. No schema version check on the Viewer side (F-19). |
| **Client daily report → Server reconciliation** | Server compares alarm states in the daily report vs its own records and resolves discrepancies. Strongest reliability mechanism in the system. |
| **Client relay command → Server relay forwarding** | Client sends `relay_forward.qo` → Server re-issues to target client. Relay commands cannot bypass the Server. |
| **I2C recovery across all three devices** | Shared `tankalarm_recoverI2CBus()` ensures identical recovery behavior. Consistent. |
| **Schema version across devices** | Client stamps `_sv=1` on all notes. Server reads it. Viewer does not check it (F-19). |

---

## 10. Findings Summary

### 10.1 Important Findings

| # | Finding | Component | Impact |
|---|---------|-----------|--------|
| **F-18** | Viewer time-sync bootstrap deadlock — if time is unavailable at boot, `gNextSummaryFetchEpoch` stays 0.0 forever. Summary fetch never fires until reboot with cellular. | Viewer | Dashboard shows no data indefinitely |
| **F-19** | Viewer never checks `NOTEFILE_SCHEMA_VERSION` — schema changes cause silent mis-parsing | Viewer | Data integrity risk on firmware mismatch |

### 10.2 Moderate Findings

| # | Finding | Component | Impact |
|---|---------|-----------|--------|
| **F-1** | Current loop has no multi-sample averaging (analog path does 8-sample) | Client | Noisy reading accepted verbatim |
| **F-2** | Sensor recovery is instantaneous (1 valid reading clears failure) — flaky sensors oscillate | Client | Repeated fault/recovery alarm pairs |
| **F-4** | I2C failure thresholds are loop-count-based, varying 3,000× by power state | Client | Non-deterministic recovery timing |
| **F-5** | Remote power thresholds accepted without cross-field validation | Client | Misconfigured push could trap device in wrong power state |
| **F-11** | `sensor-recovered` blocked from SMS by diagnostic guard | Server | Operators miss recovery notifications |
| **F-13** | 24h change tracking frozen for high-frequency reporters | Server | Stale daily deltas on dashboard |
| **F-14** | Daily email time labeled without "(UTC)" in web UI | Server | Users schedule at wrong hour |
| **F-16** | Manual config retry clears `pendingDispatch` before ACK (auto path waits for ACK) | Server | Lost note means no automatic re-send |

### 10.3 Minor Findings

| # | Finding | Component | Impact |
|---|---------|-----------|--------|
| **F-3** | Stuck sensor tolerance (0.05) is hard-coded, not proportional to range | Client | False stuck alarms on high-resolution sensors |
| **F-6** | Millis-based daily report fallback has ~24h gap every ~50 days (millis wrap) | Client | One skipped fallback report per 50 days |
| **F-7** | No heartbeat in CRITICAL_HIBERNATE — server loses visibility | Client | Device appears dead during deep sleep |
| **F-8** | `reinitializeHardware()` resets all monitors' alarm state, not just changed ones | Client | Transient alarm gap for unaffected monitors |
| **F-9** | No DFU rollback on crash-loop | Client | Bricked device on bad firmware |
| **F-10** | Pulse sampler only polled during `sampleMonitors()` — edges lost between samples | Client | Missed pulses in non-ISR mode |
| **F-15** | Negative level not clamped for pressure config path (clamped in learned + ultrasonic) | Server | Inconsistent negative level handling |
| **F-17** | Archived clients manifest not written atomically | Server | Manifest corruption risk on power loss |
| **F-12** | SMS rate limiting bypassed when epoch is 0 (no time sync) | Server | Unbounded SMS if Notecard fails permanently |

### 10.4 Previously Reported — Now Resolved

| Previous Finding | Status in v1.1.9 |
|------------------|-------------------|
| Viewer uses `delay()` in main loop — watchdog starvation risk | ✅ **Fixed.** All delays via `safeSleep()` with watchdog kicks. Fully non-blocking main loop. |
| Viewer has no I2C recovery mechanism | ✅ **Implemented.** Exponential-backoff health check with bus recovery via shared library. |
| I2C recovery duplicated per-sketch | ✅ **Refactored.** Shared `TankAlarm_I2C.h` with `tankalarm_recoverI2CBus()`, `tankalarm_scanI2CBus()`, `tankalarm_readCurrentLoopMilliamps()`. |
| No I2C wire timeout set | ✅ **Fixed.** `Wire.setTimeout(I2C_WIRE_TIMEOUT_MS)` = 25ms set in all three sketches. |
| No relay command cooldown | ✅ **Implemented.** `RELAY_COMMAND_COOLDOWN_MS` = 5000ms. |
| No schema versioning for notefiles | ✅ **Implemented.** `NOTEFILE_SCHEMA_VERSION = 1` stamped on all outbound notes. |

---

## 11. Scorecard

| Category | Rating | Key Strengths | Key Gaps |
|----------|--------|---------------|----------|
| **Sensor Reading** | ★★★★☆ | Hold-last-value, robust analog averaging | No current-loop averaging (F-1) |
| **Sensor Validation** | ★★★★☆ | Consecutive failure counting, stuck detection | Instant recovery (F-2), fixed tolerance (F-3) |
| **I2C Management** | ★★★★★ | Three-tier escalation, exponential backoff, circuit breaker, shared library | Loop-count-vs-time disparity (F-4, pedantic) |
| **Alarm Generation** | ★★★★★ | Proper hysteresis, 3-sample debounce, mutual exclusion | — |
| **Alarm Rate Limiting** | ★★★★★ | Three layers (per-sensor, hourly, global), clear/recovery exempt, persisted across reboots | — |
| **Power Conservation** | ★★★★★ | Step-up recovery, 3-reading debounce, 0.4–0.7V hysteresis, relay safety in CRITICAL | No remote threshold validation (F-5) |
| **Solar-Only Mode** | ★★★★★ | Dual-condition sunset, startup debounce with timeout, 24h decay for battery failure | — |
| **Notecard Communication** | ★★★★★ | Exponential backoff, modem stall detection, buffered retry, outbox trim, schema versioning | — |
| **Daily Reports** | ★★★★★ | Three trigger paths, multi-part splitting, backup alarm summary, I2C error alerting | — |
| **Relay Control** | ★★★★★ | Per-relay timing, command cooldown, activate-once guard, CRITICAL safety | — |
| **Config Handling** | ★★★★★ | Schema versioning, validation guards, update-if-present semantics, hardware change detection | — |
| **SMS Dispatch** | ★★★★☆ | Per-sensor + hourly cap, persisted across reboots, recipient fallback | `sensor-recovered` blocked (F-11) |
| **Stale Detection** | ★★★★★ | Five-phase architecture, orphan pruning safety guard, deferred removal | — |
| **24h Change Tracking** | ★★★☆☆ | 22h threshold prevents drift | Frozen for frequent reporters (F-13) |
| **Daily Email Scheduling** | ★★★★☆ | Correct UTC math, rate-limited | Missing timezone label in UI (F-14) |
| **Calibration** | ★★★★★ | Temperature compensation, singular matrix detection, graceful fallback chain | — |
| **Config Dispatch** | ★★★★☆ | Version hashing, ACK tracking, auto-retry, watchdog awareness | Manual retry premature ACK clear (F-16) |
| **Authentication** | ★★★★★ | Constant-time compare, exponential backoff, single-session, ADC+timing entropy | — |
| **Viewer** | ★★★★☆ | Non-blocking, PROGMEM HTML, OOM-safe JSON, robust I2C recovery | Time-sync deadlock (F-18), no schema check (F-19) |
| **Watchdog Coverage** | ★★★★★ | Every long-running path covered across all three devices. Intentional non-kick for fatal states. | — |
| **Atomic Writes** | ★★★★★ | POSIX atomic write + boot recovery for all critical files | Manifest exception (F-17) |
| **Cross-System** | ★★★★★ | Alarm reconciliation via daily reports, shared I2C library, consistent recovery patterns | — |

**Overall: ★★★★½ — Production-quality embedded firmware with comprehensive defensive engineering.**

---

*End of review.*
