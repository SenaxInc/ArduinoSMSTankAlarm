# Future Improvement 3.1.1 — Notecard `notecard.begin()` Idempotency Audit

**Priority:** High  
**Effort:** 4–8 hours (code audit + testing)  
**Risk:** Medium — potential memory leak or stale state  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The `notecard.begin(NOTECARD_I2C_ADDRESS)` call is now invoked in **5 distinct code paths** within the Client sketch. The Blues `note-arduino` library creates a `NoteI2c` singleton internally when `begin()` is called. It is unclear whether calling `begin()` multiple times:

1. Leaks memory (allocates a new `NoteI2c` without freeing the old one)
2. Creates stale internal state (e.g., orphaned transaction buffers)
3. Is fully idempotent and safe to call repeatedly

If `begin()` leaks even a small amount of memory per call, the cumulative effect over days/weeks of field operation could cause out-of-memory (OOM) crashes on the STM32H747's constrained heap.

---

## Current Call Sites

| # | Location | File | Line | Trigger Condition |
|---|----------|------|------|-------------------|
| 1 | `initializeNotecard()` | Client .ino | ~2376 | Once at startup in `setup()` |
| 2 | `checkNotecardHealth()` — sustained failure path | Client .ino | ~2453 | When `gNotecardFailureCount == I2C_NOTECARD_RECOVERY_THRESHOLD` (10) |
| 3 | `checkNotecardHealth()` — response failure path | Client .ino | ~2469 | Same threshold, response-null branch |
| 4 | `checkNotecardHealth()` — recovery path | Client .ino | ~2482 | When Notecard recovers (`!gNotecardAvailable` was true) |
| 5 | `reinitializeHardware()` | Client .ino | ~2772 | After config update triggers hardware reinit |

### Worst Case Frequency

- Call site 1: Once per boot
- Call sites 2-3: Every `I2C_NOTECARD_RECOVERY_THRESHOLD` (10) health check failures. Health checks run every 5 minutes when Notecard is down. So at worst, `begin()` is called once per 50 minutes during a Notecard outage.
- Call site 4: Once per Notecard recovery event.
- Call site 5: Once per remote config update.

In a sustained Notecard outage lasting 24 hours: ~29 extra `begin()` calls.

---

## Investigation Plan

### Step 1 — Source Code Audit of `note-arduino` Library

The Blues `note-arduino` library is open source: https://github.com/blues/note-arduino

Key files to audit:

1. **`Notecard.cpp` / `Notecard.h`** — The `Notecard::begin()` method
   - Does it check if `_noteI2c` already exists before allocating?
   - Does it `delete` the old interface before creating a new one?
   - What happens to pending transaction state?

2. **`NoteI2c_Arduino.cpp`** — The I2C interface implementation
   - Constructor and destructor behavior
   - Any internal buffers that need cleanup

3. **`NoteTxn.cpp`** — Transaction handling
   - Is there any state that persists across `begin()` calls?

### Step 2 — Test on Hardware

```cpp
// Test sketch to verify begin() idempotency
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  notecard.begin(NOTECARD_I2C_ADDRESS);
  uint32_t heapBefore = tankalarm_freeRam();
  Serial.print("Heap after first begin(): ");
  Serial.println(heapBefore);
  
  // Call begin() 100 times
  for (int i = 0; i < 100; i++) {
    notecard.begin(NOTECARD_I2C_ADDRESS);
  }
  
  uint32_t heapAfter = tankalarm_freeRam();
  Serial.print("Heap after 100 begin() calls: ");
  Serial.println(heapAfter);
  Serial.print("Heap delta: ");
  Serial.println((int32_t)heapBefore - (int32_t)heapAfter);
  
  // Verify Notecard still responds
  J *req = notecard.newRequest("card.version");
  J *rsp = notecard.requestAndResponse(req);
  if (rsp) {
    Serial.println("Notecard still responsive after 100 begin() calls");
    notecard.deleteResponse(rsp);
  } else {
    Serial.println("ERROR: Notecard not responding!");
  }
}
```

### Step 3 — Long-Duration Soak Test

Run the Client firmware with a simulated Notecard outage (pull I2C wires) for 48 hours. Monitor:
- Heap free bytes via health telemetry (`heap_free`, `heap_min_free`)
- Whether `heap_min_free` trends downward over time
- Whether the device eventually crashes with an OOM or hard fault

---

## Expected Findings

Based on typical Arduino library patterns, likely outcomes:

1. **Best case:** `begin()` checks for existing singleton and is a no-op → no action needed
2. **Likely case:** `begin()` deletes old interface and creates new one → safe but inefficient
3. **Worst case:** `begin()` allocates without checking → memory leak, needs fix

---

## Remediation Options

### Option A — Guard with Flag (Minimal Change)

```cpp
static bool gNotecardInitialized = false;

static void reinitNotecard() {
  if (!gNotecardInitialized) {
    notecard.begin(NOTECARD_I2C_ADDRESS);
    gNotecardInitialized = true;
  }
}
```

**Problem:** Doesn't handle the case where `begin()` is intentionally called to re-establish I2C binding after bus recovery.

### Option B — Explicit Cleanup Before Re-init

```cpp
static void reinitNotecard() {
  // notecard.end() is available in note-arduino v1.6+
  notecard.end();
  notecard.begin(NOTECARD_I2C_ADDRESS);
}
```

**Depends on:** Whether `Notecard::end()` exists in the library version used.

### Option C — Conditional Re-init Only When Needed

Replace all recovery-path `begin()` calls with a helper that only calls `begin()` when the I2C bus was actually recovered:

```cpp
static void ensureNotecardBinding(bool busWasRecovered) {
  if (busWasRecovered) {
    notecard.begin(NOTECARD_I2C_ADDRESS);
  }
}
```

This is the most conservative approach — only re-initializes when the Wire library was restarted by `recoverI2CBus()`.

---

## Acceptance Criteria

- [ ] Blues `note-arduino` source code audited for `begin()` idempotency
- [ ] Heap delta measured after 100+ `begin()` calls on actual hardware
- [ ] 48-hour soak test shows no heap degradation
- [ ] If leak found: remediation applied and re-tested
- [ ] Document findings in code comments at each `begin()` call site

---

## Files Affected

| File | Change |
|------|--------|
| `TankAlarm-112025-Client-BluesOpta.ino` | Potentially add guard/helper around `begin()` calls |
| `TankAlarm-112025-Common/src/TankAlarm_Notecard.h` | Potential shared helper |

---

## References

- Blues note-arduino library: https://github.com/blues/note-arduino
- Blues community forum for `begin()` re-entrancy discussion
- STM32H747XI datasheet — heap/RAM constraints (512KB SRAM)
