# Future Improvement 3.3.3 — Wire Library Timeout Configuration

**Priority:** Low  
**Effort:** 4–8 hours (investigation + testing)  
**Risk:** Medium — changing I2C timeout could break Notecard communication  
**Prerequisite:** 3.2.1 (I2C Transaction Timing) recommended for baseline data  
**Date:** 2026-02-26  

---

## Problem Statement

The Arduino `Wire` library on Mbed OS uses a default I2C timeout of approximately **25ms** per transaction. When an I2C device is unresponsive (stuck SDA, missing device, power issue), the calling code blocks for the full 25ms before returning an error.

This matters because:

1. **8 channels × 3 retries × 25ms = 600ms** worst-case blocking time in `readCurrentLoopMilliamps()` when the A0602 is completely unresponsive
2. **Notecard clock stretching** — the Notecard uses clock stretching extensively (holding SCL low while processing). Long JSON responses can cause clock stretches of 5–15ms, which is within the 25ms default but close to the edge
3. **Loop timing** — the main `loop()` iteration needs to complete within the watchdog timeout (30 seconds default). While 600ms is well within budget, reducing I2C blocking time leaves more room for other operations
4. **Responsiveness** — a shorter timeout means faster failure detection, which accelerates the path to bus recovery

---

## Mbed OS Wire Timeout API

### Availability

The Mbed OS I2C implementation (`targets/TARGET_STM/i2c_api.c`) uses HAL-level timeout configuration. The Arduino Wire wrapper on Mbed exposes this through:

```cpp
// Mbed OS Wire library (mbed_opta core)
Wire.setTimeout(uint16_t timeout_ms);  // Not guaranteed to exist
```

**Investigation needed:** Check whether the specific Arduino Opta board support package (BSP) version used in this project exposes `setTimeout()`. The standard Arduino API does not define `setTimeout()` for Wire — it's a Mbed-specific extension that may or may not be present.

### Alternative: Direct HAL Configuration

If `Wire.setTimeout()` is not available, the HAL can be configured directly:

```cpp
#if defined(ARDUINO_ARCH_MBED)
  // Access the underlying Mbed I2C object
  // This is platform-specific and may break across BSP updates
  extern "C" {
    #include "hal/i2c_api.h"
  }
  // Configure timeout at HAL level
  // ... implementation depends on STM32 HAL version ...
#endif
```

**This approach is fragile** and not recommended for production firmware.

---

## Notecard Clock Stretching Behavior

The Blues Notecard is documented to use clock stretching in several scenarios:

| Scenario | Expected Stretch Duration |
|----------|--------------------------|
| Simple request (card.version) | < 1ms |
| Data request (note.get) | 1–5ms |
| Large JSON response | 5–15ms |
| Notecard busy (sync in progress) | 10–25ms |
| Notecard firmware update | Up to 100ms+ |

**Critical constraint:** The I2C timeout MUST be longer than the maximum expected clock stretch. Setting it below 25ms risks timing out on legitimate Notecard responses during sync operations.

---

## Recommended Timeout Values

| Timeout | Effect | Risk |
|---------|--------|------|
| 5ms | Very fast failure detection | HIGH — will timeout on Notecard clock stretching |
| 10ms | Fast failure detection | MEDIUM — marginal for large Notecard responses |
| 25ms (default) | Current behavior | None — proven in field |
| 50ms | Conservative | None — slightly slower failure detection |

### Per-Device Timeout (Ideal)

The optimal approach is different timeouts for different devices:
- **A0602:** 5ms timeout — simple register reads, no clock stretching
- **Notecard:** 50ms timeout — needs room for clock stretching

However, the Wire library typically has a single global timeout.

### Workaround: Per-Transaction Timeout

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  // Use short timeout for A0602 (no clock stretching)
  Wire.setTimeout(5);
  
  Wire.beginTransmission(addr);
  Wire.write(channel);
  // ... I2C operations ...
  
  // Restore default timeout for Notecard operations
  Wire.setTimeout(25);
  
  return value;
}
```

**Risk:** Thread safety — if Mbed OS interrupts this function and another thread uses Wire, the timeout is wrong. This is academic for single-threaded execution but relevant if 3.3.1 (I2C mutex) is implemented.

---

## Investigation Steps

### Step 1 — Verify API Availability

```cpp
// Test sketch
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Attempt to call setTimeout
  #if defined(ARDUINO_OPTA)
    Wire.setTimeout(10);  // Does this compile?
    Serial.println(F("Wire.setTimeout() is available"));
  #else
    Serial.println(F("Wire.setTimeout() not available on this platform"));
  #endif
}
```

Compile for `arduino:mbed_opta:opta` and verify.

### Step 2 — Measure Baseline Timing

Before changing any timeout, collect baseline data:
1. Implement 3.2.1 (I2C Transaction Timing Telemetry)
2. Run for 1 week in the field to collect timing distributions
3. Determine the p99 transaction duration for both Notecard and A0602

### Step 3 — Test with Reduced Timeout

```cpp
// Gradually reduce timeout and test:
Wire.setTimeout(20);  // Start conservative
// Run for 24h, monitor error rates

Wire.setTimeout(15);  // Reduce further
// Run for 24h

Wire.setTimeout(10);  // Target
// Run for 24h, verify no increase in Notecard errors
```

### Step 4 — Validate Under Load

- Trigger Notecard sync while reading A0602 channels
- Perform DFU update while monitoring timeout behavior
- Simulate high I2C error rates and verify timeout doesn't exacerbate issues

---

## Alternative: Software Timeout Wrapper

If `Wire.setTimeout()` doesn't exist, implement a software timeout:

```cpp
static bool wireRequestFromWithTimeout(uint8_t addr, uint8_t count, uint16_t timeoutMs) {
  unsigned long start = millis();
  
  uint8_t received = Wire.requestFrom(addr, count);
  
  unsigned long elapsed = millis() - start;
  if (elapsed > timeoutMs) {
    Serial.print(F("I2C request exceeded timeout: "));
    Serial.print(elapsed);
    Serial.println(F("ms"));
    // Still return the data — the operation completed, just slowly
  }
  
  return (received == count);
}
```

**Limitation:** This doesn't actually abort the I2C transaction — it just measures how long it took. True timeout requires HAL-level support.

---

## Decision Matrix

| Approach | Effectiveness | Complexity | Risk |
|----------|-------------|-----------|------|
| Wire.setTimeout() | High (if available) | Low | Medium — test with Notecard |
| HAL-level configuration | High | High | High — BSP dependency |
| Software timeout wrapper | Low (measurement only) | Low | None |
| Per-transaction timeout swapping | High | Medium | Low (single-threaded) |
| Do nothing (keep 25ms default) | None | None | None |

**Recommendation:** Investigate API availability first. If `Wire.setTimeout()` exists, test with 15ms as a starting point after collecting baseline timing data from 3.2.1.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| API availability | Compile with `Wire.setTimeout(10)` | Compiles without error |
| Normal operation at 15ms | Run 24h with 15ms timeout | No increase in I2C errors |
| Notecard sync at 15ms | Trigger hub.sync while timeout is 15ms | Sync completes successfully |
| DFU at default timeout | Start DFU while measuring timeout | No DFU failures |
| A0602 at 5ms | Set 5ms timeout, read all channels | All channels read correctly |
| Timeout too short | Set 3ms timeout | Notecard errors detected (expected) |

---

## Files Affected

| File | Change |
|------|--------|
| Client .ino | +5 lines — `Wire.setTimeout()` call in `setup()` or per-function |
| `TankAlarm_Config.h` | +1 `#define` for timeout value |
| Potentially: `TankAlarm_I2C.h` (if 3.1.2 is done first) | Timeout configuration in shared header |

---

## References

- STM32H747 I2C HAL Reference — Timeout configuration registers
- Mbed OS I2C API: https://os.mbed.com/docs/mbed-os/latest/apis/i2c.html
- Blues Notecard I2C Protocol: https://dev.blues.io/guides-and-tutorials/notecard-guides/serial-over-i2c/
- Arduino Wire Library Source: https://github.com/arduino/ArduinoCore-mbed
