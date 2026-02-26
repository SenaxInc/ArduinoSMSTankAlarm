# Future Improvement 3.2.3 — Notecard Health Check Backoff

**Priority:** Medium  
**Effort:** 1–2 hours  
**Risk:** Very Low — only affects retry timing during outages  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

When the Notecard becomes unavailable, `gNotecardAvailable` is set to `false` and `checkNotecardHealth()` is called from `loop()` to attempt reconnection. Currently, this health check runs on a **fixed 5-minute interval** regardless of how long the Notecard has been down:

```cpp
// Current implementation in loop() (Client .ino ~1107)
static unsigned long lastHealthCheck = 0;
if (now - lastHealthCheck > 300000UL) {  // Check every 5 minutes
  lastHealthCheck = now;
  if (!gNotecardAvailable) {
    checkNotecardHealth();
  }
}
```

Each `checkNotecardHealth()` call performs an I2C transaction (`card.wireless` request). If the Notecard is physically disconnected or has a hardware fault, these attempts:

1. **Generate unnecessary I2C bus traffic** every 5 minutes, indefinitely
2. **Can trigger bus recovery** after `I2C_NOTECARD_RECOVERY_THRESHOLD` (10) failures — meaning bus recovery fires every 50 minutes during a sustained outage
3. **Produce repetitive Serial log output** that obscures useful information
4. **Consume power** — relevant for battery-powered Client devices in ECO/LOW_POWER states

With exponential backoff, the retry interval would increase over time (5min → 10min → 20min → 40min → 80min), reducing I2C traffic and power consumption during prolonged outages while still checking reasonably often.

---

## Current Behavior Timeline (Notecard Removed at T=0)

| Time | Event | I2C Transactions |
|------|-------|-----------------|
| T+5min | Health check #1, fails | 1 |
| T+10min | Health check #2, fails | 1 |
| T+15min | Health check #3, fails | 1 |
| ... | ... | ... |
| T+50min | Health check #10, bus recovery triggered | 1 + recovery |
| T+55min | Health check #11, fails (counter reset) | 1 |
| T+100min | Health check #20, bus recovery again | 1 + recovery |
| ... | ... | ... |
| T+24h | 288 health checks, ~5 bus recoveries | 288 + 5 recoveries |

---

## Proposed Behavior Timeline (With Backoff)

| Time | Event | Interval Used | I2C Transactions |
|------|-------|---------------|-----------------|
| T+5min | Health check #1, fails | 5min (base) | 1 |
| T+10min | Health check #2, fails | 5min | 1 |
| T+15min | Health check #3, fails | 5min → backoff to 10min | 1 |
| T+25min | Health check #4, fails | 10min → backoff to 20min | 1 |
| T+45min | Health check #5, fails | 20min → backoff to 40min | 1 |
| T+85min | Health check #6, fails, bus recovery | 40min → backoff to 80min | 1 + recovery |
| T+165min | Health check #7, fails | 80min (max cap) | 1 |
| T+245min | Health check #8, fails | 80min | 1 |
| ... | ... | 80min | ... |
| T+24h | ~21 health checks, ~1 bus recovery | 21 + 1 recovery |

**Reduction: 288 checks → ~21 (93% fewer), 5 recoveries → ~1 (80% fewer)**

---

## Implementation

### New Configuration Define

```cpp
// In TankAlarm_Config.h:

// Maximum backoff interval for Notecard health checks (ms)
// Default: 80 minutes (4,800,000 ms). Health check interval starts at 5 min
// and doubles after each failure, capping at this value.
#ifndef NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS
#define NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS 4800000UL  // 80 minutes
#endif
```

### Modified Loop Code

```cpp
// In loop():
static unsigned long lastHealthCheck = 0;
static unsigned long healthCheckInterval = 300000UL;  // Start at 5 minutes

if (now - lastHealthCheck > healthCheckInterval) {
  lastHealthCheck = now;
  if (!gNotecardAvailable) {
    bool recovered = checkNotecardHealth();
    if (recovered) {
      // Notecard recovered — reset backoff
      healthCheckInterval = 300000UL;
      Serial.println(F("Notecard health check interval reset to 5 min"));
    } else {
      // Still failing — increase interval with exponential backoff
      if (healthCheckInterval < NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {
        healthCheckInterval *= 2;
        if (healthCheckInterval > NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS) {
          healthCheckInterval = NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS;
        }
        Serial.print(F("Notecard health check backoff: next in "));
        Serial.print(healthCheckInterval / 60000UL);
        Serial.println(F(" min"));
      }
    }
  }
}
```

### Update `checkNotecardHealth()` Return Value

The current function returns `bool` already (true on recovery), so no signature change needed. Just ensure the return value is used:

```cpp
// Current signature (no change needed):
static bool checkNotecardHealth() {
  // ... returns true if Notecard responded, false otherwise ...
}
```

---

## Power State Integration

In ECO and LOW_POWER states, the Client already samples less frequently to conserve energy. The health check backoff should be **more aggressive** in these states:

```cpp
// Optionally: start with longer interval in low-power states
if (gPowerState >= POWER_STATE_ECO) {
  healthCheckInterval = max(healthCheckInterval, 600000UL);  // Min 10 min in ECO
}
if (gPowerState >= POWER_STATE_LOW_POWER) {
  healthCheckInterval = max(healthCheckInterval, 1200000UL);  // Min 20 min in LOW_POWER
}
```

This ensures battery-powered devices don't waste energy on frequent Notecard checks when power is already constrained.

---

## Edge Cases

### Backoff vs. Notecard Recovery Threshold

The `I2C_NOTECARD_RECOVERY_THRESHOLD` (10 failures) triggers bus recovery inside `checkNotecardHealth()`. With backoff, the threshold takes longer to reach:

- Without backoff: 10 failures × 5min = 50 minutes to first bus recovery
- With backoff: 10 failures spans ~5+5+5+10+20+40+40+40+40+40 = ~245 minutes

**Resolution:** The failure counter (`gNotecardFailureCount`) tracks individual call failures, not time. So bus recovery still fires after exactly 10 failed health checks, just spread over a longer time. This is actually **better** — it avoids doing bus recovery too often while still eventually trying it.

### Reconnection After Long Outage

If the Notecard comes back after hours, the worst-case discovery delay is the current backoff interval (max 80 minutes). This is acceptable because:
- The Notecard being unavailable means the device is already offline
- An 80-minute delay in detecting reconnection vs. the current 5-minute polling is not operationally significant
- The device will discover the Notecard on the next health check and immediately reset the backoff

### millis() Overflow

`unsigned long` overflow occurs at ~49.7 days. The subtraction `now - lastHealthCheck` handles overflow correctly for intervals under 49.7 days, which is well within our max interval of 80 minutes.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Backoff progression | Disconnect Notecard, monitor Serial | Intervals increase: 5, 5, 5, 10, 20, 40, 80, 80... |
| Max cap | Wait for 6+ failures | Interval stops at 80 min |
| Recovery reset | Reconnect Notecard after backoff | Interval resets to 5 min |
| Power state floor | Set ECO mode, disconnect Notecard | Minimum interval is 10 min |
| Failure counter still works | Wait for bus recovery trigger | Recovery fires after 10 failed checks |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | ~15 lines modified in `loop()` health check section |
| `TankAlarm_Config.h` | +1 `#define` (`NOTECARD_HEALTH_CHECK_MAX_INTERVAL_MS`) |

---

## Alternative: Fibonacci Backoff

Instead of power-of-2 doubling, use Fibonacci-like progression (5, 5, 10, 15, 25, 40, 65, 80) for a gentler curve. This checks slightly more often in the first hour.

**Recommendation:** Simple doubling is preferred — it's easier to reason about, matches the pattern already used for sensor-only recovery (1.9.2), and the operational difference is minimal.
