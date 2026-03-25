# Client Firmware Decision-Logic Review

**File:** `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`  
**Version:** 1.1.9 · 7 571 lines  
**Reviewer:** GitHub Copilot (Claude Opus 4.6)  
**Date:** 2026-03-24  
**Scope:** Every decision point in the client — conditions, thresholds, feedback loops, gaps, race conditions.

---

## Table of Contents

1. [Sensor Reading Decisions](#1-sensor-reading-decisions)
2. [I2C Bus Management](#2-i2c-bus-management)
3. [Alarm Generation](#3-alarm-generation)
4. [Notecard Communication](#4-notecard-communication)
5. [Sleep / Wake Cycle](#5-sleep--wake-cycle)
6. [Config Application & Validation](#6-config-application--validation)
7. [Battery / Solar Monitoring](#7-battery--solar-monitoring)
8. [Daily Report Generation](#8-daily-report-generation)
9. [Relay / Output Control](#9-relay--output-control)
10. [DFU (Firmware Update)](#10-dfu-firmware-update)
11. [Watchdog Management](#11-watchdog-management)
12. [New / Advanced Logic](#12-new--advanced-logic)
13. [Cross-Cutting Concerns](#13-cross-cutting-concerns)
14. [Summary Scorecard](#14-summary-scorecard)

---

## 1. Sensor Reading Decisions

### 1.1 Sensor Dispatch — `readMonitorSensor()` (~L4159)

| Condition | Action |
|-----------|--------|
| `SENSOR_DIGITAL` | → `readDigitalSensor()` |
| `SENSOR_ANALOG` | → `readAnalogSensor()` |
| `SENSOR_CURRENT_LOOP` | → `readCurrentLoopSensor()` |
| `SENSOR_PULSE` | → `readPulseSensor()` |
| default | returns 0.0 |

**Assessment:** Sound dispatch table. No gap.

### 1.2 Current Loop Reading — `readCurrentLoopSensor()` (~L4027)

| Condition | Action |
|-----------|--------|
| `sensorRangeMax <= sensorRangeMin` | Return 0.0 (misconfigured) |
| `milliamps < 0.0f` (I2C read failure) | Return **previous** `currentInches` (hold-last-value) |
| `currentLoopType == CURRENT_LOOP_ULTRASONIC` | Distance-to-level inversion: `mountHeight − distanceInches` |
| Pressure | `linearMap(mA, 4→20, rangeMin→rangeMax) × conversionFactor + mountHeight` |
| `levelInches < 0` | Clamped to 0 |

**Assessment:**
- **Sound:** Hold-last-value on failure prevents transient false alarms.
- **Gap:** A single-sample read (`readCurrentLoopMilliamps`) with no retry or multi-sample averaging. Analog sensors do 8-sample averaging (L3987) but the I2C 4–20 mA path does not. A transient I2C glitch produces a noisy reading that is accepted verbatim. Consider multi-reading averaging.
- **Note:** The milliamps < 0 guard relies on the shared library returning a negative sentinel on error — this contract must be verified at the library level.

### 1.3 Analog Voltage Sensor — `readAnalogSensor()` (~L3968)

- 8-sample averaging with 2 ms settling delay between reads.
- Maps voltage linearly to pressure, then converts to inches.
- Validates `sensorRangeMax > sensorRangeMin` and `analogVoltageMax > analogVoltageMin`.
- Clamps to 0 at minimum.

**Assessment:** Robust multi-sample approach. No issue.

### 1.4 Digital (Float Switch) — `readDigitalSensor()` (~L3940)

- `INPUT_PULLUP` is set on every call — harmless but redundant after first call.
- Normally-Open (NO): LOW = activated. Normally-Closed (NC): HIGH = activated.
- Returns `1.0` or `0.0`.

**Assessment:** Correct. Minor: redundant `pinMode()` every call is cheap on ARM.

### 1.5 Pulse / RPM — Cooperative State Machine (~L743–1075)

- Non-blocking `pollPulseSampler()` with `PULSE_POLL_BURST_MS = 50 ms` bursts.
- Three sub-modes: accumulated, time-based, pulse-counting.
- Edge debounce: 2 ms (`DEBOUNCE_MS`).
- Accumulated mode: `atomicReadAndResetPulses()` uses `noInterrupts()` guard — correct for Cortex-M7.

**Assessment:**
- **Sound design.** Cooperative sampler avoids blocking loop().
- **Pitfall:** `pollPulseSampler()` is only called inside `readPulseSensor()` → `readMonitorSensor()` → `sampleMonitors()`. Between sample intervals the sampler is not polled at all, meaning **edge detection between samples is lost** unless `pulseAccumulatedMode` is enabled with an ISR. For high-RPM engines this may be acceptable; for low-RPM applications consider recommending `pulseAccumulatedMode`.

### 1.6 Sensor Validation — `validateSensorReading()` (~L3818)

| Condition | Action |
|-----------|--------|
| Reading outside `[minValid, maxValid]` | Increment `consecutiveFailures` |
| `consecutiveFailures ≥ SENSOR_FAILURE_THRESHOLD (5)` | Set `sensorFailed = true`, send `sensor-fault` alarm |
| Stuck detection enabled AND `|reading − lastValid| < 0.05` | Increment `stuckReadingCount` |
| `stuckReadingCount ≥ SENSOR_STUCK_THRESHOLD (10)` | Set `sensorFailed = true`, send `sensor-stuck` alarm |
| Valid reading | Reset `consecutiveFailures`, reset `stuckReadingCount`, clear `sensorFailed` if was true → send `sensor-recovered` |

**Decision soundness:**
- **Good:** Consecutive failure counter prevents single glitch from false-positive.
- **Gap – Stuck threshold vs sample interval:** With a 30-minute sample interval (1 800 s default), 10 identical readings = 5 hours of identical data before detection. Reasonable for tank monitoring. For faster-interval deployments the constant should be reviewed.
- **Gap – Tolerance for "stuck":** `0.05` is hard-coded. For sensors with very high resolution or very low signal (e.g., 0–1 PSI), 0.05 units may be within normal noise and produce false stuck alarms. Consider making tolerance configurable or proportional to sensor range.
- **Recovery is instant:** A single valid reading resets `sensorFailed`. This could oscillate on a flaky sensor. A recovery debounce (e.g., 3 valid readings) would be more robust.

### 1.7 Solar-Only Sensor Gating — `isSensorVoltageGateOpen()` (~L5480)

| Condition | Result |
|-----------|--------|
| Not solar-only mode | Always open |
| Startup not complete | Closed |
| Vin monitor enabled AND `gVinVoltage ≥ sensorGateVoltage` | Open |
| Vin monitor not enabled | Assume open after warmup |

**Assessment:** Conservative decision — prevents garbage readings on brownout. Sound.

---

## 2. I2C Bus Management

### 2.1 Wire Timeout — Setup (~L1287)

```
Wire.setTimeout(I2C_WIRE_TIMEOUT_MS);  // 25 ms
```

**Assessment:** Good. Prevents indefinite clock-stretching hang on a locked slave.

### 2.2 I2C Address Resolution — `resolveCurrentLoopI2cAddress()` (~L1147)

- Tries preferred address, then CURRENT_LOOP_I2C_ADDRESS (0x64), then ALT_1 (0x0A), then ALT_2 (0x00).
- Skips Notecard address (0x17) and addresses outside 0x08–0x77.
- Deduplicates candidates.
- Falls back to preferred/default if none ACK.

**Assessment:** Thorough. **Note:** Address 0x00 (general-call) is included as a candidate. `i2cAck()` rejects addresses < 0x08, so it will be skipped — no actual issue, but the constant definition is misleading.

### 2.3 Startup I2C Scan (~L1302)

- `tankalarm_scanI2CBus()` probes expected devices.
- Results stored in `gStartupNotecardFound`, `gStartupCurrentLoopFound`, `gStartupScanRetries`, `gStartupUnexpectedDevices`.
- First health telemetry publishes these.

**Assessment:** Good field-diagnostic feature.

### 2.4 Dual-Failure Detection — Loop (~L1500–1535)

```
if (notecardDown && anyCurrentLoopFailed) {
    consecutiveTotalI2cFailLoops++;
    if (== I2C_DUAL_FAIL_RECOVERY_LOOPS (30))  → recoverI2CBus() + rebind Notecard
    if (≥ I2C_DUAL_FAIL_RESET_LOOPS (120))     → infinite delay → watchdog reset
}
```

**Assessment:**
- **Sound escalation:** 30 loops → attempt recovery, 120 loops → forced reset.
- **Pitfall – Loop count vs wall clock:** The dual-fail counter increments once per `loop()` iteration. In `POWER_STATE_CRITICAL_HIBERNATE`, sleep is 5 minutes per loop, so 120 loops = **10 hours** before forced reset. In `NORMAL` state (100 ms sleep), 120 loops = **12 seconds**. The behavior is vastly different by power state. Consider a **time-based** threshold instead of loop-count for deterministic behavior.
- **The counter resets to 0 if either device recovers** — correct, prevents false escalation.

### 2.5 Sensor-Only I2C Failure — Loop (~L1540–1596)

```
if (gNotecardAvailable && allCurrentLoopFailed) {
    consecutiveSensorOnlyFailLoops++;
    effectiveThreshold = I2C_SENSOR_ONLY_RECOVERY_THRESHOLD (10) × sensorRecoveryBackoff;
    if (≥ effectiveThreshold) {
        if (sensorRecoveryTotalAttempts ≥ I2C_SENSOR_RECOVERY_MAX_ATTEMPTS (5))
            → circuit breaker: permanent fault
        else
            → recoverI2CBus() + reset sensor failure counters
            → backoff *= 2 (capped at I2C_SENSOR_RECOVERY_MAX_BACKOFF (8))
    }
} else {
    → reset counters, backoff, and circuit breaker
}
```

**Assessment:**
- **Excellent design:** Exponential backoff (10, 20, 40, 80 loops), circuit breaker after 5 total attempts, full reset on recovery.
- **Pitfall – Same loop-count-vs-time issue** as §2.4. In CRITICAL state each "loop" takes 5 min; in NORMAL 100 ms.
- **Edge case:** `sensorRecoveryTotalAttempts++` happens beyond the == check that prevents logging. The increment after `== MAX_ATTEMPTS` prevents the log from firing again — good, but `sensorRecoveryTotalAttempts` will overflow (uint8_t) after 255 cumulative recovery attempts across multiple full-failure/recovery cycles if `sensorRecoveryTotalAttempts` is also reset. It IS reset when sensors recover (`sensorRecoveryTotalAttempts = 0`), so overflow is unreachable in practice.

### 2.6 Notecard-Specific I2C Recovery — `checkNotecardHealth()` (~L3068)

```
if (gNotecardFailureCount == I2C_NOTECARD_RECOVERY_THRESHOLD (10))
    → recoverI2CBus() + rebind Notecard
```

Fires exactly once at threshold, not on every subsequent failure.

**Assessment:** Correct "fire once" semantics. Further failures are handled by the exponential health-check backoff in the main loop.

---

## 3. Alarm Generation

### 3.1 Analog / Current-Loop Alarms — `evaluateAlarms()` (~L4225–4345)

| Condition | Action |
|-----------|--------|
| `sensorFailed == true` | Skip evaluation |
| `currentInches ≥ highAlarmThreshold` | Increment `highAlarmDebounceCount` |
| `highAlarmDebounceCount ≥ ALARM_DEBOUNCE_COUNT (3)` | Latch high, clear low, send "high" alarm |
| `highAlarmLatched && currentInches < (highThreshold − hysteresis) && currentInches > (lowThreshold + hysteresis)` | Increment `clearDebounceCount` → clear high |
| Symmetric for low alarm | ... |
| `highCondition \| lowCondition` | Reset `clearDebounceCount` |

**Assessment:**
- **Sound hysteresis model:** Separate entry/exit bands prevent oscillation.
- **Debounce (3 samples):** With 30-min default interval = 90 minutes to confirm alarm. Appropriate for slow-changing tanks; if interval is shortened to 1 min, alarm fires in 3 min — also reasonable.
- **Mutual exclusion:** When high alarm latches, `lowAlarmLatched = false`, and vice versa. Prevents simultaneous high+low.
- **Gap — Dead zone:** A reading in the hysteresis band (between `lowClear` and `highTrigger`) resets debounce counters but doesn't clear a latched alarm unless `clearCondition` is met. This is correct but means an alarm can persist indefinitely if the signal hovers just above `highClear`.

### 3.2 Digital Sensor Alarms — `evaluateAlarms()` (~L4178–4224)

- Two trigger modes: `digitalTrigger` string ("activated"/"not_activated") or legacy threshold comparison.
- Uses same `ALARM_DEBOUNCE_COUNT` and `highAlarmLatched` for state tracking.
- Only uses `highAlarmLatched` (no `lowAlarmLatched` for digital) — correct, since digital is binary.

**Assessment:** Clean. Legacy fallback path is well-guarded.

### 3.3 Rate Limiting — `checkAlarmRateLimit()` (~L4425–4510)

| Check | Threshold |
|-------|-----------|
| Same alarm type minimum interval | `MIN_ALARM_INTERVAL_SECONDS (300)` = 5 min |
| Per-monitor hourly limit | `MAX_ALARMS_PER_HOUR (10)` |
| Global hourly limit (all sensors) | `MAX_GLOBAL_ALARMS_PER_HOUR (30)` |
| Clear / recovery notes | **Never rate-limited** |

**Assessment:**
- **Excellent:** Layered rate limiting prevents runaway cellular data costs.
- **Clear/recovery exemption** ensures server always sees alarm resolution — critical for operational safety.
- **Implementation:** Sliding-window with explicit timestamp arrays, pruning entries > 1 hour old. Correct.

---

## 4. Notecard Communication

### 4.1 Health Check with Exponential Backoff — Loop (~L1431–1477)

```
healthCheckInterval starts at NOTECARD_HEALTH_CHECK_BASE_INTERVAL_MS (300 000 = 5 min)
On failure: healthCheckInterval *= 2, capped at NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS (4 800 000 = 80 min)
On recovery: reset to base
In LOW_POWER+: floor at 20 min.  In ECO: floor at 10 min.
```

**Assessment:**
- **Sound.** Classic exponential backoff.
- **Remote-tunable base interval:** `gConfig.healthCheckBaseIntervalMs` allows server override — good for fleet management.
- **Subtlety:** The `healthCheckInterval` is a function-local `static`, not a global. If `healthBaseInterval` changes (via config update) and the Notecard is available, the reset path `healthCheckInterval = healthBaseInterval` correctly adopts the new value. 

### 4.2 Modem Stall Detection — `checkNotecardHealth()` (~L3119–3147)

```
if (gLastSuccessfulNoteSend > 0 && (millis() − gLastSuccessfulNoteSend) > NOTECARD_MODEM_STALL_MS (4h))
    → card.restart → reset gLastSuccessfulNoteSend → mark unavailable
```

**Assessment:**
- **Good:** Detects the case where the Notecard's I2C responds but the modem is stuck internally.
- **Potential issue:** `gLastSuccessfulNoteSend` is only set in `publishNote()` on success. If the *first* note after boot fails and no note ever succeeds, `gLastSuccessfulNoteSend` stays 0, and the `> 0` guard prevents stall detection. This means a device that **never** sends a note won't trigger modem stall. Mitigation: `sendRegistration("boot")` or boot telemetry normally succeeds. But if the Notecard is born stuck from power-on, the device enters health-check backoff instead. This is a **minor gap** — the device will eventually cycle through the dual-failure watchdog reset path.

### 4.3 Note Publishing — `publishNote()` (~L6187–6260)

| Condition | Action |
|-----------|--------|
| Notecard unavailable | → `bufferNoteForRetry()` |
| `newRequest("note.add")` fails | → buffer |
| `JParse()` fails | → buffer |
| `requestAndResponse()` returns error string | → buffer, increment failure count |
| `requestAndResponse()` returns null | → buffer, increment failure count |
| Success | Update `gLastSuccessfulNotecardComm`, `gLastSuccessfulNoteSend`, reset failure count, `flushBufferedNotes()` |

- Kicks watchdog before the blocking I2C transaction.
- Stamps `_sv` (schema version) on every note body.

**Assessment:**
- **Sound.** Every failure path buffers to flash.
- **`flushBufferedNotes()` on success** is a nice opportunistic drain. Capped at 20 notes per call to avoid loop blocking.
- **Static 1 KB buffer:** `static char buffer[1024]` — sufficient for current payloads; daily reports are multi-part with per-sensor budgets.

### 4.4 Note Buffering — `bufferNoteForRetry()` / `flushBufferedNotes()` (~L6262–6450)

- Append-only `.log` file, tab-delimited (fileName\tsyncFlag\tpayload).
- Flush reads line-by-line; failures rewritten to a `.tmp` file; atomic rename at end.
- Truncated lines skipped. Flush capped at 20 per call.
- `pruneNoteBufferIfNeeded()`: over `NOTE_BUFFER_MAX_BYTES (16 KB)`, keeps last `16 KB − 2 KB` headroom.

**Assessment:**
- **Sound.** Atomic rename prevents corruption. 20-per-call cap prevents watchdog starvation.
- **Watchdog kick** inside flush loop — good.
- **Gap — Flash wear:** Frequent buffering during extended outages writes 16 KB in append mode, then prunes. LittleFS handles wear leveling, but very long outages (days) could degrade flash. Acceptable for the target hardware.

### 4.5 Telemetry Outbox Trimming — `trimTelemetryOutbox()` (~L6013–6120)

- Queries `note.changes` to collect note IDs.
- Deletes oldest notes until `TELEMETRY_OUTBOX_MAX_PENDING (15)` remain.
- Multi-pass retry (up to `TELEMETRY_TRIM_MAX_PASSES = 10`) for large backlogs.
- Zero-deletion guard: if `note.delete` fails for all attempts in a pass, abort.
- Called once per `sampleMonitors()` invocation (before individual sensor sends).

**Assessment:**
- **Very thorough.** Prevents unbounded queue growth during extended outages.
- **Watchdog kick** at top of each pass and before each delete — good.
- **Potential I2C cost:** Each trim pass involves 1 `note.changes` + N `note.delete` I2C transactions. With 15 max pending, worst case is ~16 I2C calls per trim. This is acceptable.

---

## 5. Sleep / Wake Cycle

### 5.1 Power-State Sleep Durations

| State | Sleep Duration |
|-------|---------------|
| NORMAL | 100 ms |
| ECO | 5 s |
| LOW_POWER | 30 s |
| CRITICAL_HIBERNATE | 5 min |

### 5.2 `safeSleep()` (~L7505)

- Chunks sleep into segments of `WATCHDOG_TIMEOUT_SECONDS / 2` (15 s).
- Kicks watchdog between chunks.
- Uses `rtos::ThisThread::sleep_for()` on Mbed (true RTOS sleep → low power).

**Assessment:** Correct and safe. The chunking prevents watchdog resets during long sleeps.

### 5.3 Power-State-Aware Sample Interval — Loop (~L1598)

```
sampleInterval = sampleSeconds × 1000
× POWER_ECO_SAMPLE_MULTIPLIER (2) in ECO
× POWER_LOW_SAMPLE_MULTIPLIER (4) in LOW_POWER
Skip entirely in CRITICAL_HIBERNATE
```

**Assessment:** Sound progressive degradation.

### 5.4 Power-State-Aware Polling — Loop (~L1610–1640)

- Inbound polling frequency multiplied by ECO (×4) or LOW_POWER (×12).
- All inbound polling skipped in CRITICAL_HIBERNATE.
- Solar charger, battery, and Vin polling still active in CRITICAL (needed for recovery detection).

**Assessment:**
- **Good.** Battery/solar polling enables recovery detection even in deepest sleep state.
- **Gap:** In `CRITICAL_HIBERNATE` the device sends NO telemetry — the server has no visibility. An extremely infrequent heartbeat (e.g., 1/day) might be worth the power cost for operational awareness.

---

## 6. Config Application & Validation

### 6.1 Config Schema Versioning — `loadConfigFromFlash()` (~L2613)

```
if (cfg.configSchemaVersion != CONFIG_SCHEMA_VERSION)
    → log mismatch
    → continue loading (new fields get zero-defaults from memset)
    → overwrite stored version with current
```

**Assessment:**
- **Forward compatible:** Missing fields from older schemas get zeroed defaults — safe for all current types.
- **Backward compatible:** If firmware downgrades, fields from newer schemas are silently ignored (they aren't read).
- **Minor gap:** No explicit handling if a field's semantic meaning changes between versions (vs. just adding/removing fields). Currently low risk since schema has only been bumped once.

### 6.2 Config Validation Guards

| Field | Guard |
|-------|-------|
| `minLevelChangeInches` | Clamped to ≥ 0 (~L2635) |
| `sensorMountHeight` | `fmaxf(0.0f, ...)` (~L2346) |
| `unloadDropPercent` | `constrain(pct, 10.0f, 95.0f)` (~L2368) |
| `relayMomentarySeconds[]` | Capped at 86400 (~L2314) |
| `vinMonitor.analogPin` | > 7 → default (~L2785) |
| `vinMonitor.r1Kohm/r2Kohm` | ≤ 0 → default (~L2787–2788) |
| I2C address | 0x08–0x77, not Notecard address (~L1284, 3812) |
| `pulsesPerUnit` | `max(1, ...)` (~L2252) |

**Assessment:** Generally thorough. No unchecked numeric overflows detected.

### 6.3 `applyConfigUpdate()` (~L3494–3711)

- "Update if present" semantics — absent fields retain current values.
- `hardwareChanged` triggers `reinitializeHardware()` → Wire.end/begin, Notecard rebind, hub.set reconfigure.
- `telemetryPolicyChanged` triggers `resetTelemetryBaselines()` → forces fresh telemetry for all monitors.
- Monitor removal sends `sensor-recovered` / `clear` notes for active alarms.
- Monitor addition initializes via `parseMonitorFromJson()` with existing defaults.
- Accumulated pulse state reset on mode change.
- Power state reset to NORMAL on hardware change.

**Assessment:**
- **Very thorough.** Clean teardown of removed monitors. Good alarm-state hygiene.
- **Gap — `reinitializeHardware()` resets all monitor debounce/alarm state:** If the server pushes a config change that only affects monitor B, monitor A's in-progress alarm debounce is also reset. This is generally acceptable since hardware changed, but could cause a transient alarm gap.

---

## 7. Battery / Solar Monitoring

### 7.1 Effective Battery Voltage — `getEffectiveBatteryVoltage()` (~L5571)

Sources (in priority):
1. SunSaver MPPT via Modbus (if enabled and comm OK)
2. Notecard `card.voltage` (if enabled and valid)
3. Analog Vin divider (if enabled and > 0.5 V)

**Decision:** Uses the **minimum** of available sources (conservative — protect battery).

**Assessment:** Sound. Conservative minimum prevents over-optimistic state decisions.

### 7.2 Power State Machine — `updatePowerState()` (~L5611–5800)

| Current State | Degradation | Recovery |
|---------------|-------------|----------|
| NORMAL | < ECO_ENTER (12.0 V) → ECO | — |
| ECO | < LOW_ENTER (11.8 V) → LOW. ≥ ECO_EXIT (12.4 V) → NORMAL | 0.4 V hysteresis |
| LOW_POWER | < CRITICAL_ENTER (11.5 V) → CRITICAL. ≥ LOW_EXIT (12.3 V) → ECO | Step-up one level only |
| CRITICAL | ≥ CRITICAL_EXIT (12.2 V) → LOW_POWER | Step-up one level only |

- **Debounce:** `POWER_STATE_DEBOUNCE_COUNT (3)` consecutive readings at proposed state.
- Exit is always to the **next better** state (not straight to NORMAL).
- Remote-tunable thresholds from `gConfig.power*V` fields.
- On entering CRITICAL: all relays de-energized.
- Transition logging rate-limited to 5-minute intervals.

**Assessment:**
- **Excellent design.** Step-up recovery prevents oscillation. Hysteresis bands (0.4–0.7 V) are appropriate for lead-acid.
- **Relay safety on CRITICAL entry** is a good safety feature.
- **Gap — Voltage reading frequency:** In CRITICAL_HIBERNATE, the loop runs every 5 minutes, so debounce takes 15 minutes (3 × 5 min). Appropriate.
- **Gap — No validation of remote thresholds:** If the server pushes `powerEcoEnterV = 15.0` and `powerEcoExitV = 14.0`, the device would enter ECO immediately and stay there. The thresholds are trusted without a sanity check (e.g., ecoEnter < ecoExit < normal). Consider adding cross-validation.

### 7.3 Battery Failure Fallback (~L5750–5800)

```
if (battery failure fallback enabled && CRITICAL state):
    gSolarOnlyBatFailCount++
    if (count ≥ batteryFailureThreshold && !batteryFailed):
        → enable solar-only behaviors
        → send ALARM with SMS escalation
```

- 24-hour decay (`SOLAR_BAT_FAIL_DECAY_MS`) prevents slow accumulation.
- Full recovery to ECO or better → deactivate fallback.

**Assessment:**
- **Sound.** The 24-hour decay prevents a device that dips to CRITICAL once/day from slowly accumulating toward the threshold over weeks.
- **Minor:** The `gSolarOnlyBatFailLastIncrMillis` is set when count increments (in CRITICAL), but the decay check uses `millis() - gSolarOnlyBatFailLastIncrMillis >= 24h`. If the device is in CRITICAL for 12 hours, the counter grows; once it exits CRITICAL, after 24h of non-CRITICAL the counter resets. Correct behavior.

### 7.4 Solar-Only Sunset Protocol — `checkSolarOnlySunsetProtocol()` (~L5500–5560)

```
if (Vin < sunsetVoltage AND Vin ≤ previousVin):
    Start sunset timer
    After confirmSec: save state, flush pending data, send sunset notification
if (Vin ≥ sunsetVoltage):
    Cancel protocol
```

**Assessment:**
- **Sound.** Two conditions required (below threshold AND declining) prevents false trigger from momentary cloud cover.
- **State persisted to flash** ensures boot count and last report epoch survive the power loss.

### 7.5 Startup Debounce — `performStartupDebounce()` (~L5400–5475)

- With Vin: wait for `startupDebounceVoltage` stable for `startupDebounceSec`.
- Safety timeout: `STARTUP_DEBOUNCE_MAX_WAIT_MS (5 min)` — proceeds anyway.
- Without Vin: fixed timer `startupWarmupSec`.

**Assessment:** Good safety net with the 5-minute max wait.

---

## 8. Daily Report Generation

### 8.1 Report Scheduling — `scheduleNextDailyReport()` (~L3179)

```
scheduled = dayStart + (reportHour × 3600) + (reportMinute × 60)
if (scheduled ≤ currentEpoch) → add 86400
```

### 8.2 Report Triggering — Loop (~L1750–1790)

**Standard mode:**
```
if (nextDailyReportEpoch > 0 && currentEpoch() ≥ nextDailyReportEpoch) → send
```

**Solar-only mode:**
```
if (hoursSinceLastReport ≥ opportunisticReportHours) → send
OR if (scheduledTime has passed) → send
```

**Millis fallback:**
```
if (currentEpoch() ≤ 0 && millis() > 24h):
    Fire every 24h based on millis() — prevents permanent blackout if time never syncs
```

**Assessment:**
- **Sound.** Three independent trigger paths ensure daily reports are sent under all conditions.
- **Millis fallback** is a nice safety net for devices with no cellular/GPS time sync.
- **Gap — Millis fallback has its own static:** `lastFallbackReportMillis` is function-local static, initialized to 0. The first fallback fires after millis() > 24h (24 hours of uptime). But `millis()` wraps at ~49.7 days. After wrap, `millis() > 24h` is false until time accumulates again. This creates a ~24-hour gap every ~50 days. Extremely minor.

### 8.3 Report Multi-Part Splitting — `sendDailyReport()` (~L5802–5900)

- Each part targets `DAILY_NOTE_PAYLOAD_LIMIT = 960 bytes`.
- If a single monitor exceeds the limit, allows +48 bytes headroom.
- If still too large, skips that monitor with a warning.
- Part 0 includes: Vin voltage, solar data, battery data, power state, signal strength, active alarm summary.
- Active alarm summary acts as a **backup notification path** — if the original alarm note was lost.

**Assessment:**
- **Excellent.** The backup alarm summary in the daily report is a strong reliability feature.
- **I2C error alerting** at end of `sendDailyReport()`: if `gCurrentLoopI2cErrors ≥ I2C_ERROR_ALERT_THRESHOLD (50)`, publishes `i2c-error-rate` alarm. Then resets counters. Good.

---

## 9. Relay / Output Control

### 9.1 Relay Activation via Alarm — `sendAlarm()` (~L4530–4620)

| Condition | Action |
|-----------|--------|
| `isAlarm && relayTrigger matches` | Activate relays via `triggerRemoteRelays()`, record per-relay activation time |
| `!isAlarm && mode == UNTIL_CLEAR && active` | Deactivate relays |
| Already active | Skip (prevents re-send) |

**Assessment:**
- **Good "activate once" guard:** `!gRelayActiveForMonitor[idx]` before activation.
- **BugFix 02282026 applied correctly:** Uses stored `gRelayActiveMaskForMonitor[idx]` bitmask to clear per-relay times, not the monitor index.

### 9.2 Momentary Timeout — `checkRelayMomentaryTimeout()` (~L6853)

- Checks each relay independently within each monitor's mask.
- Per-relay duration from `cfg.relayMomentarySeconds[r]`, default 1800 s (30 min).
- Unsigned subtraction handles millis() overflow correctly.
- After timeout: sends deactivation via `triggerRemoteRelays()`.

**Assessment:** Sound. Independent per-relay timing is well-implemented.

### 9.3 Relay Command Cooldown — `processRelayCommand()` (~L6715)

```
static lastRelayCommandMillis;
if (now - last < RELAY_COMMAND_COOLDOWN_MS (5000)) → reject
```

**Assessment:**
- **Good.** Prevents rapid toggling from stale queued Notes or route replays.
- **Minor gap:** The static is per-function (shared across all relays). If relay 1 and relay 2 commands arrive 3 seconds apart (legitimately distinct), the second is rejected. This is acceptable as a safety measure — commands requiring specific timing should use explicit sequencing.

### 9.4 Remote Relay Forwarding — `triggerRemoteRelays()` (~L6823)

- Sends via `relay_forward.qo` → server re-issues to target client.
- Per-relay-bit iteration over mask.

**Assessment:** Clean. Relies on server's relay forwarding pipeline — no direct client-to-client relay possible.

### 9.5 CRITICAL_HIBERNATE Relay Safety — Loop (~L1640)

```
if (gPowerState != POWER_STATE_CRITICAL_HIBERNATE):
    checkRelayMomentaryTimeout(now)
```

And in `updatePowerState()`:
```
if (gPowerState == POWER_STATE_CRITICAL_HIBERNATE):
    de-energize all relays
```

**Assessment:** Dual protection — relays are both de-energized on entry and not re-activated while in CRITICAL.

---

## 10. DFU (Firmware Update)

### 10.1 DFU Check — `checkForFirmwareUpdate()` (~L3236)

- Queries `dfu.status`.
- If `on == true` and version string present → set `gDfuUpdateAvailable`.

### 10.2 Auto-DFU — Loop (~L1713)

```
if (powerState ≤ ECO && !gDfuInProgress && gNotecardAvailable):
    checkForFirmwareUpdate()
    if (gDfuUpdateAvailable):
        enableDfuMode()
```

### 10.3 DFU Execution — `enableDfuMode()` (~L3281)

- Sets `gDfuInProgress = true` to prevent multiple triggers.
- Persists dirty config before reboot.
- Sends `dfu.status` with `on: true` → Notecard downloads and resets device.

**Assessment:**
- **Guard against low power:** DFU only allowed in NORMAL or ECO — avoids brownout during update.
- **Guard against double-trigger:** `gDfuInProgress` flag.
- **Gap — No rollback detection:** If the new firmware is broken, there's no automatic rollback mechanism. The device would boot into the new firmware and potentially crash-loop. This is a known limitation of the Blues DFU model — the Notecard doesn't provide automatic rollback. Consider adding a boot-success flag that gets set after N successful loop iterations; if not set after bootloader → revert.

---

## 11. Watchdog Management

### 11.1 Initialization — Setup (~L1305)

```
WATCHDOG_TIMEOUT_SECONDS × 1000 → mbedWatchdog.start()
```

### 11.2 Loop Kick — Loop top (~L1427)

Every loop iteration kicks the watchdog.

### 11.3 Critical Watchdog Points

| Location | Context |
|----------|---------|
| Pre-boot telemetry (~L1396) | Kick before potentially 30 s Notecard I2C call |
| `safeSleep()` (~L7505) | Chunked with kick every `timeout/2` |
| `flushBufferedNotes()` loop (~L6325) | Kick per iteration |
| `trimTelemetryOutbox()` passes (~L6048) | Kick per pass and per delete |
| `pollForSerialRequests()` (~L7058) | Kick per iteration |

**Assessment:**
- **Thorough coverage.** All long-running loops have explicit watchdog kicks.
- **Forced-reset via intentional non-kick:** In the dual-failure infinite loop (~L1530), `while (true) { delay(100); }` intentionally refuses to kick → watchdog fires within 30 s. Good.
- **safeSleep chunk:** `timeout/2 = 15 s` chunks. Watchdog timeout is 30 s. Margin is exactly half — sufficient.

---

## 12. New / Advanced Logic

### 12.1 I2C_DUAL_FAIL_RECOVERY_LOOPS (30) / I2C_DUAL_FAIL_RESET_LOOPS (120)

See §2.4. Defined in `TankAlarm_Config.h`. Two-stage escalation: bus recovery at 30 loops, forced watchdog reset at 120 loops.

### 12.2 I2C_SENSOR_RECOVERY_MAX_BACKOFF (8)

See §2.5. Caps the exponential backoff multiplier at 8× base threshold (8 × 10 = 80 loops between recovery attempts).

### 12.3 Exponential Backoff (Health Check)

See §4.1. Doubles from 5 min to max 80 min. Floor enforced in ECO (10 min) and LOW_POWER+ (20 min).

### 12.4 RELAY_COMMAND_COOLDOWN_MS (5000)

See §9.3. 5-second global cooldown between relay commands.

### 12.5 CONFIG_SCHEMA_VERSION / NOTEFILE_SCHEMA_VERSION

- `CONFIG_SCHEMA_VERSION = 1`: Stored in flash config. On mismatch, logs warning, loads with defaults for missing fields, re-saves with current version.
- `NOTEFILE_SCHEMA_VERSION = 1`: Stamped as `_sv` field in every outbound note body. Allows server to detect client firmware age and handle protocol differences.

**Assessment:** Forward/backward compatible. Server can feature-flag by `_sv` value.

### 12.6 I2C_SENSOR_RECOVERY_MAX_ATTEMPTS (5) — Circuit Breaker

Stops retrying after 5 total sensor-only recovery attempts. Logs "permanent fault" once. Resets on full sensor recovery.

**Assessment:** Prevents infinite recovery cycles on truly dead hardware. Sound.

### 12.7 I2C_ERROR_ALERT_THRESHOLD (50)

In `sendDailyReport()`, if the 24-hour I2C error count exceeds 50, sends an `i2c-error-rate` alarm. Counters reset after daily report.

**Assessment:** Good proactive alerting for degrading connections.

---

## 13. Cross-Cutting Concerns

### 13.1 Race Conditions

| Concern | Analysis |
|---------|----------|
| `gPulseSampler[]` shared between `readPulseSensor()` and potential ISR | `noInterrupts()` guards in all `atomic*Pulses()` helpers — correct. |
| `gRelayActivationTime[]` shared across monitors | Relay indices are unique (0–3), and only one code path activates a relay at a time (alarm path). No ISR involvement. **No race.** |
| `millis()` overflow (49.7 days) | Unsigned subtraction is used consistently throughout. **Correct.** |

### 13.2 Feedback Loops

| Loop | Assessment |
|------|------------|
| Alarm → relay → alarm clear → relay off → re-alarm | Debounce (3 samples) and `!gRelayActiveForMonitor` guard prevent rapid cycling. |
| Sensor failure → I2C recovery → sensor OK → sensor failure | Exponential backoff + circuit breaker prevent runaway. |
| Power state oscillation near threshold | 3-reading debounce + hysteresis (0.4–0.7 V) prevents oscillation. |
| Battery failure counter accumulation | 24-hour decay prevents slow false accumulation. |

### 13.3 Memory Safety

| Concern | Analysis |
|---------|----------|
| `strlcpy` used everywhere for string copies | **Good.** No unbounded copies. |
| JSON parsing via heap-allocated `JsonDocument` | `std::unique_ptr` used for large documents — prevents leaks on early return. |
| Note buffer line parsing | Fixed-size `char lineBuffer[1024]` — truncated lines detected and skipped. **Good.** |
| `snprintf` for log messages | Buffer sizes match allocations. **Good.** |
| `gMonitorState[MAX_MONITORS]` array bounds | Index checks (`idx >= gConfig.monitorCount`) at function entry. **Good.** |

### 13.4 Flash Wear and Atomic Writes

- Config writes: `tankalarm_posix_write_file_atomic()` → write .tmp → rename → safe.
- Solar state writes: Same atomic pattern.
- Note buffer: POSIX rename for recovery, no explicit `remove()` before rename.
- Boot recovery: `recoverOrphanedTmpFiles()` handles interrupted rename at startup.

**Assessment:** Comprehensive. No torn-write risk.

### 13.5 Incorrect Assumptions

| Assumption | Verdict |
|------------|---------|
| `millis()` is monotonic | **Correct** on Mbed OS / Cortex-M7 (`us_ticker` is hardware counter). |
| `currentEpoch()` based on millis delta from last sync | **Drifts** over time (millis may be faster/slower than real seconds). Re-syncs every 6 hours. Acceptable for alarm/report scheduling but not sub-second precision. |
| ADC resolution is 12-bit | Set explicitly in `setup()` with `analogReadResolution(12)`. **Correct.** |
| Notecard I2C address is always 0x17 | Defined as `NOTECARD_I2C_ADDRESS` in common header. **Correct per Blues documentation.** |

---

## 14. Summary Scorecard

| Category | Rating | Key Finding |
|----------|--------|-------------|
| Sensor reading | ★★★★☆ | Current loop: no multi-sample averaging (unlike analog path). Single-reading recovery from sensor failure. |
| I2C management | ★★★★★ | Three-tier escalation, exponential backoff, circuit breaker. Loop-count-vs-time disparity is a minor pedantic concern. |
| Alarm generation | ★★★★★ | Proper hysteresis, debounce, layered rate limiting, clear/recovery never suppressed. |
| Notecard communication | ★★★★★ | Exponential backoff, modem stall detection, buffered retry, telemetry outbox trim, schema versioning. |
| Sleep/wake | ★★★★☆ | Correct chunked sleep. No heartbeat in CRITICAL_HIBERNATE is an operational visibility gap. |
| Config handling | ★★★★★ | Schema versioning, thorough validation guards, clean update-if-present semantics. |
| Battery/solar | ★★★★☆ | Excellent hysteresis state machine. No validation of remote-tuned thresholds could allow misconfiguration. |
| Daily reports | ★★★★★ | Multi-part splitting, backup alarm summary, I2C error alerting, millis fallback. |
| Relay control | ★★★★★ | Per-relay independent timing, command cooldown, CRITICAL safety, bugfix for bitmask indexing applied. |
| DFU | ★★★☆☆ | Functional but no automatic rollback on failed updates. |
| Watchdog | ★★★★★ | Every long-running path covered. Intentional non-kick for fatal states. |
| New logic | ★★★★★ | All new mechanisms (backoff, cooldown, schema, circuit breaker) are well-designed and well-integrated. |

### Top Recommendations

1. **Current loop multi-sample averaging:** Add 2–4 sample averaging to `readCurrentLoopSensor()` to match the analog sensor path and reduce I2C noise sensitivity.

2. **Sensor recovery debounce:** Require 2–3 valid readings before clearing `sensorFailed`, preventing oscillation on flaky sensors.

3. **Time-based I2C failure thresholds:** Replace loop-count thresholds (`I2C_DUAL_FAIL_RECOVERY_LOOPS`) with wall-clock time thresholds, so behavior is deterministic regardless of power state sleep duration.

4. **Remote threshold validation:** Add sanity checks when applying remote power thresholds (e.g., `ecoEnter < lowEnter < criticalEnter`, `ecoExit > ecoEnter`).

5. **CRITICAL_HIBERNATE heartbeat:** Consider a single "still alive" note per day in CRITICAL to maintain server visibility.

6. **DFU rollback safety:** Implement a boot-success flag (set after N successful loops) that the bootloader can check to revert to previous firmware on crash-loop.

7. **Configurable stuck sensor tolerance:** Make the 0.05-unit stuck-detection tolerance configurable or proportional to sensor range.

---

*End of review.*
