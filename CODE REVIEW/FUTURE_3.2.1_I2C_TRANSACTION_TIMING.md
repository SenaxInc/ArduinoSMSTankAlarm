# Future Improvement 3.2.1 — I2C Transaction Timing Telemetry

**Priority:** Medium  
**Effort:** 4–6 hours  
**Risk:** Low — read-only instrumentation, no behavioral changes  
**Prerequisite:** None (but benefits from 3.1.2 if extracting to shared header)  
**Date:** 2026-02-26  

---

## Problem Statement

The current I2C reliability infrastructure detects **failures** (NACK, short read, timeout) but provides no visibility into **degradation**. A healthy I2C bus might show transactions completing in 200–500µs, while a degrading bus (due to capacitance, noise, or weak pull-ups) might show transactions stretching to 5–15ms before eventually failing.

By instrumenting I2C transaction timing, operators could detect bus degradation days before it causes outright measurement gaps — enabling proactive maintenance (e.g., checking pull-up resistors, cable connections, shielding).

---

## Current I2C Transaction Flow

### `readCurrentLoopMilliamps()` (Client .ino ~3207)

```
Wire.beginTransmission(addr)    ← ~50µs (address phase)
Wire.write(channel)             ← ~50µs (data phase)
Wire.endTransmission(false)     ← ~200µs (repeated start)
Wire.requestFrom(addr, 2)       ← ~400µs (2-byte read)
Wire.read() × 2                 ← ~10µs (buffer read)
```

Total expected: ~700µs per channel, ~5.6ms for 8 channels.

### Notecard I2C Transactions

Notecard transactions are handled by the Blues `note-arduino` library and are not directly instrumented. They involve larger payloads (JSON requests/responses up to 8KB) and can take 10–100ms depending on the command. The Notecard also uses clock stretching, which can extend transactions unpredictably.

---

## Implementation Design

### New Global Variables

```cpp
// I2C transaction timing stats (reset with error counters in daily report)
static uint32_t gI2cTransactionCount = 0;
static uint32_t gI2cTotalMicros = 0;          // Sum of all transaction durations
static uint32_t gI2cMaxTransactionMicros = 0;  // Longest single transaction
static uint32_t gI2cSlowTransactionCount = 0;  // Transactions exceeding threshold

// Threshold for "slow" classification (configurable)
#ifndef I2C_SLOW_TRANSACTION_THRESHOLD_US
#define I2C_SLOW_TRANSACTION_THRESHOLD_US 5000  // 5ms
#endif
```

### Instrumented `readCurrentLoopMilliamps()`

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  if (channel < 0) return -1.0f;

  uint8_t i2cAddr = gConfig.currentLoopI2cAddress 
                    ? gConfig.currentLoopI2cAddress 
                    : CURRENT_LOOP_I2C_ADDRESS;

  for (uint8_t attempt = 0; attempt < I2C_CURRENT_LOOP_MAX_RETRIES; attempt++) {
    if (attempt > 0) delay(2);

    uint32_t txnStart = micros();  // ← NEW: Start timing

    Wire.beginTransmission(i2cAddr);
    Wire.write((uint8_t)channel);
    uint8_t err = Wire.endTransmission(false);

    if (err != 0) {
      uint32_t elapsed = micros() - txnStart;  // ← NEW: Measure even on failure
      gI2cTransactionCount++;
      gI2cTotalMicros += elapsed;
      if (elapsed > gI2cMaxTransactionMicros) gI2cMaxTransactionMicros = elapsed;
      if (elapsed > I2C_SLOW_TRANSACTION_THRESHOLD_US) gI2cSlowTransactionCount++;
      
      if (attempt == I2C_CURRENT_LOOP_MAX_RETRIES - 1) {
        // ... existing error logging ...
        gCurrentLoopI2cErrors++;
      }
      continue;
    }

    if (Wire.requestFrom(i2cAddr, (uint8_t)2) != 2) {
      uint32_t elapsed = micros() - txnStart;
      gI2cTransactionCount++;
      gI2cTotalMicros += elapsed;
      if (elapsed > gI2cMaxTransactionMicros) gI2cMaxTransactionMicros = elapsed;
      if (elapsed > I2C_SLOW_TRANSACTION_THRESHOLD_US) gI2cSlowTransactionCount++;
      
      // ... existing error handling ...
      continue;
    }

    // Success path
    uint32_t elapsed = micros() - txnStart;  // ← NEW: Measure success too
    gI2cTransactionCount++;
    gI2cTotalMicros += elapsed;
    if (elapsed > gI2cMaxTransactionMicros) gI2cMaxTransactionMicros = elapsed;
    if (elapsed > I2C_SLOW_TRANSACTION_THRESHOLD_US) gI2cSlowTransactionCount++;

    // ... existing data processing ...
  }
}
```

### Health Telemetry Integration

Add timing stats to the existing `sendHealthTelemetry()` function:

```cpp
static void sendHealthTelemetry() {
  // ... existing health fields ...
  
  // I2C timing telemetry
  doc["i2c_txn_count"] = gI2cTransactionCount;
  if (gI2cTransactionCount > 0) {
    doc["i2c_avg_us"] = gI2cTotalMicros / gI2cTransactionCount;
  }
  doc["i2c_max_us"] = gI2cMaxTransactionMicros;
  doc["i2c_slow_count"] = gI2cSlowTransactionCount;
  
  // ... existing publishNote() ...
}
```

### Daily Report Counter Reset

Add timing counters to the existing daily reset:

```cpp
// In sendDailyReport(), after existing counter reset:
gI2cTransactionCount = 0;
gI2cTotalMicros = 0;
gI2cMaxTransactionMicros = 0;
gI2cSlowTransactionCount = 0;
```

---

## Telemetry Payload Impact

| Field | Type | Bytes | Description |
|-------|------|------:|-------------|
| `i2c_txn_count` | uint32 | ~15 | Total transactions since last report |
| `i2c_avg_us` | uint32 | ~12 | Average transaction duration (µs) |
| `i2c_max_us` | uint32 | ~12 | Maximum single transaction duration (µs) |
| `i2c_slow_count` | uint32 | ~16 | Transactions exceeding threshold |
| **Total** | | ~55 | Additional bytes per health note |

Health notes are sent every 6 hours by default. At ~55 extra bytes per note, the daily bandwidth impact is ~220 bytes — negligible.

---

## Expected Values and Alerting Thresholds

Based on I2C electrical characteristics at 100 kHz:

| Metric | Healthy | Degrading | Critical |
|--------|---------|-----------|----------|
| avg_us | 300–800 | 800–3000 | >3000 |
| max_us | <2000 | 2000–10000 | >10000 |
| slow_count | 0 | 1–10/day | >10/day |

These thresholds could be used by the Server or Notehub routing rules to generate maintenance alerts.

---

## micros() Overflow Consideration

`micros()` overflows every ~71.6 minutes on 32-bit Arduino. For individual transaction timing (< 100ms), overflow within a single measurement is not a concern. The `uint32_t` subtraction handles the overflow correctly:

```cpp
uint32_t elapsed = micros() - txnStart;  // Correct even across overflow
```

However, `gI2cTotalMicros` could overflow if there are many transactions. At 8 channels × 2 samples/hour × 700µs average = ~11,200µs/hour. Over 24 hours = ~269,000µs. Well within `uint32_t` range (4.3 billion).

---

## Testing Plan

| Test | Method | Expected Result |
|------|--------|----------------|
| Normal operation | Read A0602 channels | `avg_us` in 300–800 range |
| Degraded bus (add capacitance) | Add 1000pF between SDA/GND | `avg_us` increases, `slow_count` > 0 |
| Timing appears in health note | Check Notehub | `i2c_avg_us`, `i2c_max_us` fields present |
| Counters reset daily | Wait for daily report | Counters reset to 0 after report |
| µs overflow handling | Run for 72+ minutes | No timing artifacts |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | +4 globals, instrument `readCurrentLoopMilliamps()`, extend `sendHealthTelemetry()`, extend daily reset |
| `TankAlarm_Config.h` | +1 `#define` (`I2C_SLOW_TRANSACTION_THRESHOLD_US`) |

---

## Future Extension

Once timing data is collected from the field fleet, the `I2C_SLOW_TRANSACTION_THRESHOLD_US` value can be tuned based on actual distributions. A Notehub route could aggregate timing data across all devices and generate fleet-wide I2C health dashboards.

This data also directly supports improvement 3.3.3 (Wire Library Timeout Configuration) by providing baseline measurements to tune timeout values.
