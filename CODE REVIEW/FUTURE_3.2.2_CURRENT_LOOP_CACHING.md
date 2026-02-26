# Future Improvement 3.2.2 — Current Loop Read Caching / Rate Limiting

**Priority:** Medium  
**Effort:** 2–4 hours  
**Risk:** Very Low — optimization, no behavioral change for current config  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The function `readCurrentLoopMilliamps(channel)` performs a full I2C transaction every time it's called. In the current firmware architecture, each tank monitor reads its own unique A0602 channel once per sample interval, so there's no redundant I2C traffic. However, several future scenarios could cause redundant reads:

1. **Multi-sensor configurations** — A tank monitor with both a level sensor and a flow sensor on the same A0602 channel
2. **Alarm re-evaluation** — If alarm evaluation triggers a re-read for confirmation
3. **Display refresh** — If a future Viewer-on-Client feature polls sensors for display
4. **Calibration** — The calibration process may read the same channel repeatedly
5. **Diagnostic scan** — The I2C Utility already reads all channels in sequence, sometimes repeatedly

Caching the last-read value with a TTL would eliminate redundant I2C transactions, reducing bus traffic and error exposure.

---

## Current Read Flow

```
sampleTanks()
  └── for each monitor:
        └── readTankSensor(i)
              └── readCurrentLoopMilliamps(cfg.primaryChannel)
                    └── Wire.beginTransmission(addr)
                        Wire.write(channel)
                        Wire.endTransmission(false)
                        Wire.requestFrom(addr, 2)
                        Wire.read() × 2
                        → ~700µs per call
```

Each channel is read once per sample interval (default 30 minutes in NORMAL, 120 minutes in ECO). With 4 tanks on 4 channels, that's 4 I2C transactions per sample — very light.

---

## Proposed Implementation

### Cache Structure

```cpp
struct CurrentLoopCache {
  float milliamps;           // Cached reading
  unsigned long readMillis;  // millis() when read was taken
  bool valid;                // Whether cache contains a valid reading
};

static CurrentLoopCache gCurrentLoopCache[8];  // Max 8 A0602 channels

// Cache TTL — readings within this window are returned from cache
#ifndef CURRENT_LOOP_CACHE_TTL_MS
#define CURRENT_LOOP_CACHE_TTL_MS 500  // 500ms default
#endif
```

### Modified `readCurrentLoopMilliamps()`

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0 || channel >= 8) return -1.0f;

  // Check cache first
  unsigned long now = millis();
  if (gCurrentLoopCache[channel].valid &&
      (now - gCurrentLoopCache[channel].readMillis) < CURRENT_LOOP_CACHE_TTL_MS) {
    return gCurrentLoopCache[channel].milliamps;
  }

  // ... existing I2C read logic ...

  // On successful read, update cache
  float reading = /* computed value */;
  gCurrentLoopCache[channel].milliamps = reading;
  gCurrentLoopCache[channel].readMillis = now;
  gCurrentLoopCache[channel].valid = true;

  return reading;
}
```

### Cache Invalidation

The cache should be invalidated when:
1. **I2C bus recovery** — Cached values may be stale after bus reset
2. **Hardware reinitialization** — After `reinitializeHardware()`
3. **Cache TTL expiry** — Automatic via timestamp check

```cpp
static void invalidateCurrentLoopCache() {
  for (uint8_t i = 0; i < 8; i++) {
    gCurrentLoopCache[i].valid = false;
  }
}
```

Add `invalidateCurrentLoopCache()` calls in:
- `recoverI2CBus()` — after bus recovery
- `reinitializeHardware()` — after hardware reinit

---

## Memory Impact

```
sizeof(CurrentLoopCache) = 4 (float) + 4 (unsigned long) + 1 (bool) + padding = 12 bytes
8 channels × 12 bytes = 96 bytes
```

96 bytes of SRAM — negligible on the STM32H747's 512KB SRAM.

---

## TTL Selection Rationale

| TTL Value | Use Case |
|-----------|----------|
| 100ms | Tight caching — only prevents true duplicate reads within the same function |
| 500ms (recommended) | Catches all reads within a single `sampleTanks()` iteration across all tanks |
| 5000ms | Aggressive caching — may return slightly stale data for rapid sampling |

The recommended 500ms TTL means that if `sampleTanks()` reads 4 channels in sequence (taking ~3ms total), any re-read of the same channel within that window gets the cached value. At the normal 30-minute sample interval, the cache is always cold when real sampling occurs.

---

## Behavioral Comparison

| Scenario | Without Cache | With Cache (500ms TTL) |
|----------|---------------|----------------------|
| Normal sampling: 4 tanks, 4 unique channels | 4 I2C reads | 4 I2C reads (no cache hits) |
| Two tanks sharing a channel | 2 I2C reads | 1 I2C read + 1 cache hit |
| Alarm re-evaluation reads | Extra I2C reads | Cache hits (same iteration) |
| I2C Utility rapid scan | 8 reads per request | 8 reads (cache cold) |

**Key:** For the current configuration, this change has zero effect on behavior. It's purely future-proofing.

---

## Alternative: Rate Limiting Instead of Caching

Instead of caching the last value, rate-limit I2C reads:

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  static unsigned long lastReadTime[8] = {0};
  
  unsigned long now = millis();
  if ((now - lastReadTime[channel]) < CURRENT_LOOP_MIN_READ_INTERVAL_MS) {
    return gCurrentLoopCache[channel].milliamps;  // Return last value
  }
  lastReadTime[channel] = now;
  // ... proceed with I2C read ...
}
```

This is functionally equivalent but semantically different — it emphasizes "don't read too often" rather than "reuse recent data."

**Recommendation:** Caching is preferred because it's more explicit about what happens (returning cached data) and the TTL naturally handles the "too often" case.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Normal operation unchanged | Sample 4 tanks | Same readings as before, 4 I2C reads |
| Cache hit verification | Call `readCurrentLoopMilliamps(0)` twice within 500ms | Second call returns immediately, no I2C traffic |
| Cache expiry | Call, wait 600ms, call again | Second call performs I2C read |
| Cache invalidation on recovery | Trigger bus recovery, then read | Fresh I2C read after recovery |
| Cache invalidation on reinit | Trigger config update, then read | Fresh I2C read after reinit |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | +20 lines (cache struct, check logic, invalidation calls) |
| `TankAlarm_Config.h` | +1 `#define` (`CURRENT_LOOP_CACHE_TTL_MS`) |

---

## Decision: Implement Now vs. Defer

**Recommendation: Defer until needed.** The current architecture doesn't produce redundant reads, so this is purely preventive. Implementing it now would add complexity (cache invalidation is a common source of subtle bugs) without immediate benefit. Flag it for implementation when:
- Multi-sensor-per-channel configurations are designed
- Alarm confirmation re-reads are added
- I2C transaction timing (3.2.1) reveals unexpectedly high transaction counts
