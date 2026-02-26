# Future Improvement 3.3.6 — Comprehensive Integration Test Suite

**Priority:** Low  
**Effort:** 16–40 hours (framework + 7+ test scenarios)  
**Risk:** Low — tests don't modify production firmware  
**Prerequisite:** None (but benefits from 3.1.2 shared I2C header and modularization)  
**Date:** 2026-02-26  

---

## Problem Statement

The TankAlarm firmware currently has **no automated test infrastructure**. Testing is performed manually:
- Compile and upload to hardware
- Observe Serial output
- Manually trigger conditions (disconnect devices, vary voltage, etc.)
- Verify behavior visually

This approach has several limitations:

1. **Regression risk** — Changes to the 6,700+ line Client sketch can introduce subtle regressions that manual testing misses
2. **Test coverage gaps** — Edge cases (watchdog recovery, power state transitions, I2C failure modes) are difficult to test manually and tend to be tested once, then never again
3. **No CI/CD** — Without automated tests, there's no gate preventing broken code from being committed
4. **Knowledge loss** — Test procedures live in developers' heads or informal notes. The `POWER_STATE_TEST_COVERAGE.md` document (7 test scenarios) is a step in the right direction but isn't executable

---

## Existing Test Documentation

### `POWER_STATE_TEST_COVERAGE.md` (248 lines)

Defines 7 manual test scenarios:

| Test | Description | Variables |
|------|-------------|-----------|
| T1 | Threshold Edge Oscillation (Hysteresis) | Voltage near ECO boundary |
| T2 | Debounce Transition Correctness | Multi-level degradation/recovery |
| T3 | Transition Log Rate-Limit | 5-minute log suppression |
| T4 | CRITICAL_HIBERNATE Entry Actions | Relay de-energization |
| T5 | Voltage Source Switching | SunSaver/Notecard voltage sources |
| T6 | Battery Failure Fallback (Solar-Only) | Solar-only mode activation |
| T7 | Periodic Power State Logging | 30-minute log interval |

These scenarios need controlled voltage injection, relay state verification, and timing checks — all testable with the right framework.

---

## Proposed Test Architecture

### Two-Tier Test Strategy

```
┌─────────────────────────────────────────────┐
│  Tier 1: Unit Tests (Host Machine)          │
│  - Pure function logic testing              │
│  - No hardware required                     │
│  - Runs in CI/CD pipeline                   │
│  - Tests: math, config parsing, state logic │
└─────────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────────┐
│  Tier 2: HIL Tests (Hardware-in-the-Loop)   │
│  - Actual Arduino Opta hardware             │
│  - I2C Utility as test harness              │
│  - Controlled voltage/sensor injection      │
│  - Tests: I2C, power states, alarms, relay  │
└─────────────────────────────────────────────┘
```

---

## Tier 1: Unit Tests on Host Machine

### Framework: PlatformIO + Unity (or Google Test)

Extract pure functions into testable headers (aligns with MODULARIZATION_DESIGN_NOTE.md) and test them on the host machine without Arduino hardware:

#### Testable Functions (No Hardware Dependencies)

| Function | Module | What to Test |
|----------|--------|-------------|
| `linearMap()` | Sensor | Linear interpolation accuracy, edge cases |
| `roundTo()` | Utils | Rounding precision |
| `computeAlarmLevel()` | Alarm | Threshold evaluation, debounce counting |
| `evaluateUnload()` | Unload | Peak/trough detection, rate calculation |
| `powerStateTransition()` | Power | State machine logic, hysteresis, debounce |
| `checkAlarmRateLimit()` | Alarm | Rate limiting with sliding window |
| `formatDailyReportEntry()` | Daily | JSON structure, payload size limit |
| Config parsing | Config | JSON → struct, default values, validation |

#### Example: Power State Unit Test

```cpp
// test_power_state.cpp
#include <unity.h>
#include "TankAlarm_PowerState.h"  // Extracted from Client .ino (see modularization)

void test_normal_to_eco_requires_debounce() {
  PowerState state = POWER_STATE_NORMAL;
  uint8_t debounce = 0;
  
  // 2 readings below threshold — should NOT transition
  state = evaluatePowerTransition(state, 11.9, &debounce);
  TEST_ASSERT_EQUAL(POWER_STATE_NORMAL, state);
  TEST_ASSERT_EQUAL(1, debounce);
  
  state = evaluatePowerTransition(state, 11.9, &debounce);
  TEST_ASSERT_EQUAL(POWER_STATE_NORMAL, state);
  TEST_ASSERT_EQUAL(2, debounce);
  
  // 3rd reading — should transition
  state = evaluatePowerTransition(state, 11.9, &debounce);
  TEST_ASSERT_EQUAL(POWER_STATE_ECO, state);
  TEST_ASSERT_EQUAL(0, debounce);  // Reset after transition
}

void test_eco_to_normal_requires_hysteresis() {
  PowerState state = POWER_STATE_ECO;
  uint8_t debounce = 0;
  
  // Above ECO entry (12.0) but below ECO exit (12.4) — should stay ECO
  state = evaluatePowerTransition(state, 12.2, &debounce);
  TEST_ASSERT_EQUAL(POWER_STATE_ECO, state);
  
  // Above exit threshold for 3 readings
  for (int i = 0; i < 3; i++) {
    state = evaluatePowerTransition(state, 12.5, &debounce);
  }
  TEST_ASSERT_EQUAL(POWER_STATE_NORMAL, state);
}

void test_recovery_is_one_step_at_a_time() {
  PowerState state = POWER_STATE_CRITICAL_HIBERNATE;
  uint8_t debounce = 0;
  
  // Jump to high voltage — should only recover one step
  for (int i = 0; i < 3; i++) {
    state = evaluatePowerTransition(state, 13.0, &debounce);
  }
  TEST_ASSERT_EQUAL(POWER_STATE_LOW_POWER, state);  // NOT NORMAL
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normal_to_eco_requires_debounce);
  RUN_TEST(test_eco_to_normal_requires_hysteresis);
  RUN_TEST(test_recovery_is_one_step_at_a_time);
  return UNITY_END();
}
```

### PlatformIO Configuration

```ini
; platformio.ini
[env:native_test]
platform = native
test_framework = unity
build_flags = 
  -DUNITY_INCLUDE_DOUBLE
  -DPLATFORMIO_UNIT_TEST
test_build_src = yes
```

### CI/CD Integration

```yaml
# .github/workflows/test.yml
name: Unit Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - run: pip install platformio
      - run: pio test -e native_test
```

---

## Tier 2: Hardware-in-the-Loop (HIL) Tests

### Architecture

```
┌─────────────────┐     I2C      ┌─────────────────┐
│  Opta #1        │◄────────────►│  Blues Notecard  │
│  (Device Under  │     0x17     │                  │
│   Test - DUT)   │              └─────────────────┘
│                 │     I2C      ┌─────────────────┐
│                 │◄────────────►│  A0602 Expansion│
│                 │     0x64     │  (4-20mA inputs) │
└─────┬───────────┘              └─────────────────┘
      │ USB Serial
      │ (test commands + results)
      ↓
┌─────────────────┐
│  Host PC        │
│  (Test Runner)  │
│  Python script  │
│                 │
└─────────────────┘
```

### I2C Utility as Test Harness

The existing I2C Utility sketch can be extended to serve as a test harness:

1. **Accept test commands via Serial** — e.g., `TEST T1`, `TEST T4`
2. **Execute test sequence** — inject conditions, observe behavior
3. **Report results** — `PASS` / `FAIL` with details
4. **Upload results via Notecard** — optional, for remote CI

### Example: Test Command Protocol

```
Serial Input:                     Serial Output:
TEST START T1                     TEST T1 STARTED
                                  STEP 1: Set voltage 12.1V — NORMAL (OK)
                                  STEP 2: Drop to 11.99V — debounce 1 (OK)
                                  STEP 3: 2 readings at 11.99V — no transition (OK)
                                  STEP 4: Raise to 12.01V — debounce reset (OK)
                                  STEP 5: 3 readings at 11.99V — ECO transition (OK)
                                  STEP 6: Raise to 12.1V — still ECO (OK)
                                  STEP 7: Raise to 12.41V × 3 — NORMAL (OK)
                                  TEST T1 PASSED (7/7 steps)
```

### Voltage Injection

For power state tests, voltage must be controlled programmatically. Options:

| Method | Hardware | Cost | Complexity |
|--------|----------|------|-----------|
| DAC output to voltage divider | MCP4725 I2C DAC + op-amp | $15 | Low |
| Bench PSU with SCPI | Keysight/Rigol via USB/LAN | $200+ | Medium |
| Override `getEffectiveBatteryVoltage()` | Software mock (test mode) | $0 | Low |
| potentiometer with servo motor | Servo + potentiometer | $10 | Medium |

**Recommendation for initial implementation:** Software mock. Add a test mode that injects voltage values via Serial commands:

```cpp
#ifdef TANKALARM_TEST_MODE
static float gInjectedVoltage = -1.0f;  // -1 = use real reading

static float getEffectiveBatteryVoltage() {
  if (gInjectedVoltage >= 0) return gInjectedVoltage;
  // ... real implementation ...
}

// Serial command handler:
// "INJECT VOLTAGE 11.9" sets gInjectedVoltage = 11.9
#endif
```

### I2C Failure Injection

For I2C reliability tests:

```cpp
#ifdef TANKALARM_TEST_MODE
static bool gI2cFailureInjected = false;

// In readCurrentLoopMilliamps():
if (gI2cFailureInjected) {
  gCurrentLoopI2cErrors++;
  return -1.0f;  // Simulate I2C failure
}

// Serial command: "INJECT I2C_FAIL ON" / "INJECT I2C_FAIL OFF"
#endif
```

---

## Test Scenarios

### Core Test Cases

| ID | Category | Description | Tier |
|----|----------|-------------|------|
| T1–T7 | Power State | From POWER_STATE_TEST_COVERAGE.md | HIL/Unit |
| T8 | I2C Recovery | Bus recovery after simulated hang | HIL |
| T9 | I2C Backoff | Exponential backoff for sensor-only recovery | Unit/HIL |
| T10 | Startup Scan | Device found / missing / retry | HIL |
| T11 | Error Alert | Daily I2C error count exceeds threshold | Unit |
| T12 | Notecard Health | Health check backoff during outage | HIL |
| T13 | Alarm Debounce | High/low/clear alarm transitions | Unit |
| T14 | Rate Limiting | Alarm rate limiting correctness | Unit |
| T15 | Config Update | Remote config applied correctly | HIL |
| T16 | Daily Report | Multi-part report generation | Unit |
| T17 | Note Buffering | Buffer/flush/prune during offline | HIL |
| T18 | DFU Guard | I2C recovery blocked during DFU | HIL |
| T19 | Watchdog | Recovery from simulated hang | HIL |
| T20 | Solar-Only | Sunset protocol, boot debounce | HIL |

### Estimated Test Development Time

| Test Group | Count | Effort per Test | Total |
|-----------|-------|----------------|-------|
| Power State (T1-T7) | 7 | 1–2 hours | 7–14h |
| I2C Reliability (T8-T12) | 5 | 1–2 hours | 5–10h |
| Alarm/Relay (T13-T14) | 2 | 1 hour | 2h |
| Communication (T15-T17) | 3 | 2 hours | 6h |
| System (T18-T20) | 3 | 2 hours | 6h |
| **Total** | **20** | | **26–38h** |

---

## Python Test Runner

```python
#!/usr/bin/env python3
"""HIL test runner for TankAlarm firmware."""

import serial
import time
import sys

class TankAlarmTestRunner:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=5)
        self.results = []
    
    def send_command(self, cmd):
        self.ser.write(f"{cmd}\n".encode())
        time.sleep(0.1)
    
    def wait_for(self, expected, timeout=30):
        start = time.time()
        buffer = ""
        while time.time() - start < timeout:
            data = self.ser.readline().decode().strip()
            if data:
                print(f"  << {data}")
                buffer += data + "\n"
                if expected in data:
                    return True
        return False
    
    def run_test(self, test_id):
        print(f"\n{'='*40}")
        print(f"Running {test_id}")
        print(f"{'='*40}")
        
        self.send_command(f"TEST START {test_id}")
        passed = self.wait_for(f"TEST {test_id} PASSED", timeout=120)
        
        result = "PASS" if passed else "FAIL"
        self.results.append((test_id, result))
        print(f"Result: {result}")
        return passed
    
    def run_all(self):
        tests = ["T1", "T2", "T3", "T4", "T5", "T6", "T7",
                 "T8", "T9", "T10", "T11", "T12"]
        
        for t in tests:
            self.run_test(t)
        
        print(f"\n{'='*40}")
        print("SUMMARY")
        print(f"{'='*40}")
        passed = sum(1 for _, r in self.results if r == "PASS")
        total = len(self.results)
        for test_id, result in self.results:
            icon = "✓" if result == "PASS" else "✗"
            print(f"  {icon} {test_id}: {result}")
        print(f"\n{passed}/{total} tests passed")
        
        return passed == total

if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    runner = TankAlarmTestRunner(port)
    success = runner.run_all()
    sys.exit(0 if success else 1)
```

---

## Reporting via Notecard

Optionally publish test results to Notehub for tracking:

```cpp
// After all tests complete on the Opta:
void publishTestResults(uint8_t passed, uint8_t total) {
  JsonDocument doc;
  doc["ev"] = "test-results";
  doc["passed"] = passed;
  doc["total"] = total;
  doc["fw"] = FIRMWARE_VERSION;
  doc["t"] = currentEpoch();
  publishNote(DIAG_FILE, doc, true);
}
```

This enables tracking test results across firmware versions and hardware units.

---

## Directory Structure

```
tests/
├── unit/
│   ├── platformio.ini
│   ├── test_power_state.cpp
│   ├── test_alarm_logic.cpp
│   ├── test_linear_map.cpp
│   ├── test_rate_limit.cpp
│   └── test_daily_report.cpp
├── hil/
│   ├── test_runner.py
│   ├── test_i2c_recovery.py
│   ├── test_power_states.py
│   └── test_notecard_health.py
└── README.md
```

---

## Files Affected

| File | Change |
|------|--------|
| `tests/` (new directory) | Test framework, ~15 files |
| Client .ino | `#ifdef TANKALARM_TEST_MODE` sections (~50 lines) |
| `.github/workflows/test.yml` (new) | CI configuration |
| `platformio.ini` (new) | PlatformIO project for host tests |
| Extracted modules (per MODULARIZATION_DESIGN_NOTE.md) | Testable headers |

---

## Implementation Priority

1. **Start with Tier 1 unit tests** — no hardware required, can run in CI
2. **Extract `updatePowerState()` first** — most documented test coverage (T1-T7)
3. **Add Tier 2 HIL tests incrementally** — one test per I2C improvement
4. **Add CI/CD pipeline last** — after unit test suite is stable

This aligns with the modularization plan: extracting modules for testability provides both better code organization AND automated test coverage.
