# Logic & Decision Pathway Review — TankAlarm v1.1.0

**Document**: `LOGICREVIEW-20260324-1-Copilot-v1.0`  
**Author**: GitHub Copilot (Claude Opus 4.6)  
**Date**: March 24, 2026  
**Scope**: All decision-making functions across Server, Client, Viewer, and Common library  
**Firmware Version Reviewed**: 1.1.0 (Server), 1.1.0 (Client), 1.1.9 (Viewer)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Methodology](#2-methodology)
3. [Architecture-Level Decision Analysis](#3-architecture-level-decision-analysis)
4. [Client Decision Pathways](#4-client-decision-pathways)
   - 4.1 [Alarm Evaluation](#41-alarm-evaluation)
   - 4.2 [Sensor Reading & Selection](#42-sensor-reading--selection)
   - 4.3 [Sensor Failure Detection](#43-sensor-failure-detection)
   - 4.4 [Power Conservation State Machine](#44-power-conservation-state-machine)
   - 4.5 [Telemetry Dispatch](#45-telemetry-dispatch)
   - 4.6 [Unload Detection](#46-unload-detection)
   - 4.7 [Relay Control](#47-relay-control)
   - 4.8 [Client Rate Limiting](#48-client-rate-limiting)
   - 4.9 [RPM/Pulse Counting](#49-rpmpulse-counting)
   - 4.10 [Config Update Application](#410-config-update-application)
   - 4.11 [Note Buffering & Offline Resilience](#411-note-buffering--offline-resilience)
   - 4.12 [Notecard Failure Handling](#412-notecard-failure-handling)
5. [Server Decision Pathways](#5-server-decision-pathways)
   - 5.1 [SMS Dispatch & Rate Limiting](#51-sms-dispatch--rate-limiting)
   - 5.2 [Stale Client Detection](#52-stale-client-detection)
   - 5.3 [24-Hour Change Tracking](#53-24-hour-change-tracking)
   - 5.4 [Daily Email Scheduling](#54-daily-email-scheduling)
   - 5.5 [Calibration Learning System](#55-calibration-learning-system)
   - 5.6 [Config Dispatch & ACK Tracking](#56-config-dispatch--ack-tracking)
   - 5.7 [Server-Down Detection](#57-server-down-detection)
   - 5.8 [Authentication & Session Management](#58-authentication--session-management)
   - 5.9 [Viewer Summary Publishing](#59-viewer-summary-publishing)
   - 5.10 [FTP Backup & Restore](#510-ftp-backup--restore)
6. [Viewer Decision Pathways](#6-viewer-decision-pathways)
   - 6.1 [Summary Fetch & Scheduling](#61-summary-fetch--scheduling)
   - 6.2 [Web Server & HTTP Handling](#62-web-server--http-handling)
   - 6.3 [Notecard Health & Recovery](#63-notecard-health--recovery)
   - 6.4 [DFU Update Flow](#64-dfu-update-flow)
   - 6.5 [Client-Side JavaScript](#65-client-side-javascript)
7. [Cross-System Decision Interactions](#7-cross-system-decision-interactions)
8. [Findings Summary](#8-findings-summary)
   - 8.1 [Critical Findings](#81-critical-findings)
   - 8.2 [Moderate Findings](#82-moderate-findings)
   - 8.3 [Minor Findings](#83-minor-findings)
   - 8.4 [Informational Observations](#84-informational-observations)
9. [Recommendations](#9-recommendations)
10. [Appendix: Threshold & Constant Registry](#10-appendix-threshold--constant-registry)

---

## 1. Executive Summary

This review examines every conditional branch, threshold comparison, state transition, and logical pathway across the TankAlarm firmware suite — comprising approximately 18,000 lines of active logic across three Arduino Opta firmware sketches and a shared common library.

**Overall Assessment**: The decision architecture is well-reasoned and conservative. The system exhibits defense-in-depth at every layer: alarms are debounced, rate-limited, and gated by multiple conditions; power conservation transitions use hysteresis with step-by-step recovery; and sensor failure detection avoids single-sample false positives. The codebase demonstrates mature engineering judgment, particularly in its handling of edge cases like `millis()` rollover, Notecard communication failures, and incomplete calibration data.

**Key Strengths**:
- Every alarm decision uses debounce (3+ consecutive samples) before acting
- Dual rate-limiting on both Client (millis-based) and Server (epoch-based) prevents cascade failures even when one system's clock is unreliable
- Power state machine uses asymmetric transitions (fast degrade, slow recover) with increasing hysteresis gaps — textbook correct for battery systems
- Calibration system gracefully degrades from multivariate regression → simple linear → raw passthrough
- Offline note buffering with atomic write patterns ensures no data loss during power events

**Key Concerns** (3 critical, 8 moderate — detailed in §8):
- Viewer Ethernet retry uses raw `delay()` near the watchdog timeout boundary
- Server daily email scheduling uses UTC with no timezone awareness, causing emails to arrive at unexpected local times
- Stuck sensor detection defaults to enabled and will fire false positives on legitimately idle tanks
- SMS fail-open on Server when time sync is unavailable could cause SMS flooding in a clock-loss scenario

---

## 2. Methodology

### Approach
Every `.ino` file and every `.h`/`.cpp` file in the Common library was read in full. Decision points were cataloged by:
1. **Control-flow extraction**: Every `if`, `switch`, ternary, and loop guard was identified
2. **Threshold mapping**: Every numeric constant used in a comparison was traced to its definition and evaluated for reasonableness
3. **State machine analysis**: Multi-state systems (power conservation, alarm latching, Notecard health) were validated for completeness, deadlock freedom, and transition correctness
4. **Cross-system interaction tracing**: Decisions in one component that depend on behavior in another were traced end-to-end

### Files Reviewed
| Component | File | Lines |
|-----------|------|-------|
| Common | `TankAlarm_Common.h` | ~80 |
| Common | `TankAlarm_Config.h` | ~90 |
| Common | `TankAlarm_Battery.h` | ~200 |
| Common | `TankAlarm_Solar.h` | ~250 |
| Common | `TankAlarm_Utils.h` | ~100 |
| Common | `TankAlarm_Notecard.h` | ~150 |
| Common | `TankAlarm_Platform.h` | ~300 |
| Server | `TankAlarm-112025-Server-BluesOpta.ino` | ~13,000 |
| Client | `TankAlarm-112025-Client-BluesOpta.ino` | ~5,300 |
| Viewer | `TankAlarm-112025-Viewer-BluesOpta.ino` | ~997 |

---

## 3. Architecture-Level Decision Analysis

### 3.1 Communication Model

**Decision**: Server publishes configuration and commands via Notecard outbound notefiles; Clients pull inbound notefiles via polling. Viewers receive summary data from the Server on a 6-hour aligned schedule.

**Assessment**: Sound. This is a store-and-forward model that tolerates intermittent cellular connectivity — essential for solar-powered field devices. The polling model means no server-to-client push capability, but the inbound polling interval (configurable, default 10 min grid / 60 min solar) provides adequate responsiveness for configuration changes.

**Gap**: There is no mechanism for the Server to force an urgent command (e.g., "activate relay now") to a Client faster than the inbound polling interval. In ECO mode, this increases to 4× the base interval (4 hours for solar). For time-critical relay activation (e.g., overfill shutoff), this latency could be operationally significant.

### 3.2 Clock Architecture

**Decision**: All three devices derive time from Notecard `card.time` (cellular network time), synced every 6 hours. Between syncs, time is derived from the MCU's `millis()` with linear interpolation: `epoch = lastSyncedEpoch + (millis() - lastSyncMillis) / 1000.0`.

**Assessment**: Correct. The `(uint32_t)(millis() - lastSyncMillis)` cast handles 49.7-day `millis()` rollover. Crystal drift on the Opta (~50 ppm) limits drift to ~1 second per 6-hour sync window. The `gLastSyncedEpoch == 0.0` sentinel correctly disables all epoch-based scheduling until first sync.

**Observation**: The Client's alarm rate limiting uses `millis()` directly (not epoch), which means it functions even before time sync. The Server's SMS rate limiting uses epoch, which means it fails open (allows SMS) when `currentEpoch()` returns 0.0. This asymmetry is documented in §5.1.

### 3.3 Storage & Persistence

**Decision**: All persistent data uses LittleFS with POSIX-style file I/O. Critical writes use an atomic write-to-temp-then-rename pattern. Boot-time recovery handles orphaned `.tmp` files.

**Assessment**: Excellent. The atomic write pattern (`tankalarm_posix_write_file_atomic`) prevents data corruption from power loss during write. The boot-time cleanup (`recoverOrphanedTmpFiles`) handles the edge case where power is lost after creating `.tmp` but before rename. The Mbed OS vs. STM32duino split in the platform layer is correctly abstracted.

---

## 4. Client Decision Pathways

### 4.1 Alarm Evaluation

**Function**: `evaluateAlarms()` (Client L2956–3257)

#### Digital Sensor Path
**Decision tree**:
1. Skip if `sensorFailed` — correct; no false alarms from faulty sensors
2. Compare `currentInches` against `DIGITAL_SWITCH_THRESHOLD` (0.5) — binary threshold for a 0.0/1.0 signal
3. Determine trigger condition from `digitalTrigger` field (`"activated"` or `"not_activated"`)
4. Legacy fallback: if `digitalTrigger` is empty, infer from `highAlarmThreshold` — backward compatibility
5. Debounce: 3 consecutive readings to set OR clear alarm

**Assessment**: **Well-founded.** The debounce prevents contact bounce (common in float switches) from generating spurious alarms. The legacy fallback ensures configs created before `digitalTrigger` was added continue working.

**Finding [I-1]**: The legacy fallback logic (`highAlarmThreshold >= 1.0` → alarm on activated) relies on the convention that thresholds were configured a certain way in prior firmware versions. If a user had set `highAlarmThreshold = 2.0` (an unusual but valid value for an analog sensor later reconfigured as digital), the legacy logic would still interpret it correctly (2.0 ≥ 1.0 → alarm on activated). No gap here.

#### Analog Sensor Path
**Decision tree**:
1. Compute hysteresis bands: `highTrigger = threshold`, `highClear = threshold - hysteresis`
2. Mutual exclusion: setting high alarm unlatches low alarm (and vice versa)
3. Clear requires readings within the hysteresis dead zone (below high clear AND above low clear)
4. Debounce on all transitions

**Assessment**: **Correct and conservative.** Hysteresis prevents oscillation near threshold boundaries. Mutual exclusion is appropriate — a tank cannot physically be simultaneously above high and below low thresholds. The debounce count of 3 at typical 30-minute sample intervals means 90 minutes to confirm an alarm, which could be tuned based on operational urgency.

**Finding [I-2]**: The clear condition requires `currentInches < highClear AND currentInches > lowClear`. If `hysteresisValue` is 0, then `highClear == highTrigger` and `lowClear == lowTrigger`, making the clear zone a strict subset that can never be entered (would need to be strictly less than the trigger AND strictly greater than the trigger at the same point). In practice, hysteresis should always be > 0 for analog sensors, and the config validation should enforce this. **Verify that `hysteresisValue` is validated to be > 0 for analog sensors during config application.**

### 4.2 Sensor Reading & Selection

**Function**: `readTankSensor()` (Client L2615–2936)

#### Current Loop (4-20mA)
**Decision**: Reads milliamps via I2C current loop module. On I2C failure, returns previous `currentInches` (graceful degradation).

**Assessment**: **Sound.** Returning the last known value prevents a single I2C glitch from triggering false alarms. The sensor failure detection (§4.3) will catch sustained failures. The I2C read is a blocking call; if the current loop module hangs, the watchdog (30s) will recover the system.

**Finding [M-1]**: Ultrasonic sensors (top-mounted) compute level as `sensorMountHeight - distanceInches`. If the ultrasonic sensor reports a distance greater than the mount height (echo from floor below sensor), the result would be negative, which is clamped to 0. However, if the ultrasonic beam bounces off foam or agitation and reports a *shorter* distance than actual, the level will read *higher* than reality. This is a physics constraint, not a code bug, but the lack of any moving-average or outlier rejection on ultrasonic readings means a single shortened-echo sample could trigger a high alarm (moderated by the 3-sample debounce). **Consider documenting that ultrasonic sensors in turbulent tanks should use a higher debounce count or shorter sample interval to catch transient reflections.**

#### Analog Voltage (0-10V)
**Decision**: 8 ADC readings averaged with 2ms delay between each. Voltage mapped linearly to pressure, then to inches.

**Assessment**: **Good.** The 8-sample average smooths ADC noise. The 2ms delay between samples is appropriate for the STM32H7's ADC settling time. The linear mapping assumes the sensor has a linear output, which is standard for industrial 0-10V transmitters.

#### Pulse/RPM
**Decision**: Three modes — accumulated, time-based, and pulse counting — selected based on config and expected pulse rate.

**Assessment**: **Well-designed.** The auto-selection in `getRecommendedPulseSampling()` correctly matches the sampling strategy to the expected rate range. The accumulated mode with atomic read-modify-write is essential for sub-RPM rates where a single sample window might catch zero pulses. The 2ms per-edge debounce is appropriate for mechanical Hall effect sensors.

**Finding [I-3]**: In time-based mode, if only one pulse is detected during the sample window, the code keeps the last reading. This means a decelerating motor will show the previous (higher) RPM until it either stops completely or gets two pulses in one window. For a 60-second sample window, this is acceptable — but for shorter windows (3s at high RPM), a single-pulse scenario implies the RPM has dropped below `60000 / (3000 × pulsesPerUnit)`, which should be ~20 RPM minimum for 1 PPR. The logic is correct but creates an observability gap near the mode's lower detection limit.

### 4.3 Sensor Failure Detection

**Function**: `validateSensorReading()` (Client L2830–2934)

#### Out-of-Range Detection
**Decision**: Compute valid range per sensor type with 10% overhead (× 1.1 for max, × 0.1 for negative allowance). Require 5 consecutive out-of-range readings before declaring failure.

**Assessment**: **Conservative and correct.** The 10% overhead prevents false positives from minor calibration offsets. The 5-sample debounce ensures transient spikes don't trigger failure alerts. For a 30-minute sample interval, this means 2.5 hours to confirm a sensor failure, which balances false-positive avoidance against detection latency.

#### Stuck Sensor Detection

**Decision**: If reading differs from previous by less than `0.05f` for 10 consecutive readings, declare the sensor stuck.

**Assessment ⚠️**: **Problematic for idle tanks.** A tank that is not being filled or emptied will have a genuinely stable level. At 0.05 inches epsilon and 10 consecutive readings (5 hours at 30-min intervals), a tank that sits at a constant level for half a day will be falsely declared stuck. The `stuckDetectionEnabled` per-monitor flag exists as a mitigation, but it defaults to `true`.

**Finding [C-1, MODERATE]**: **Stuck detection false-positives on idle tanks.** The 0.05-inch threshold is tighter than the natural drift of a stationary 4-20mA sensor (which typically shows 0.01–0.05 inches of ADC noise). If the sensor happens to quantize to the same value 10 times in a row — plausible with a 12-bit ADC at a stable level — the tank will be falsely reported as having a stuck sensor.

**Recommendation**: Either (a) increase the stuck threshold to match the sensor's noise floor (e.g., 0.1 inches for 4-20mA), (b) default `stuckDetectionEnabled` to `false` for sensor types where stability is expected (e.g., tank level), or (c) add a minimum time window (e.g., 24 hours of identical readings) rather than a sample count, so the threshold scales with the sample interval.

#### Recovery

**Decision**: Any valid reading after a failure clears `sensorFailed` and sends a `"sensor-recovered"` notification.

**Finding [M-2, MODERATE]**: `"sensor-recovered"` notifications are not rate-limited. The comment in `checkAlarmRateLimit()` explicitly exempts `"clear"` and `"sensor-recovered"` from rate limiting. If a sensor oscillates between valid and invalid readings (e.g., a loose wire making intermittent contact), each valid reading generates a recovery note. With a 30-minute sample interval this is bounded (max 2/hour), but in pulse mode with short sample windows (3s), the rate could be significant. The recovery bypass is intentional (the Server needs recoveries to clear alarm states), but a minimum interval (e.g., 60s) between recovery notifications would prevent flooding without breaking alarm resolution.

### 4.4 Power Conservation State Machine

**Function**: `updatePowerState()` (Client L4288–4398)

#### State Transitions

| State | Entry (V drops below) | Exit (V rises above) | Hysteresis Gap |
|-------|----------------------|---------------------|----------------|
| NORMAL | — | — | — |
| ECO | 12.0V | 12.4V | 0.4V |
| LOW_POWER | 11.8V | 12.3V | 0.5V |
| CRITICAL_HIBERNATE | 11.5V | 12.2V | 0.7V |

**Assessment**: **Textbook correct.** The increasing hysteresis gaps (0.4V → 0.5V → 0.7V) as battery stress increases prevent oscillation at lower voltages where the battery's internal resistance causes larger voltage swings under load. The asymmetric transition rules (multi-level degrade, single-step recover) prevent premature return to high-power states.

**Key behaviors reviewed**:
- Debounce of 3 readings prevents transient voltage dips (e.g., during I2C transmit) from triggering transitions
- CRITICAL entry de-energizes all relays (correct — relay coils draw ~100mA each, prioritize MCU survival)
- Step-by-step recovery prevents voltage sag from load restoration causing immediate re-entry to CRITICAL
- `getEffectiveBatteryVoltage()` takes the *lower* of SunSaver MPPT and Notecard readings (conservative)
- If no battery monitoring is active, `getEffectiveBatteryVoltage()` returns 0 → forces NORMAL state (correct default for grid-powered installations)

**Finding [I-4]**: The gap between ECO entry (12.0V) and LOW_POWER entry (11.8V) is only 0.2V. Under heavy load (cellular modem active), a lead-acid battery can drop 0.3–0.5V. This means a battery at 12.1V during idle could drop to 11.6V during a cellular transmit, potentially skipping ECO and going directly to LOW_POWER. The transition is still valid (multi-level degrade is allowed), and the debounce of 3 readings should catch this (the voltage would recover between transmits), but it means a battery hovering near 12.0V might exhibit rapid ECO→LOW_POWER transitions during modem activity.

**Finding [I-5]**: The battery failure decay (reset after 24 hours without CRITICAL readings) is a good safeguard against slow accumulation. However, the 24-hour timer uses `millis()`, which introduces a subtle interaction: if the device is in LOW_POWER (30s sleep between iterations), the 24-hour window as measured by wall-clock time is correct, but the device only samples battery voltage a few times per hour. This is fine — the timer measures wall time, not sample count.

#### Loop-Level Effects

| State | Sample Interval | Inbound Polling | Outbound Sync | DFU | Daily Report |
|-------|----------------|-----------------|---------------|-----|-------------|
| NORMAL | 1× | 1× | 1× | ✓ | ✓ |
| ECO | 2× | 4× | 2× | ✓ | ✓ |
| LOW_POWER | 4× | 12× | 4× | ✗ | ✓ |
| CRITICAL | Skip | Skip | N/A | ✗ | ✗ |

**Assessment**: The multiplier stacking is well-chosen. With a 30-minute base sample interval, LOW_POWER samples every 2 hours and syncs outbound every 24 hours (for solar). The Server's stale threshold of 49 hours allows approximately 2 missed LOW_POWER outbound syncs before alerting — adequate margin.

**Finding [M-3, MODERATE]**: In CRITICAL_HIBERNATE, all sampling is skipped, which means the alarm evaluation also never fires. A CRITICAL-state tank that is actively overflowing will not generate an alarm until battery recovery. This is arguably correct (preserving the MCU at all costs), but it represents a silent safety gap. At minimum, the Server should be made aware that a client in CRITICAL state has lost alarm capability — which it is, via the `sendPowerStateChange()` notification.

### 4.5 Telemetry Dispatch

**Function**: `sendTelemetry()` called from `sampleTanks()` (Client L2937–3391)

**Decision**: Send telemetry if (a) it's the first reading ever (`lastReportedInches < 0.0`), or (b) the level has changed by ≥ `minLevelChangeInches` since the last reported value.

**Assessment**: **Efficient.** The change-threshold model avoids unnecessary cellular data usage. The first-reading baseline ensures the server always receives at least one data point after boot.

**Finding [I-6]**: The threshold comparison uses absolute change (`fabs(currentInches - lastReported) >= threshold`). For tanks measured in inches where levels can range from 0–200+, a 1-inch threshold is ~0.5% at full and infinite% at empty. For RPM sensors where the value might be 3000, a 1-inch (really 1-RPM) threshold would be very granular. The threshold is configurable per-monitor, but operators must understand they're setting an absolute value, not a percentage. This is documented behavior, not a bug.

### 4.6 Unload Detection

**Function**: `evaluateUnload()` (Client L3669–3743)

**Decision tree**:
1. Start tracking when level ≥ 12 inches (tank has meaningful contents)
2. Track peak level as tank fills
3. Compute unload trigger: `peak × (unloadDropPercent/100)` or `peak - unloadDropThreshold`, whichever is lower
4. If level drops below trigger for 3 consecutive readings → declare unload
5. Reset tracking after confirmed unload

**Assessment**: **Reasonable.** The dual-threshold approach (percentage-based or absolute-based, whichever triggers first) handles both large and small tanks. The 12-inch minimum peak prevents small fluctuations on mostly-empty tanks from being detected as unloads.

**Finding [M-4, MODERATE]**: The unload detection only tracks a single peak. If a tank is slowly filled over days (each delivery raises the level), the peak continuously ratchets up but never triggers an unload because the level never drops below the threshold relative to the ever-increasing peak. This means: (a) gradual consumption between deliveries is not detected as unload events (correct — unloads are typically rapid pump-outs); (b) if the tank is filled to 100", then partially emptied to 60", then refilled to 90", the peak updates to 100" and the 50% trigger is 50". The partial empty from 100→60 was captured as an unload. The refill to 90 is below the 100 peak, so no new peak is set. If the tank is then emptied to 30, the trigger is still 50 (from the 100 peak), so it fires correctly. **No gap found** — the algorithm correctly handles partial fills and re-empties.

**Finding [I-7]**: When level drops to or below `sensorMountHeight`, the code uses `unloadEmptyHeight` (default 2.0 inches) as the "empty" reading. This prevents the unload event from reporting misleading negative levels due to sensor drift below mounting height.

### 4.7 Relay Control

**Functions**: `activateLocalAlarm()`, `sendAlarm()`, `checkRelayMomentaryTimeout()`, `clearAllRelayAlarms()` (Client L3467–5292)

#### Activation Logic
**Decision**: Relay activation is gated by `relayTrigger` config — `ANY`, `HIGH`, or `LOW`. Only the matching alarm type activates the relay.

**Assessment**: **Correct.** This prevents low-alarm conditions from activating a relay meant for overfill protection (high alarm), and vice versa.

#### Deactivation Logic
Three modes:
1. **Momentary**: Relay active for configured duration (default 30 min), then auto-deactivates
2. **Until Clear**: Relay stays active until the triggering alarm clears (via hysteresis band)
3. **Manual Reset**: Relay stays active until operator presses clear button or Server sends reset command

**Assessment**: **Complete coverage of operational needs.** The mutual exclusion (`gRelayActiveForTank[idx]` tracking) prevents duplicate activations. The momentary timeout uses `millis()` with unsigned arithmetic, correctly handling rollover.

**Finding [I-8]**: The momentary timeout uses the *minimum* duration among all active relays in the mask. If two relays are configured with different momentary durations (e.g., relay A = 30 min, relay B = 60 min), both will deactivate after 30 minutes. This is documented behavior but could surprise operators who configure per-relay durations.

#### Clear Button
**Decision**: Physical button with 50ms debounce and 500ms minimum press. Active-high or active-low configurable. Clears ALL relay alarms and de-energizes ALL relays.

**Assessment**: **Good.** The 500ms minimum press prevents accidental bumps. The 2-second block after trigger prevents rapid re-trigger. The clear-all behavior is appropriate for a field panic button.

### 4.8 Client Rate Limiting

**Function**: `checkAlarmRateLimit()` (Client L4367–4440)

**Decision**: Uses `millis()` for all timing (not epoch). Per-type minimum interval of 5 minutes. Per-monitor hourly cap of 10 alarms. Global hourly cap across all monitors.

**Assessment**: **Excellent design choice.** Using `millis()` means rate limiting works from first boot, before Notecard time sync. The per-type discrimination prevents a high-alarm rate limit from blocking a subsequent clear notification. The explicit exemption of `"clear"` and `"sensor-recovered"` from rate limiting ensures the Server always receives alarm resolution events.

**Finding [I-9]**: The `millis()` timestamps in the sliding window are compared with unsigned subtraction (`state.alarmTimestamps[i] > oneHourAgo`). Because `millis()` rolls over at ~49.7 days and `oneHourAgo = now - 3600000UL`, this comparison fails at rollover if `now < 3600000UL` (within the first hour after rollover). In practice, this means at the 49.7-day mark, all timestamps will appear to be within the last hour, and the rate limiter will briefly suppress alarms. This is a ~1-hour window every 49.7 days. **Low severity** — the debounce and alarm latching ensure the alarm fires on the next cycle.

### 4.9 RPM/Pulse Counting

**Functions**: `readTankSensor()` pulse path, `getRecommendedPulseSampling()` (Client L2615+)

**Decision**: Auto-select sampling strategy based on expected pulse rate:

| Expected Rate | Sample Duration | Mode |
|---------------|----------------|------|
| ≤0 RPM | 60s | Counting |
| <1 RPM | 60s | **Accumulated** |
| 1–10 RPM | 60s | Counting |
| 10–100 RPM | 30s | Counting |
| 100–1000 RPM | 10s | Counting |
| >1000 RPM | 3s | Counting |

**Assessment**: **Well-calibrated.** The threshold breakpoints match the statistical requirements for reliable pulse counting. At each tier, the sample window captures at least ~3 pulses at the low end of the range (e.g., 10 RPM × 30s/60 = 5 pulses), providing reasonable precision.

**Finding [I-10]**: The accumulated mode uses atomic read-modify-write with interrupt disable (`noInterrupts()`/`interrupts()`) and a 1-second burst supplement. If an ISR fires during the 1-second burst and adds to the accumulated counter, the burst count and accumulated count can overlap (both capture the same pulses). The burst sample uses `digitalRead` polling, not ISR, so this overlap shouldn't occur in practice — the ISR increments `gRpmAccumulatedPulses[]`, while the burst sample counts edges via polling. These are two independent counts. **The intent appears to be that the accumulated counter captures all pulses between telemetry cycles, and the burst provides a real-time snapshot. They are NOT combined.** After reading the counter, it is reset. No double-counting gap found.

### 4.10 Config Update Application

**Function**: `applyConfigUpdate()` (Client L2304–2556)

**Decision**: Partial update model — only non-null fields in the incoming JSON overwrite local config.

**Assessment**: **Robust.** This allows the Server to send targeted updates (e.g., change only sample interval) without needing to retransmit the entire configuration. The `hardwareChanged` / `telemetryPolicyChanged` flags correctly distinguish between changes that require hardware reinitialization vs. changes that only affect reporting policy.

**Validation decisions reviewed**:
- `minLevelChangeInches` clamped ≥ 0 — prevents negative change thresholds (which would always trigger)
- `sensorMountHeight` clamped ≥ 0 — negative mount height is physically meaningless
- `unloadDropPercent` clamped 10–95% — prevents 0% (everything is an unload) or 100% (nothing is an unload)
- `relayMomentaryDurations` clamped 1–86400s — prevents 0 (immediate deactivation) or exceeding 1 day
- `pulsesPerUnit` minimum 1 — prevents division by zero in RPM calculation

**Assessment**: All validations are appropriate and catch operator misconfiguration.

**Finding [I-11]**: When `pulseAccumulatedMode` changes, accumulated pulse counters are reset atomically. This is correct — switching modes mid-operation would produce garbage data if old accumulated counts were mixed with new counting-mode data.

### 4.11 Note Buffering & Offline Resilience

**Functions**: `publishNote()`, `bufferNoteForRetry()`, `flushBufferedNotes()`, `pruneNoteBufferIfNeeded()` (Client L4550–4900)

**Decision**: If Notecard is unavailable, serialize notes to `pending_notes.log` (tab-delimited, one per line). Flush on recovery. Prune to 16KB max, keeping newest.

**Assessment**: **Well-designed.** The tab-delimited format is simple and robust. The atomic swap on flush (write failures to `.tmp`, rename to `.log`) prevents corruption. Pruning keeps the newest data (most operationally relevant).

**Finding [I-12]**: The prune function seeks to an offset and skips to the next newline, which means the first note after the prune point may be truncated. The flush function handles truncated lines by skipping them — so this is safe, but it means one note per prune cycle is silently lost.

### 4.12 Notecard Failure Handling

**Functions**: `checkNotecardHealth()`, periodic retry in `loop()` (Client L2001–2032)

**Decision**: Health probe via `card.wireless`. Failure threshold of 5 before declaring offline. Recovery retry every 5 minutes (general) or 60 seconds (from poll functions). Every success resets failure counter.

**Assessment**: **Correct.** The I2C bus recovery (clock-toggle) at the `I2C_NOTECARD_RECOVERY_THRESHOLD` handles bus lockup from a stuck slave. The exponential backoff (in the Viewer; the Client uses a fixed 5-min retry) prevents CPU waste on persistent hardware failures.

**Finding [I-13]**: The Client and Viewer use different backoff strategies for Notecard health checks. The Client uses a fixed 5-minute interval; the Viewer uses exponential backoff doubling up to a maximum. This inconsistency is not harmful (both converge on a workable retry cadence), but if one approach proves superior in the field, the other should be updated to match. Both implementations exist by convention, not shared code.

---

## 5. Server Decision Pathways

### 5.1 SMS Dispatch & Rate Limiting

**Functions**: `processClientAlarm()`, `checkSmsRateLimit()`, `sendSmsAlert()` (Server L8350–9100)

#### Three-Gate System
An SMS is sent only if ALL three conditions are met:
1. `smsEnabled` — Client explicitly requested SMS for this alarm type
2. `smsAllowedByServer` — Server policy allows SMS for this alarm category (high/low/clear, configurable)
3. `checkSmsRateLimit()` — Rate limit not exceeded

**Assessment**: **Well-layered.** The client-side gate prevents diagnostic alarms from generating SMS. The server-side gate provides fleet-wide policy control. The rate limiter prevents flooding.

#### Rate Limiting Details
- **Minimum interval**: 5 minutes between SMS for the same sensor
- **Hourly cap**: 2 SMS per sensor per hour (sliding window)
- **Timestamp array**: 10 slots per sensor, with hour-old entries pruned on each check

**Finding [C-2, MODERATE]**: **SMS fail-open when time sync is unavailable.** The Server's `checkSmsRateLimit()` contains:
```cpp
if (now <= 0.0) {
    return true;  // No time sync yet, allow SMS
}
```
This means if the Server's Notecard loses time sync (e.g., cellular outage, Notecard power loss), ALL SMS rate limiting is bypassed. A rapidly oscillating alarm could then generate unlimited SMS — up to one per 5-second poll cycle (the Notecard poll interval). The Client's alarm debounce (3 samples at 30-min intervals) limits the ingress rate from a single client, but if multiple clients report alarms simultaneously during a server clock-loss event, the SMS backlog could be significant.

**Recommendation**: Implement a `millis()`-based fallback rate limiter for the no-time-sync case, similar to the Client's approach. A simple "no more than 1 SMS per 60 seconds when time is unavailable" guard would prevent flooding without blocking critical alarms.

#### SMS Content
**Decision**: Alarm type overrides for digital sensors (e.g., "ACTIVATED" / "NOT ACTIVATED" instead of "high" / "low").

**Finding [M-5, MODERATE]**: The `snprintf` for digital alarm SMS messages has a potential format issue:
```cpp
snprintf(message, sizeof(message), "%s%s%d Float Switch %s",
    rec->site, rec->userNumber > 0 ? " #" : " sensor ",
    rec->userNumber > 0 ? rec->userNumber : rec->sensorIndex,
    stateDesc);
```
The `%d` format specifier receives an `uint8_t`, which is promoted to `int` — this is safe. However, the `%s` at the end receives `stateDesc` which is a `const char *` from a `strcmp` result — also safe. The total message length is bounded by the 160-byte buffer with `sizeof(message)`. **No overflow risk**, but the message format concatenates site name directly with " #" or " sensor " — long site names could truncate the alarm state descriptor. A site name of 140 characters would leave only 20 bytes for " #X Float Switch ACTIVATED\0". **Low severity** — site names are unlikely to exceed ~30 characters in practice.

### 5.2 Stale Client Detection

**Function**: `checkStaleClients()` (Server L9934–10050+)

#### 5-Phase Algorithm:
1. **Dedup**: `deduplicateSensorRecordsLinear()` — catch duplicates from race conditions
2. **Per-client analysis**: Aggregate sensor staleness per client
3. **Orphan sensor pruning**: If some sensors from a client are >72h stale but others are fresh, prune the stale ones (likely removed/reconfigured sensors)
4. **Stale SMS alert**: If no data for >49 hours and alert not already sent → SMS
5. **Auto-removal**: If no data for >7 days → archive and remove

**Assessment**: **Sophisticated and well-layered.** The tiered thresholds (49h alert → 72h orphan prune → 7-day removal) provide escalating responses. The dedup phase prevents phantom stale entries from duplicated sensor records.

**Finding [M-6, MODERATE]**: **No "client recovered" SMS.** When a stale client resumes reporting, the `staleAlertSent` flag is cleared (via `meta.staleAlertSent = false` when new data arrives), but no SMS notification is sent. An operator who received a "client offline" SMS must check the web dashboard to verify recovery. For a fleet of many remote tanks, this creates an asymmetric notification experience.

**Recommendation**: Send a "client recovered" SMS when the first data arrives after `staleAlertSent` was true. Rate-limit to one per client per 24 hours to prevent flapping alerts.

**Finding [I-14]**: The orphan sensor pruning (>72h stale when sibling sensors are fresh) could incorrectly prune a sensor that legitimately reports infrequently. For example, a client with a 30-minute tank sensor and a once-per-week diagnostic sensor — the diagnostic sensor would be pruned after 3 days even though it's functioning correctly. **In practice**, this is mitigated by the condition `staleSensors < totalSensors` (only pruning when some are fresh), but the scenario is possible with mixed-frequency sensor configurations.

### 5.3 24-Hour Change Tracking

**Function**: Inline in `processClientTelemetry()` (Server L8715)

**Decision**:
```cpp
if (lastUpdateEpoch > 0 && (now - lastUpdateEpoch) >= 22_hours)
    previousLevel = currentLevel  // Roll baseline
else if (previousEpoch == 0 && lastUpdateEpoch > 0)
    previousLevel = currentLevel  // Initialize baseline
```

**Assessment**: **Reasonable.** The 22-hour threshold ensures the "previous" baseline is always roughly 24 hours old (within the 22–48 hour window). This is a simplification of a true 24-hour rolling window, but it works well when readings arrive at consistent intervals.

**Finding [M-7, MODERATE]**: **24-hour change is undefined during infrequent reporting.** If a client reports every 30 minutes (normal), the previous baseline rolls every ~22-24 hours — correct. But if a client is in LOW_POWER mode (reporting every 24 hours due to 4× outbound multiplier with 6-hour solar base), the baseline roll-forward happens on every single telemetry arrival. The "24h change" then represents the change over one reporting interval (24h), which is coincidentally correct. However, if the client's outbound is 48 hours (theoretically possible with extreme multiplier stacking), the baseline rolls to the prior reading, making "24h change" actually a 48-hour change. **This is cosmetically misleading but operationally harmless** — the dashboard shows it as "24h change" regardless of actual window.

### 5.4 Daily Email Scheduling

**Function**: `scheduleNextDailyEmail()`, `sendDailyEmail()` (Server L5563–9280)

**Decision**: Schedule is computed as `UTCmidnight + dailyHour × 3600 + dailyMinute × 60`. If the scheduled time has already passed today, defer to tomorrow.

**Finding [C-3, MODERATE]**: **UTC-based scheduling with no timezone awareness.** The Web UI presents the daily email time as a simple HH:MM input with no timezone indicator. Users naturally enter their local time (e.g., "06:00" meaning 6 AM local). But the Server interprets this as UTC. A user in US Central time (UTC-6) setting "06:00" will receive their daily email at midnight local time.

**Impact**: This is a usability issue that leads to misconfigured email times for every operator who doesn't manually calculate their UTC offset. The configuration is correct from a code perspective (UTC is unambiguous), but the UI creates a trap.

**Recommendation**: Either (a) add the timezone offset to the Server's config (from the client's GPS location or manual entry) and display times in local time, or (b) clearly label the time input as "UTC" in the web interface with a helper showing the equivalent local time based on browser timezone.

#### Rate Limit
**Decision**: Minimum 1 hour between daily emails.

**Assessment**: Correct safeguard against double-sending from clock adjustments or scheduler re-triggering.

### 5.5 Calibration Learning System

**Function**: `recalculateCalibration()` (Server L12133–12500+)

#### Two-Path Regression

**Path 1 — Multiple Linear Regression** (when ≥5 temp-enabled points with ≥10°F variation):
$$\text{level} = \beta_0 + \beta_1 \cdot \text{mA} + \beta_2 \cdot (T - 70°\text{F})$$
Solved via Cramer's rule on 3×3 normal equations.

**Path 2 — Simple Linear Regression** (when ≥2 points):
$$\text{level} = \text{slope} \cdot \text{mA} + \text{offset}$$
Standard OLS.

**Assessment**: **Sound mathematical approach.** The temperature normalization to 70°F reference improves numerical conditioning. The R² goodness-of-fit check provides confidence metrics. The graceful fallback from multivariate → simple → raw passthrough ensures the system never produces worse results from calibration than from no calibration.

**Finding [M-8, MODERATE]**: **Cramer's rule near-singular detection insufficiency.** The singular matrix check is `fabs(det) < 0.0001f`. This threshold is absolute, not relative to the matrix norm. If all data points are tightly clustered (e.g., tank always at 80-90% capacity, temperature always 65-75°F), the matrix entries are large but nearly coplanar, producing a numerically small determinant relative to the matrix magnitude. The condition number of the matrix could be very high while `det > 0.0001`. This would produce coefficients with high variance.

**Recommendation**: Replace the absolute determinant threshold with a condition-number estimate. A simple approach: compute `det / (norm(column1) × norm(column2) × norm(column3))` and check if this ratio is below a threshold. Alternatively, use the ratio of eigenvalues (the SVD approach) — but this is complex for embedded. A pragmatic alternative: validate the resulting calibrated values against the training data and require R² ≥ 0.8 before accepting the multivariate model (which is already partially implemented — the R² is computed but not used as a gate for acceptance).

**Finding [I-15]**: The calibration only applies to 4-20mA current-loop sensors (validated by the `4.0 ≤ reading ≤ 20.0` filter). This is by design — analog voltage and digital sensors don't need multipoint calibration. But if a user attempts to add calibration points for a voltage sensor (which would be rejected by the filter), there's no user-facing error message in the web UI explaining why calibration isn't updating.

### 5.6 Config Dispatch & ACK Tracking

**Decision**: Config is always written to local cache first, then sent to Notecard. Auto-retry every 60 seconds. ACK is verified via DJB2 hash of the config payload.

**Assessment**: **Robust.** The dual-write (cache + Notecard) ensures the Server retains the config even if the Notecard send fails. The hash-based ACK is a simple but effective means of confirming the Client received the correct config. The auto-retry prevents silent config divergence.

**Finding [I-16]**: The DJB2 hash has a 32-bit space (~4 billion values). For config payloads of a few hundred bytes, collision probability is negligible. However, DJB2 is not cryptographically secure — a malicious payload crafted to produce the same hash as a legitimate config could bypass ACK verification. This is a non-concern in the Notehub environment (the route uses TLS and Notehub authentication).

### 5.7 Server-Down Detection

**Decision**: On boot, check if the last heartbeat file is >24 hours old. If so, send a one-time "server was down" SMS. Write hourly heartbeat files.

**Assessment**: **Correct.** The once-per-boot restriction prevents repeated alerts on rapid reboot cycles. The 24-hour threshold avoids false positives from brief power glitches (a 1-second power loss and reboot would show <1 minute since last heartbeat).

**Finding [I-17]**: If the server crashes and reboots multiple times (e.g., hardware fault causing reboot loop), each boot that finds a >24h heartbeat gap sends an SMS. But the reboot-loop typically occurs within seconds, so only the first boot after a prolonged outage (>24h) would trigger the SMS — subsequent rapid reboots would find a fresh heartbeat from the first boot's hourly write.

### 5.8 Authentication & Session Management

**Decision**: PIN-based authentication with exponential backoff (1s, 2s, 4s, 8s, 16s). After 5 failures, 30-second lockout.

**Assessment**: **Appropriate for the threat model.** The Server is on a local network (Ethernet), not exposed to the internet. A 4-digit PIN with exponential backoff is sufficient against casual unauthorized access. The session management uses browser-side token storage with 90-day expiry.

**Finding [I-18]**: The exponential backoff resets on success. An attacker who guesses correctly once can then attempt brute force at 1-per-second. However, with a 4-digit PIN (10,000 combinations) and 5 failures → 30-second lockout → reset after success, the expected brute-force time is ~138 hours. This is acceptable for LAN-only access.

### 5.9 Viewer Summary Publishing

**Decision**: Every 6 hours, aligned to a 6 AM base (6:00, 12:00, 18:00, 0:00 UTC). Published to `viewer_summary.qo` via Notecard.

**Assessment**: **Aligned with Viewer fetch schedule.** The Viewer's 6-hour fetch aligns with the Server's 6-hour publish, ensuring the Viewer always gets fresh data.

**Finding [I-19]**: If the Server and Viewer have slightly different time sync offsets, the Viewer might fetch slightly before the Server publishes. The Viewer would then get the previous summary (or nothing if it was already consumed). The next fetch (6 hours later) would get the delayed summary — causing a 6-hour staleness. This is unlikely in practice (both sync from Notecard, so their clocks should be within seconds).

### 5.10 FTP Backup & Restore

**Decision**: Backup on config change (if auto-backup enabled), on boot (if restore-on-boot enabled), and on manual trigger.

**Assessment**: **Good for disaster recovery.** The FTP credentials are stored in Server config — changing them requires authenticated access via the web UI.

**Finding [I-20]**: FTP uses plain-text credentials and unencrypted transfers. SFTP/FTPS is not supported. For a LAN-only deployment, this is acceptable. If the FTP server is on a remote network, credentials are exposed in transit.

---

## 6. Viewer Decision Pathways

### 6.1 Summary Fetch & Scheduling

**Function**: `fetchViewerSummary()`, `scheduleNextSummaryFetch()` (Viewer L770–903)

**Decision**: Drain queued summaries in a loop with a 20-note safety cap. Schedule next fetch using aligned epoch calculation.

**Assessment**: **Correct.** The drain loop ensures multiple queued summaries (from Viewer downtime) are consumed. The 20-note cap prevents infinite loops from Notecard API issues.

**Finding [I-21]**: `note.get` with `delete: true` means each consumed note is irreversibly removed. If `handleViewerSummary()` fails after `note.get` returns (e.g., OOM during JSON parse), that summary data is lost. The 6-hour publishing cadence means the next summary will arrive in 6 hours — operationally acceptable.

### 6.2 Web Server & HTTP Handling

**Functions**: `handleWebRequests()`, `readHttpRequest()` (Viewer L514–635)

**Decision**: Only two endpoints: `GET /` (dashboard) and `GET /api/sensors` (JSON). No authentication.

**Assessment**: **Appropriate for a read-only kiosk display.** No write operations are exposed, eliminating CSRF and unauthorized modification risks.

**Finding [C-4, MODERATE/LOW]**: **Raw `delay()` in Ethernet retry risks watchdog reset.** `initializeEthernet()` retries with `delay(attempt * 5000)` — the third retry uses `delay(15000)`. With a watchdog timeout of ~16 seconds, this 15-second delay leaves only 1 second of margin. Any additional processing delay before the retry could cause a watchdog reset. **Use `safeSleep()` instead of `delay()` in the retry loop.**

**Finding [I-22]**: **Exact path matching.** `/api/sensors?param=val` returns 404 because the path comparison is exact. Query parameters are not stripped. This is acceptable for the Viewer's simple two-endpoint API, but if future endpoints need query parameters, the parsing must be updated.

**Finding [I-23]**: The `readHttpRequest()` function has a `bodyTooLarge` flag that caps body reading at 1024 bytes. However, the remaining body data stays in the TCP receive buffer. When `client.stop()` is called, the TCP stack sends RST — but on some MCU TCP implementations, the client may interpret this as a connection error rather than a clean close after the 413 response.

### 6.3 Notecard Health & Recovery

**Function**: Inline in `loop()` (Viewer L308–360)

**Decision**: Exponential backoff from base interval (doubling each failure) up to maximum. I2C bus recovery after repeated failures.

**Assessment**: **Superior to the Client's fixed-interval retry** for prolonged hardware faults. The exponential backoff reduces I2C bus contention when the Notecard is genuinely unresponsive.

### 6.4 DFU Update Flow

**Function**: `checkForFirmwareUpdate()`, `enableDfuMode()` (Viewer L907–997)

**Decision**: DFU auto-enable is compile-time gated (`#ifdef DFU_AUTO_ENABLE`). Must explicitly opt-in.

**Assessment**: **Conservative and correct.** Preventing accidental remote firmware updates on field hardware is important. The compile-time gate means even a compromised Notehub route cannot push unauthorized firmware.

### 6.5 Client-Side JavaScript

**Decision**: Client-side stale detection at 26 hours (93.6 million ms). XSS protection via `escapeHtml()`.

**Assessment**: **Good.** The 26-hour UI-level stale threshold provides an early visual warning before the Server's 49-hour SMS threshold fires. The `escapeHtml()` function correctly maps `& < > " '` to HTML entities, preventing reflected XSS from sensor labels or site names.

**Finding [I-24]**: `formatFeetInches()` returns `--` for negative values. A sensor reading of −0.1 inches (possible from minor calibration drift) shows as `--` rather than a value. This is a reasonable UX choice — negative levels are physically meaningless — but log-level diagnostics might benefit from showing the raw value on hover.

---

## 7. Cross-System Decision Interactions

### 7.1 End-to-End Alarm Propagation

```
Client sensor read → Client debounce (3 samples) → Client rate limit (millis)
       → Notecard outbound → Notehub route → Server Notecard inbound
       → Server alarm processing → Server SMS rate limit (epoch)
       → Server SMS dispatch
```

**Assessment**: The dual rate limiting (client + server) provides defense-in-depth. Even if the client's rate limiter fails (clock issue, counter corruption), the server's per-sensor rate limiter caps SMS output.

**Finding [I-25]**: If the Server's time sync fails AND the Client sends rapid alarms (bypassing its own rate limiter due to a bug), the fail-open behavior on both systems could theoretically allow unlimited SMS. The probability is extremely low (requires simultaneous failures on both client clock and server clock), but the consequence (large SMS bill) could be significant.

### 7.2 Stale Detection Threshold Ladder

| Width | Threshold | Purpose |
|-------|-----------|---------|
| Viewer JS | 26 hours | Visual early warning (opacity dimming) |
| Server SMS | 49 hours | Operator SMS alert |
| Server orphan prune | 72 hours | Remove orphaned sensors |
| Server auto-remove | 7 days | Auto-cleanup with FTP archive |

**Assessment**: **Well-tiered.** Each threshold rises from the previous with clear operational meaning. The 26-hour visual warning gives operators time to investigate before the 49-hour SMS fires. The 72-hour prune → 7-day removal escalation provides graceful degradation.

### 7.3 Power State vs. Server Expectations

| Client State | Outbound Interval (Solar) | Server Stale Threshold | Margin |
|-------------|---------------------------|----------------------|--------|
| NORMAL | 6h | 49h | ~43h (8 missed) |
| ECO | 12h | 49h | ~37h (4 missed) |
| LOW_POWER | 24h | 49h | ~25h (2 missed) |
| CRITICAL | No outbound | 49h | 0h — will fire |

**Assessment**: The margin shrinks as power state degrades, which is expected. In CRITICAL, the client sends no outbound data, so the server WILL declare it stale after 49 hours. This is correct — a CRITICAL client should be flagged. The power state change notification (sent before entering CRITICAL) gives the server context for why the client went silent.

**Finding [I-26]**: A client transitioning from NORMAL to CRITICAL sends a power state change notification, then enters CRITICAL which disables outbound sync. If the Notecard happens to not sync that final notification before the outbound interval elapses, the server never learns the client entered CRITICAL. The daily report (which would have reported the power state) is also skipped in CRITICAL. **The server will eventually detect staleness, but without knowing the cause.**

### 7.4 Config Propagation Timing

```
Operator saves config (web UI) → Server writes cache → Server pushes to Notecard
    → Notehub route → Client Notecard inbound (next poll)
    → Client applies config → Client sends ACK → Server receives ACK
```

Worst-case latency (solar ECO mode):
- Server-to-Notecard: immediate
- Notehub propagation: seconds
- Client inbound poll (solar ECO): 4 × 60 min = 4 hours
- Client-to-Server ACK: 2 × SOLAR_OUTBOUND = 12 hours

**Total worst case: ~16 hours** for config round-trip ACK in solar ECO mode.

**Assessment**: Acceptable for non-urgent configuration changes. For urgent changes (e.g., alarm threshold adjustment), the latency in degraded power states could be operationally problematic. There is no mechanism to force an immediate sync.

---

## 8. Findings Summary

### 8.1 Critical Findings

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| **C-1** | Client | Stuck sensor detection defaults to enabled; fires false positives on idle tanks after 10 identical readings (~5 hours at 30-min interval) | Spurious sensor-fault alarms on stable tanks; operators learn to ignore alarms |
| **C-2** | Server | SMS rate limiting fails open when `currentEpoch()` returns 0 (no time sync) | Potential SMS flooding during server clock-loss event |
| **C-3** | Server | Daily email scheduling treats user-entered time as UTC with no UI indication | Emails arrive at unexpected local times for every non-UTC operator |
| **C-4** | Viewer | Ethernet retry uses `delay(15000)` instead of `safeSleep()`, risking watchdog reset | Viewer could enter reboot loop if Ethernet init fails on third retry |

### 8.2 Moderate Findings

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| **M-1** | Client | Ultrasonic sensors have no outlier rejection for transient short-echo reflections | Single bad reflection could propagate to dashboard (mitigated by 3-sample alarm debounce) |
| **M-2** | Client | Sensor recovery notifications are not rate-limited | Oscillating sensor generates one recovery note per valid reading |
| **M-3** | Client | CRITICAL_HIBERNATE disables all sampling and alarm evaluation | Overflowing tank in a CRITICAL-battery situation generates no alarm |
| **M-4** | Client | Unload detection single-peak tracking is correct as designed | (No actual gap — included for documentation completeness) |
| **M-5** | Server | Long site names in SMS could truncate alarm descriptor | SMS readability degraded with >80-char site names |
| **M-6** | Server | No "client recovered" SMS after stale alert clears | Operators must check dashboard to verify recovery |
| **M-7** | Server | 24h change label is misleading when client reporting interval exceeds 24 hours | Dashboard shows "24h change" for a 48-hour window in extreme power-save modes |
| **M-8** | Server | Cramer's rule singular detection uses absolute threshold; misses ill-conditioned matrices | Calibration regression could produce inaccurate coefficients with clustered data |

### 8.3 Minor Findings

| ID | Component | Finding | Impact |
|----|-----------|---------|--------|
| I-1 | Client | Digital alarm legacy fallback is safe | No impact |
| I-2 | Client | Zero hysteresis would create unreachable clear zone | Hysteresis should be validated > 0 for analog |
| I-3 | Client | Time-based RPM: single-pulse in window keeps last reading | Brief observability gap at detection limit |
| I-4 | Client | 0.2V gap between ECO/LOW_POWER entry thresholds | Possible rapid state transitions under load |
| I-5 | Client | Battery failure decay timer uses millis() correctly | No impact — wall time is correct |
| I-6 | Client | Telemetry threshold is absolute, not percentage | Operator must understand unit semantics |
| I-7 | Client | Unload empty height clamp is correct | No impact |
| I-8 | Client | Momentary relay uses minimum duration across mask | Documented behavior, could surprise operators |
| I-9 | Client | millis() sliding window briefly fails at 49.7-day rollover | ~1h window, debounce catches on next cycle |
| I-10 | Client | RPM accumulated vs. burst count isolation verified | No double-counting |
| I-11 | Client | Pulse mode reset on mode change is correct | No impact |
| I-12 | Client | Note buffer prune loses one note per prune cycle | Minimal data loss |
| I-13 | Client/Viewer | Different Notecard health check backoff strategies | Inconsistency by convention, not harmful |
| I-14 | Server | Orphan sensor prune could affect infrequent-reporting sensors | Possible with mixed-frequency configurations |
| I-15 | Server | Calibration UI doesn't explain why non-current-loop sensors can't calibrate | UX gap |
| I-16 | Server | DJB2 hash is non-cryptographic | Non-concern in TLS/Notehub environment |
| I-17 | Server | Server-down SMS on rapid reboot cycles | Self-limiting; only first boot after >24h gap fires |
| I-18 | Server | PIN brute-force ~138 hours on LAN-only | Acceptable for threat model |
| I-19 | Server/Viewer | 6-hour summary timing alignment window | Sub-second Notecard clock differences, negligible |
| I-20 | Server | FTP uses unencrypted transfers | Acceptable for LAN; document for remote deployments |
| I-21 | Viewer | note.get with delete:true loses data on parse failure | 6-hour recovery from next publish |
| I-22 | Viewer | Exact path matching excludes query parameters | Acceptable for 2-endpoint API |
| I-23 | Viewer | TCP buffer contamination after bodyTooLarge | Mitigated by client.stop() RST |
| I-24 | Viewer | Negative inches displayed as "--" | Correct UX choice |
| I-25 | Cross-system | Theoretical dual-failure unlimited SMS scenario | Extremely low probability |
| I-26 | Cross-system | CRITICAL entry notification may not sync before outbound stops | Server detects staleness but without context |

### 8.4 Informational Observations

1. **Code-sharing by convention**: The safeSleep pattern, time sync logic, and I2C health check are duplicated across Server/Client/Viewer with slight variations. If one is updated, the others may drift. A shared `.cpp` implementation would enforce consistency.

2. **Alarm debounce count is not configurable**: The `ALARM_DEBOUNCE_COUNT` of 3 is compile-time only. For use cases requiring faster response (e.g., overfill protection), this cannot be tuned without recompilation.

3. **No alarm escalation**: The system has a single alarm level per threshold. There is no concept of "warning" (approaching threshold) vs. "critical" (threshold exceeded). Some industrial monitoring systems use a two-tier approach.

4. **XSS protection is comprehensive**: All user-sourced strings in both Server and Viewer JavaScript are escaped via `escapeHtml()`. Content Security Policy headers are not set, but inline script execution is necessary for the embedded PROGMEM HTML approach.

---

## 9. Recommendations

### Priority 1 — Address Critical Findings

1. **C-1: Default stuck detection to disabled** or increase the threshold to 24 hours (not 10 samples). For tanks, a level that doesn't change for hours is normal, not a fault. Alternatively, compute a sensor-type-specific stuck threshold (e.g., RPM sensors: stuck detection ON by default; tank level sensors: OFF by default).

2. **C-2: Add millis()-based SMS fallback.** When `currentEpoch() <= 0`, use a `millis()`-based minimum interval (e.g., 60 seconds) instead of failing open. This preserves alarm delivery during clock loss while preventing flooding:
   ```cpp
   if (now <= 0.0) {
       static unsigned long lastSmsMillis = 0;
       if (millis() - lastSmsMillis < 60000UL) return false;
       lastSmsMillis = millis();
       return true;
   }
   ```

3. **C-3: Add timezone awareness to daily email scheduling.** At minimum, label the web UI time input as "UTC" with a calculated local-time preview based on the browser's `Intl.DateTimeFormat().resolvedOptions().timeZone`. Ideally, allow timezone configuration and convert internally.

4. **C-4: Replace `delay()` with `safeSleep()` in Viewer's `initializeEthernet()`.** This is a one-line change per retry that prevents watchdog reset.

### Priority 2 — Address Moderate Findings

5. **M-2: Rate-limit sensor recovery notifications.** Add a minimum 60-second interval between `"sensor-recovered"` notes for the same monitor. This can reuse the existing per-type timestamp tracking in `MonitorRuntime`.

6. **M-6: Add "client recovered" SMS.** When `meta.staleAlertSent` transitions from `true` to `false` (on receiving fresh data), queue a recovery SMS.

7. **M-8: Strengthen calibration regression conditioning checks.** Either use the computed R² as a gate (reject models with R² < 0.7) or compute a relative determinant threshold.

### Priority 3 — Consider for Future Iterations

8. **I-2: Validate hysteresis > 0** for analog sensors during config application.

9. **I-13: Unify Notecard health check strategy** across all three components. Extract to a shared function in the Common library.

10. **I-14: Protect infrequent-reporting sensors from orphan pruning** by checking per-sensor reporting frequency, not just staleness.

---

## 10. Appendix: Threshold & Constant Registry

### Client Constants
| Constant | Value | Used In | Assessment |
|----------|-------|---------|------------|
| `ALARM_DEBOUNCE_COUNT` | 3 | `evaluateAlarms()` | Appropriate for 30-min interval |
| `SENSOR_FAILURE_THRESHOLD` | 5 | `validateSensorReading()` | Conservative; 2.5h at 30-min interval |
| `SENSOR_STUCK_THRESHOLD` | 10 | `validateSensorReading()` | **Too aggressive for idle tanks** (C-1) |
| `UNLOAD_DEBOUNCE_COUNT` | 3 | `evaluateUnload()` | Prevents false unload detection |
| `UNLOAD_MIN_PEAK_HEIGHT` | 12 inches | `evaluateUnload()` | Prevents empty-tank noise |
| `POWER_STATE_DEBOUNCE_COUNT` | 3 | `updatePowerState()` | Prevents voltage transient transitions |
| `MIN_ALARM_INTERVAL_SECONDS` | 300 (5 min) | `checkAlarmRateLimit()` | Appropriate per-type interval |
| `MAX_ALARMS_PER_HOUR` | 10 | `checkAlarmRateLimit()` | Reasonable per-monitor cap |
| `NOTECARD_FAILURE_THRESHOLD` | 5 | `checkNotecardHealth()` | Takes 5 failures to go offline |
| `NOTE_BUFFER_MAX_BYTES` | 16384 | `pruneNoteBufferIfNeeded()` | ~16 notes × 1KB |
| `CLEAR_BUTTON_DEBOUNCE_MS` | 50 | `checkClearButton()` | Standard contact debounce |
| `CLEAR_BUTTON_MIN_PRESS_MS` | 500 | `checkClearButton()` | Prevents accidental activation |

### Client Power State Thresholds (Lead-Acid)
| Boundary | Voltage | Gap from Previous |
|----------|---------|-------------------|
| ECO entry | 12.0V | — |
| ECO exit | 12.4V | +0.4V hysteresis |
| LOW_POWER entry | 11.8V | −0.2V from ECO entry |
| LOW_POWER exit | 12.3V | +0.5V hysteresis |
| CRITICAL entry | 11.5V | −0.3V from LOW entry |
| CRITICAL exit | 12.2V | +0.7V hysteresis |

### Server Constants
| Constant | Value | Used In | Assessment |
|----------|-------|---------|------------|
| `MIN_SMS_ALERT_INTERVAL_SECONDS` | 300 (5 min) | `checkSmsRateLimit()` | Matches client interval |
| `MAX_SMS_ALERTS_PER_HOUR` | 2 | `checkSmsRateLimit()` | Very conservative (server-side) |
| `STALE_CLIENT_THRESHOLD_SECONDS` | 176400 (49h) | `checkStaleClients()` | ~2 missed daily reports |
| `ORPHAN_SENSOR_PRUNE_SECONDS` | 259200 (72h) | `checkStaleClients()` | Escalation from 49h stale |
| `STALE_CLIENT_PRUNE_SECONDS` | 604800 (7d) | `checkStaleClients()` | Final escalation with archive |
| `MAX_CALIBRATION_ENTRIES` | 100 | `recalculateCalibration()` | Adequate for monthly calibration over ~8 years |
| `TEMP_REFERENCE_F` | 70.0 | `recalculateCalibration()` | Standard room temp reference |
| `HOURS_22` | 79200s | 24h change tracking | Rolling baseline window |

### Viewer Constants
| Constant | Value | Used In | Assessment |
|----------|-------|---------|------------|
| `WEB_REFRESH_SECONDS` | 21600 (6h) | Dashboard auto-refresh | Matches summary publish cycle |
| `SUMMARY_FETCH_INTERVAL_SECONDS` | 21600 (6h) | Summary fetch schedule | Aligns with server publish |
| `SUMMARY_FETCH_BASE_HOUR` | 6 | Schedule alignment | 6:00, 12:00, 18:00, 0:00 UTC |
| `MAX_HTTP_BODY_BYTES` | 1024 | `readHttpRequest()` | Defense-in-depth for GET-only server |
| JS stale threshold | 93600000ms (26h) | Client-side stale warning | Early warning before server 49h SMS |

---

*End of review. Document generated by GitHub Copilot (Claude Opus 4.6) on March 24, 2026.*
