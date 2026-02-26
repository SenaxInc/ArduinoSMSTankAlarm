# Power-State Edge-Case Test Coverage

**Issue:** #250 (workstream 4)  
**Date:** 2026-02-26  
**Firmware:** TankAlarm 112025 Client v1.1.3  

## Overview

This document provides reproducible test procedures for the power conservation
state machine implemented in `updatePowerState()`. Each scenario targets a
specific edge case and documents the expected behavior.

The state machine uses four progressive power states:
```
NORMAL (0) → ECO (1) → LOW_POWER (2) → CRITICAL_HIBERNATE (3)
```

### Voltage Thresholds (12V lead-acid battery)

| Transition          | Enter (falling) | Exit (rising) | Hysteresis |
|---------------------|-----------------|---------------|------------|
| NORMAL → ECO        | < 12.0V         | ≥ 12.4V       | +0.4V      |
| ECO → LOW_POWER     | < 11.8V         | ≥ 12.3V       | +0.5V      |
| LOW_POWER → CRITICAL| < 11.5V         | ≥ 12.2V       | +0.7V      |

### Debounce

All transitions require `POWER_STATE_DEBOUNCE_COUNT` (3) consecutive readings
at the proposed new state before the transition fires.

---

## Test Scenarios

### T1 — Threshold Edge Oscillation (Hysteresis Validation)

**Purpose:** Verify that a voltage oscillating near a threshold boundary does
not cause rapid state toggling.

**Setup:**
- Start in NORMAL state
- Provide a voltage source hovering around the ECO entry threshold (12.0V)

**Procedure:**
1. Set voltage to 12.1V. Verify state stays NORMAL.
2. Drop voltage to 11.99V. Count debounce increments in Serial.
3. After 2 readings at 11.99V, raise voltage to 12.01V.
4. Verify debounce counter resets to 0 (no transition).
5. Drop voltage to 11.99V again for 3 consecutive readings.
6. Verify transition to ECO fires on the 3rd reading.
7. Immediately raise voltage to 12.1V (below exit threshold of 12.4V).
8. Verify state remains ECO (hysteresis prevents immediate recovery).
9. Raise to 12.39V, hold for 3 readings. Verify still ECO.
10. Raise to 12.41V, hold for 3 readings. Verify transition to NORMAL.

**Expected Behavior:**
- Steps 2-4: No transition (debounce interrupted by voltage recovery).
- Step 6: Transition to ECO after 3 consecutive sub-12.0V readings.
- Steps 7-9: State remains ECO (voltage in hysteresis dead zone).
- Step 10: Transition back to NORMAL after 3 readings above 12.4V.

**Pass Criteria:** No spurious transitions during oscillation; exactly 2
transitions total (NORMAL→ECO, ECO→NORMAL).

---

### T2 — Debounce Transition Correctness

**Purpose:** Verify that the debounce counter behaves correctly across all
transitions, including multi-level degradation.

**Procedure:**

#### T2a — Gradual degradation
1. Start in NORMAL at 12.5V.
2. Drop to 11.9V (< 12.0V, ECO entry) for 3 consecutive readings.
   → Expect: NORMAL → ECO
3. Drop to 11.7V (< 11.8V, LOW_POWER entry) for 3 consecutive readings.
   → Expect: ECO → LOW_POWER
4. Drop to 11.4V (< 11.5V, CRITICAL entry) for 3 consecutive readings.
   → Expect: LOW_POWER → CRITICAL_HIBERNATE

#### T2b — Skip-level degradation
1. Start in NORMAL at 12.5V.
2. Drop immediately to 11.4V (below all thresholds) for 3 readings.
   → Expect: NORMAL → CRITICAL_HIBERNATE (skip allowed for degradation)

#### T2c — Stepwise recovery only
1. Start in CRITICAL_HIBERNATE at 11.0V.
2. Raise voltage to 13.0V (above all exit thresholds).
3. Verify transition: CRITICAL → LOW_POWER (not direct to NORMAL).
4. Hold at 13.0V for 3 more readings. Verify: LOW_POWER → ECO.
5. Hold at 13.0V for 3 more readings. Verify: ECO → NORMAL.

#### T2d — Debounce interruption
1. Start in NORMAL at 12.5V.
2. Drop to 11.9V for 2 readings (debounce count = 2).
3. Return to 12.1V for 1 reading.
   → Expect: debounce resets to 0.
4. Drop to 11.9V for 3 readings.
   → Expect: transition fires on 3rd reading.

**Pass Criteria:**
- Recovery is always one step at a time.
- Degradation can skip steps.
- Debounce counter resets when proposed == current.

---

### T3 — Transition Log Rate-Limit Behavior

**Purpose:** Verify `POWER_STATE_TRANSITION_LOG_MIN_MS` (5 minutes) prevents
excessive serial/log output near threshold boundaries.

**Procedure:**
1. Start in NORMAL at 12.5V.
2. Drop to 11.9V for 3 readings → transition to ECO. Note the timestamp.
3. Immediately raise to 12.5V for 3 readings → transition to NORMAL.
4. Verify the second transition Serial.print occurs only if ≥ 5 minutes
   have elapsed since the first transition log.
5. If < 5 minutes, verify the transition STILL FIRES (state changes) but
   the Serial log is suppressed.

**Expected Behavior:**
- The state transition always occurs regardless of log rate limiting.
- The Serial output "Power state: X -> Y" is suppressed if the previous
  transition log was within 5 minutes.
- `sendPowerStateChange()` (Notehub notification) is always called regardless
  of log rate limiting — only Serial output is rate-limited.

**Pass Criteria:**
- State transitions are never delayed or suppressed by log rate limiting.
- Rapid threshold crossings produce at most one Serial log per 5-minute window.

---

### T4 — CRITICAL_HIBERNATE Entry Actions

**Purpose:** Verify all relays are de-energized immediately upon entering
CRITICAL_HIBERNATE.

**Procedure:**
1. Start in NORMAL with relays 0 and 2 activated (via alarm trigger).
2. Confirm relay pins are HIGH (energized).
3. Drop voltage to 11.4V for 3 readings → transition to CRITICAL.
4. Verify all relay pins are LOW (de-energized) immediately after transition.
5. While in CRITICAL, attempt to activate a relay via pollForRelayCommands().
6. Verify relay activation is blocked in CRITICAL (relay commands are not
   processed in CRITICAL_HIBERNATE per the loop guard).

**Expected Behavior:**
- All 4 relays de-energized on CRITICAL entry.
- No relay polling or activation in CRITICAL state.

---

### T5 — Voltage Source Switching

**Purpose:** Verify stable behavior when the effective battery voltage source
changes mid-operation (e.g., solar charger drops out).

**Procedure:**
1. Configure with both SunSaver MPPT (12.5V) and Notecard battery (12.3V).
2. Verify `getEffectiveBatteryVoltage()` returns 12.3V (conservative min).
3. State should be NORMAL.
4. Simulate SunSaver comm failure (returns 0V).
5. Verify voltage source falls back to Notecard battery (12.3V).
6. Verify no spurious state transition.
7. Simulate Notecard battery reporting 11.9V while SunSaver is offline.
8. Verify ECO entry after debounce.
9. Bring SunSaver back online reporting 12.6V.
10. Verify voltage returns to min(12.6V, Notecard) for threshold evaluation.

**Expected Behavior:**
- Source switching does not cause voltage spikes/drops that trigger transitions.
- Conservative (minimum) voltage selection prevents false "all clear."

---

### T6 — Battery Failure Fallback (Solar-Only)

**Purpose:** Verify the battery failure → solar-only fallback mechanism.

**Procedure:**
1. Enable `batteryFailureFallback` in config. Set threshold to 3.
2. Start in NORMAL.
3. Drop voltage below 11.5V → enter CRITICAL_HIBERNATE.
4. Hold in CRITICAL for `batteryFailureThreshold` (3) consecutive
   `updatePowerState()` calls while still CRITICAL.
5. Verify `gSolarOnlyBatteryFailed` becomes true.
6. Verify battery_failure alarm note is published.
7. Raise voltage above 12.2V → exit CRITICAL to LOW_POWER.
8. Verify battery failure counter resets.
9. Continue raising to ECO or NORMAL.
10. Verify `gSolarOnlyBatteryFailed` clears when reaching ECO.

**Expected Behavior:**
- Failure counter only increments while in CRITICAL_HIBERNATE.
- Counter resets upon exiting CRITICAL (any upward transition).
- Solar-only fallback deactivates when state recovers to ECO or better.

---

### T7 — Periodic Power State Logging

**Purpose:** Verify the 30-minute periodic log fires correctly in non-NORMAL
states and does not fire in NORMAL.

**Procedure:**
1. In NORMAL state for 60 minutes. Verify no periodic power state log.
2. Transition to ECO. Note timestamp.
3. Verify first periodic log fires at T + 30 minutes.
4. Verify subsequent logs fire at 30-minute intervals.
5. Transition back to NORMAL. Verify periodic logging stops.

---

## Hardware Test Setup

For bench testing with controlled voltage:
1. Use a bench power supply with 0.01V resolution connected to the Vin
   divider input.
2. Set `DEBUG_MODE` to enable Serial output monitoring.
3. Set `POWER_STATE_DEBOUNCE_COUNT` to 3 (default).
4. Set `POWER_STATE_TRANSITION_LOG_MIN_MS` to 30000 (30s) for faster
   iteration during testing.
5. Monitor Serial output at 115200 baud for state transition messages.

For simulation testing without hardware:
1. Override `getEffectiveBatteryVoltage()` to return injected values.
2. Call `updatePowerState()` in a loop with controlled voltage sequences.
3. Assert state value after each call.

---

## Regression Checklist

After any modification to the power state machine, verify:
- [ ] All 7 test scenarios pass
- [ ] No state transitions during NO voltage source operation (stays NORMAL)
- [ ] Debounce count parameterized by `POWER_STATE_DEBOUNCE_COUNT`
- [ ] Recovery always one step at a time
- [ ] Degradation can skip levels
- [ ] CRITICAL entry de-energizes all relays
- [ ] Battery failure fallback threshold is configurable
- [ ] Periodic log fires only in non-NORMAL states
- [ ] Transition log rate-limiting does not suppress actual state changes
