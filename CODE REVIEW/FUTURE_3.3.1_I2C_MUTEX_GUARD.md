# Future Improvement 3.3.1 — I2C Mutex / Guard for Future Multi-threaded Use

**Priority:** Low  
**Effort:** 2–4 hours  
**Risk:** Very Low — no behavioral change when single-threaded  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The Arduino Opta is powered by the STM32H747XI, a dual-core processor with:
- **Cortex-M7** (480 MHz) — runs the main Arduino sketch
- **Cortex-M4** (240 MHz) — available for real-time tasks via the Arduino M4 core

Currently, all firmware runs on the M7 core only. However, future firmware revisions may utilize the M4 core for:
- Real-time sensor sampling with precise timing
- Background I2C communication while the M7 handles web serving (Server sketch)
- Dedicated power management or watchdog supervision

If both cores access the I2C bus simultaneously, **bus collisions** will occur — corrupted transactions, NACK errors, stuck bus conditions, or (worst case) electrical contention on SDA/SCL lines.

---

## Current I2C Access Pattern

All I2C bus access is currently serialized in the main `loop()`:

```
loop() [M7 core, single-threaded]
  ├── checkNotecardHealth()        → Wire I2C to Notecard (0x17)
  ├── sampleTanks()
  │     └── readCurrentLoopMilliamps() → Wire I2C to A0602 (0x64)
  ├── sendDailyReport()            → Wire I2C to Notecard (0x17)
  ├── sendHealthTelemetry()        → Wire I2C to Notecard (0x17)
  └── recoverI2CBus()              → Wire GPIO bit-bang (SCL/SDA)
```

Even on the single M7 core, Mbed OS uses RTOS threads internally. The `Wire` library's I2C transactions are not atomic from the RTOS perspective — a thread context switch during a transaction could theoretically interleave with another thread's I2C access if the library is used from multiple threads. In practice, Arduino sketches run in a single thread, but libraries using callbacks or Mbed RTOS threads could break this assumption.

---

## Proposed Solution: RAII I2C Guard

### Design Pattern

An RAII (Resource Acquisition Is Initialization) wrapper that acquires a mutex on construction and releases it on destruction. When compiled for single-threaded use, the guard is a no-op with zero runtime overhead.

### Header: `TankAlarm_I2CGuard.h`

```cpp
#ifndef TANKALARM_I2C_GUARD_H
#define TANKALARM_I2C_GUARD_H

// Enable the actual mutex when dual-core or multi-threaded use is active
// #define TANKALARM_I2C_MULTITHREAD

#ifdef TANKALARM_I2C_MULTITHREAD

#include <mbed.h>
#include <rtos.h>

class I2CGuard {
public:
  I2CGuard() { _mutex.lock(); }
  ~I2CGuard() { _mutex.unlock(); }
  
  // Non-copyable, non-movable
  I2CGuard(const I2CGuard&) = delete;
  I2CGuard& operator=(const I2CGuard&) = delete;

private:
  static rtos::Mutex _mutex;
};

// Define in exactly one translation unit
// rtos::Mutex I2CGuard::_mutex;

#else  // Single-threaded — no-op guard

class I2CGuard {
public:
  I2CGuard() {}
  ~I2CGuard() {}
  I2CGuard(const I2CGuard&) = delete;
  I2CGuard& operator=(const I2CGuard&) = delete;
};

#endif // TANKALARM_I2C_MULTITHREAD

#endif // TANKALARM_I2C_GUARD_H
```

### Usage Pattern

```cpp
static float readCurrentLoopMilliamps(int16_t channel) {
  I2CGuard guard;  // Acquires mutex (or no-op in single-threaded mode)
  
  // ... all Wire operations are now protected ...
  Wire.beginTransmission(addr);
  Wire.write(channel);
  Wire.endTransmission(false);
  Wire.requestFrom(addr, 2);
  // ... etc ...
  
  return value;
}  // guard destructor releases mutex

static void recoverI2CBus() {
  I2CGuard guard;  // Prevents other threads from using Wire during recovery
  
  Wire.end();
  // ... SCL toggle ...
  Wire.begin();
}
```

### Compiler Optimization

When `TANKALARM_I2C_MULTITHREAD` is not defined, the no-op `I2CGuard` compiles to zero instructions — the constructor and destructor are empty inline functions that the compiler eliminates entirely. There is no runtime cost.

---

## Mbed OS Mutex Considerations

### Priority Inheritance

Mbed OS `rtos::Mutex` supports priority inheritance — if a high-priority thread blocks on the mutex held by a low-priority thread, the low-priority thread temporarily runs at the high-priority level. This prevents priority inversion issues common in embedded RTOS systems.

### Timeout

The default `lock()` blocks indefinitely. For safety, consider a timeout:

```cpp
I2CGuard() {
  if (!_mutex.trylock_for(std::chrono::milliseconds(1000))) {
    // Timeout — log error, don't proceed with I2C
    Serial.println(F("I2C mutex timeout!"));
    _acquired = false;
    return;
  }
  _acquired = true;
}

~I2CGuard() {
  if (_acquired) _mutex.unlock();
}
```

### ISR Safety

`rtos::Mutex` cannot be used from interrupt service routines (ISRs). If I2C access is ever needed from an ISR (unlikely but possible for sensor interrupts), use `rtos::Semaphore` instead, which supports `release()` from ISR context.

---

## Functions That Need Guarding

| Function | I2C Access | Guard Needed |
|----------|-----------|-------------|
| `readCurrentLoopMilliamps()` | Read from A0602 (0x64) | Yes |
| `recoverI2CBus()` | GPIO bit-bang on SCL/SDA, `Wire.begin()` | Yes |
| `checkNotecardHealth()` | Uses `notecard.*` which uses Wire internally | Yes* |
| `publishNote()` | Uses `notecard.*` which uses Wire internally | Yes* |
| `syncTimeFromNotecard()` | Uses `notecard.*` | Yes* |
| All other `notecard.*` calls | Via Blues library | Yes* |

*Note: The Blues `note-arduino` library wraps all I2C access through its `NoteI2c` class. Ideally, the guard would be inside the library or around each `notecard.*` call. Wrapping at the caller level is safer but more invasive.

---

## Alternative: Wire Library-Level Mutex

Instead of an application-level guard, patch the `Wire` library itself:

```cpp
// In a Wire wrapper:
TwoWire& getGuardedWire() {
  static rtos::Mutex wireMutex;
  wireMutex.lock();
  return Wire;
}
```

**Disadvantage:** This requires modifying the Arduino core library or creating a wrapper class, which is more complex and harder to maintain across board support package updates.

**Recommendation:** Application-level RAII guards are simpler and sufficient for this firmware's architecture.

---

## When to Implement

**Implement when:**
- Dual-core (M7 + M4) firmware is being designed
- Mbed RTOS threads are introduced for any purpose (e.g., background data processing)
- The Server sketch needs concurrent HTTP handling and Notecard communication

**Do not implement until needed** — the no-op guard adds minimal value as documentation but introduces a new header file and #include dependency. The actual mutex logic is straightforward to add when multi-threaded access is confirmed.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| No-op mode compilation | Compile without `TANKALARM_I2C_MULTITHREAD` | No warnings, no code size increase |
| Mutex mode compilation | Compile with `TANKALARM_I2C_MULTITHREAD` | Compiles on Mbed OS target |
| Single-thread performance | Run normal firmware with guards | No measurable performance difference |
| Multi-thread safety | Spawn RTOS thread accessing Wire | No bus collisions or NACK errors |
| Mutex timeout | Simulate held mutex, attempt lock | Timeout fires, error logged |

---

## Files Affected

| File | Change |
|------|--------|
| `TankAlarm_I2CGuard.h` (new) | ~40 lines — RAII guard class |
| Client .ino | Add `#include`, add `I2CGuard guard;` to ~6 functions |
| `TankAlarm_Common.h` | Add `#include "TankAlarm_I2CGuard.h"` |

---

## References

- STM32H747XI Reference Manual — Dual-core architecture, inter-processor communication
- Mbed OS RTOS API: https://os.mbed.com/docs/mbed-os/latest/apis/rtos.html
- Arduino Opta dual-core examples: https://docs.arduino.cc/tutorials/opta/
