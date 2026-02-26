# Code Review Recommendations (02/26/2026)

## Executive Summary
The current firmware is already production-grade and demonstrates strong engineering in several critical areas:
- Atomic flash writes with orphaned `.tmp` recovery
- Comprehensive power-state machine with hysteresis
- Watchdog integration with `safeSleep`
- I2C recovery with dual-failure escalation
- Solar-only mode with sunset protocol
- Rate-limited alarms with unload detection
- Remote config, DFU, and serial-log forwarding

The recommendations below prioritize power savings, bug reduction, maintainability, and future extensibility.

---

## Triage Notes (02/26/2026 — Copilot Review)
After cross-referencing every recommendation against the actual codebase (client .ino is 6695 lines, already using ArduinoJson v7, Mbed OS, STM32H747), these are the verdicts:

| # | Recommendation | Verdict | Rationale |
|---|---------------|---------|-----------|
| 1 | Modular split | **DEFER** | High disruption, marginal runtime benefit. Current single-file works fine with Arduino IDE. |
| 2A | Non-blocking pulse sampler | **IMPLEMENT** | Real blocking issue — `delay(1)` loops up to 60s in `SENSOR_PULSE`. Worth fixing. |
| 2B | Aggressive peripheral shutdown | **IGNORE** | Risky — `Wire.end()` kills Notecard I2C comms. Power savings marginal vs. risk of bricking comm path. |
| 2C | Solar sensor-read gate | **ALREADY DONE** | `gSolarOnlySensorsReady` already exists (line 558) and gates sensor access via `isSensorVoltageGateOpen()`. |
| 3A | Fixed JSON capacities | **IGNORE** | Codebase already uses ArduinoJson v7 (`JsonDocument`, not `StaticJsonDocument`). V7 auto-sizes and doesn't support explicit capacities. This recommendation was written for v6 — not applicable. |
| 3B | publishNote stack buffer | **IMPLEMENT** | 1024-byte `char buffer[1024]` on stack (line 5448) is a real concern. Move to `static` to reduce stack pressure. |
| 3C | Serial log memory optimization | **IGNORE** | 5 KB for 30 entries is reasonable for STM32H747 (1 MB SRAM). Not worth the complexity to compress. |
| 4A | Config versioning | **IMPLEMENT** | No schema version exists in `ClientConfig`. Flash migration safety is a real concern if struct changes. |
| 4B | Post-config baseline reset | **IMPLEMENT** | Straightforward fix in `applyConfigUpdate` — reset telemetry baselines and power state on hardware changes. |
| 4C | PROGMEM default config | **IGNORE** | `createDefaultConfig` uses `memset` + field assignments, which is fine. PROGMEM adds complexity for negligible gain on H747. |
| 5 | Sensor driver abstraction | **DEFER** | Nice-to-have but heavy refactor. Current switch-based approach is working and field-tested. |
| 6a | Const-correctness | **IMPLEMENT** | Low-risk, improves safety. Many functions already use `const` refs. |
| 6b | Remote-tunable power thresholds | **IMPLEMENT** | Good operational flexibility. Several power thresholds are compile-time `#define` constants. |
| 6c | Atomic pulse helper note | **IGNORE** | Code comment, not a code change. Already documented implicitly. |
| 6d | Consolidate sensor-fault/stuck | **IGNORE** | Separate alarm types give operators clearer diagnostics. Merging loses information. |
| 6e | Global alarm cap | **IMPLEMENT** | Easy safeguard — one `gGlobalAlarmCount` with hourly cap prevents runaway cellular usage. |
| 6f | Replace String uses | **IMPLEMENT** | ~10 `String` uses in config save and note buffer flush cause heap fragmentation. Replace with fixed buffers. |
| 7a | Compile-time HW summary | **ALREADY DONE** | Startup scan already publishes hardware config in first health packet. |
| 7b | Remote health interval | **IMPLEMENT** | Easy win — make `TANKALARM_HEALTH_TELEMETRY_ENABLED` interval a `ClientConfig` field. |
| 7c | Self-test mode | **DEFER** | Nice feature but low priority vs. bugs and core reliability. |
| 8.1 | Relay timeout semantics | **IMPLEMENT** | Real per-tank vs per-relay mismatch — if relay 0 has 10-min and relay 2 has 60-min timeout, both deactivate at 10 min. Migrate to `gRelayActivationTime[MAX_RELAYS]`. |
| 8.2 | Solar bat-fail cooldown | **IMPLEMENT** | `gSolarOnlyBatFailCount` has no time-based reset — can accumulate false triggers over days. Add 24h decay. |
| 8.3 | Solar state portability | **IMPLEMENT** | `loadSolarStateFromFlash` and `saveSolarStateToFlash` are Mbed-only with no `#else` for LittleFS. Add STM32 path. |
| 8.4 | Startup debounce doc | **IMPLEMENT** | Voltage-based mode has NO upper time bound. At minimum add a max-wait timeout (e.g., 5 minutes), and document the behavior. |

**Summary: 14 IMPLEMENT, 5 IGNORE, 3 DEFER, 2 ALREADY DONE**

---

## 1) Architecture / Maintainability (Highest Impact)
> **VERDICT: DEFER** — The file is 6695 lines, not 4000+. While large, splitting an Arduino `.ino` into modules introduces complications with Arduino IDE build order, forward declarations, and global state sharing. The current single-file approach works well with both Arduino IDE and CLI builds. The common library already provides shared headers (10 files in `TankAlarm-112025-Common/src/`). The ROI doesn't justify the disruption and regression risk for a working production system. Revisit if the file exceeds ~8000 lines or if multiple developers need to work on it concurrently.

### Recommendation
Split the 4000+ line monolithic `.ino` into focused modules.

### Suggested Structure
```text
TankAlarmClient/
├── TankAlarmClient.ino          // setup()/loop() + globals only
├── Config.h / Config.cpp
├── Sensors.h / Sensors.cpp      // readTankSensor, validation, pulse handling
├── NotecardHelpers.h / .cpp     // publishNote, buffer, flush, config poll
├── PowerManagement.h / .cpp
├── SolarOnly.h / .cpp
├── Relays.h / .cpp
├── Alarms.h / .cpp              // evaluateAlarms, unload, battery, solar
├── Storage.h / .cpp             // atomic write helpers
├── Diagnostics.h / .cpp
└── TankAlarm_Common.h           // shared common header
```

### Expected Benefit
- Easier code review and smaller, clearer Git diffs
- Improved AI-assisted suggestions and code navigation
- Better testability and lower coupling

---

## 2) Power & Timing (Critical for Solar/Battery)
### Recommendation A: Make sensor sampling fully non-blocking
> **VERDICT: IMPLEMENT** — Confirmed: `SENSOR_PULSE` has `delay(1)` loops at lines 3519, 3615, 3692 that block for the full sample duration (configurable, can be many seconds). The non-accumulated-mode paths are genuinely blocking. Converting to a `millis()`-based state machine is the right fix. This also improves watchdog behavior during long samples.

Current `SENSOR_PULSE` logic in `readTankSensor` uses blocking `delay(1)` loops (up to 60s), reducing responsiveness and potentially harming sunset detection behavior.

**Action:** Replace with a `PulseSampler` state machine using `millis()` and early returns on incomplete samples.

### Recommendation B: Aggressive peripheral shutdown in low-power states
> **VERDICT: IGNORE** — `Wire.end()` would disable I2C which is required for Notecard communication. The Notecard is the only communication path to the outside world — shutting it down risks missing critical alarm delivery. The `analogReadResolution(10)` saves negligible power. The power-state machine already handles reduced polling intervals. The risk/reward ratio is poor.

```cpp
if (gPowerState >= POWER_STATE_LOW_POWER) {
  Wire.end();                    // I2C off
  analogReadResolution(10);      // lower resolution = faster + less power
  // disable unused timers, ADC, etc. via HAL if needed
}
```

### Recommendation C: Add sensor-read gate in solar-only mode
> **VERDICT: ALREADY DONE** — `gSolarOnlySensorsReady` is declared at line 558 and dynamically managed via `isSensorVoltageGateOpen()`. This is already implemented in v1.1.3.

Add a `gSolarOnlySensorsReady` gate before any `analogRead` or I2C sensor access in `readTankSensor`.

---

## 3) Memory & JSON (Important for STM32H747)
### Recommendation A: Use fixed capacities for JSON documents
> **VERDICT: IGNORE — NOT APPLICABLE** — The codebase uses **ArduinoJson v7**, which removed `StaticJsonDocument` and `DynamicJsonDocument` entirely. All documents are bare `JsonDocument` (e.g., line 3140: `JsonDocument doc;`), many allocated via `std::unique_ptr<JsonDocument>` on the heap. V7 auto-sizes documents and explicit capacity hints are not supported. This recommendation was clearly written for ArduinoJson v6 and does not apply.

Prefer explicit capacities for all documents (per ArduinoJson guidance):
```cpp
StaticJsonDocument<512> doc;        // telemetry/alarm
StaticJsonDocument<2048> dailyDoc;  // daily report
```

### Recommendation B: Reduce stack pressure in `publishNote`
> **VERDICT: IMPLEMENT** — `publishNote()` at line 5448 allocates `char buffer[1024]` on the stack every call. Since `publishNote` is called from multiple code paths (alarm evaluation, telemetry, daily reports), this adds 1 KB of stack pressure to every call chain. Making it `static char buffer[1024]` eliminates this at the cost of 1 KB permanent RAM — acceptable trade on H747 with 1 MB SRAM.

Current 1024-byte stack buffer should move to static storage or controlled heap allocation.

### Recommendation C: Optimize serial log memory path
> **VERDICT: IGNORE** — Buffer is 168 bytes × 30 entries = ~5 KB. On STM32H747 with 1 MB SRAM, this is 0.5% of available memory. The "last 30" ring buffer is a clean, simple design. Compression would add code complexity for negligible memory savings.

`ClientSerialLog` footprint is sizable (160 bytes × 32 ≈ 5 KB). Existing “last 20” behavior is good; optionally compress or retrieve last N only on demand.

---

## 4) Configuration & Persistence
### Recommendation A: Add configuration versioning
> **VERDICT: IMPLEMENT** — Confirmed: `ClientConfig` (line 466) has no schema version field. `loadConfigFromFlash` deserializes JSON and applies fields, which is somewhat resilient to missing keys (they get default/zero values), but there's no way to detect an old schema or trigger a migration. Adding a `uint8_t configSchemaVersion` field with a bump on breaking changes, plus a migration path in `loadConfigFromFlash`, would prevent subtle bugs after firmware updates that add/rename config fields.

Add a version field (`uint32_t` or semantic version string) in `ClientConfig`, and allow server-side rejection of incompatible/old configs.

### Recommendation B: Reset baselines after hardware-affecting config changes
> **VERDICT: IMPLEMENT** — Straightforward safety improvement. When `hardwareChanged` is true in `applyConfigUpdate`, stale telemetry baselines (previous sensor readings, min/max values, etc.) from the old hardware config could cause false alarms or incorrect delta calculations.

In `applyConfigUpdate`, after `hardwareChanged` reinit:
- call `resetTelemetryBaselines()`
- set `gPowerState = POWER_STATE_NORMAL` to force fresh power evaluation

### Recommendation C: Refactor default config initialization
> **VERDICT: IGNORE** — `createDefaultConfig` uses `memset(&cfg, 0, sizeof(ClientConfig))` then straightforward field assignments. This is idiomatic, readable, and costs nothing at runtime since it runs once at boot. `PROGMEM` + `memcpy_P` adds complexity and is designed for flash-constrained AVR boards — the STM32H747 has 2 MB flash and doesn't benefit from this pattern.

`createDefaultConfig` is large; move defaults to a `PROGMEM const` struct and load with `memcpy_P`.

---

## 5) Sensor Reading Layer (Cleanup Opportunity)
> **VERDICT: DEFER** — The virtual dispatch and vtable overhead of polymorphic sensor drivers is overkill for 4 sensor types. The current switch-based approach in `readTankSensor` (lines 3286–3710) is clear, each case is self-contained, and the function has been field-tested. The benefit of adding new sensor types "more easily" is speculative — the current code adds a new sensor type by adding a switch case, which is straightforward. However, extracting each switch branch into a named helper function (e.g., `readDigitalSensor()`, `readAnalogSensor()`) would improve readability without the abstraction overhead. Consider that as a lighter alternative.

### Recommendation
Introduce a driver abstraction to replace the giant `readTankSensor` switch.

```cpp
class SensorDriver {
public:
  virtual float read(uint8_t idx) = 0;
  virtual void begin(const MonitorConfig& cfg) = 0;
};
```

Implementations: `AnalogSensor`, `CurrentLoopSensor`, `PulseSensor`, `DigitalSensor`.

### Expected Benefit
- Cleaner separation of concerns
- Easier onboarding of new sensor types
- Reduced conditional complexity and bug surface area

---

## 6) Code Style & Robustness (High ROI)
- Increase const-correctness (`const MonitorConfig&` where mutation is not needed) — **IMPLEMENT**: low-risk, incremental improvement
- Move remaining power-related magic numbers into remotely tunable `ClientConfig` — **IMPLEMENT**: several `#define` thresholds (e.g., `POWER_STATE_DEBOUNCE_COUNT`, voltage thresholds) should be remote-tunable for field adjustment without firmware updates
- Keep atomic pulse helper approach; add note that Cortex-M7 aligned 32-bit access is atomic but portability rationale remains valid — **IGNORE**: this is a code comment, not a functional change
- Consider consolidating `sensor-fault` and `sensor-stuck` into unified `sensor_issue` + subtype to simplify routing rules — **IGNORE**: keeping them separate gives operators clearer, more actionable diagnostics. A "sensor_issue" generic type would lose information at the alert routing layer
- Add global alarm cap (per hour) in addition to per-tank rate limiting — **IMPLEMENT**: easy safeguard against runaway cellular data usage if multiple tanks fault simultaneously. A single `gGlobalAlarmCount` with hourly reset and a configurable cap (e.g., 20/hour)
- Replace remaining `String` uses (e.g., Mbed retry buffering) with fixed buffers where practical — **IMPLEMENT**: found ~10 `String` uses in config save (lines 2134, 2147) and note buffer flush (lines 5575, 5587-5590, 5648, 5660-5663). Each `substring()` creates new heap allocations. Replace with `strtok`/manual parsing on fixed `char[]` buffers

---

## 7) Testing & Observability
- Include compile-time hardware summary in first health telemetry packet (existing startup scan work is a good base) — **ALREADY DONE**: the existing startup scan already publishes hardware info in the first health packet
- Make `TANKALARM_HEALTH_TELEMETRY_ENABLED` interval remotely configurable — **IMPLEMENT**: easy win, add a `healthIntervalMs` field to `ClientConfig` with the current default, allowing field adjustment without firmware update
- Add self-test mode (special config flag or boot button hold): — **DEFER**: useful for field commissioning but not urgent. Would require careful implementation to avoid accidentally triggering relay cycles on a live production system
  - cycle relays
  - read each sensor 10x
  - publish pass/fail diagnostics

---

## 8) Minor Bugs / Edge Cases
1. **Relay momentary timeout semantics** — **IMPLEMENT**
   - Current logic computes `minDurationMs` across relays but stores activation by tank, not relay.
   - Clarify comments or migrate to `gRelayActivationTime[MAX_RELAYS]` if true per-relay behavior is intended.
   - > *Confirmed*: `gRelayActivationTime[MAX_TANKS]` (line 632) tracks per-tank, but `relayMomentarySeconds[4]` in config is per-relay. In `checkRelayMomentaryTimeout()` (line 6057), the min across all relay durations in the mask is used — so if relay 0 is 10-min and relay 2 is 60-min, both get killed at 10 min. This should be migrated to per-relay tracking.

2. **Solar-only battery fail hysteresis drift** — **IMPLEMENT**
   - `gSolarOnlyBatFailCount` resets only when leaving CRITICAL.
   - Add a 24h cooldown/reset path to avoid long-term false trigger accumulation.
   - > *Confirmed*: counter resets at lines 5248-5249 when exiting CRITICAL, but has no time-based decay. Over days/weeks of borderline battery voltage, the counter can slowly accumulate and eventually trigger a false state transition.

3. **Solar state load portability path** — **IMPLEMENT**
   - `loadSolarStateFromFlash` appears Mbed-only.
   - Add `#else` handling for STM32 LittleFS or centralize in shared helper (`tankalarm_loadSolarState`).
   - > *Confirmed*: both `loadSolarStateFromFlash` (line 4835) and `saveSolarStateToFlash` (line 4863) have `#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)` with **no `#else`**. If compiled for a non-Mbed STM32 target, solar state persistence is silently disabled.

4. **Startup debounce boot-time blocking** — **IMPLEMENT (add safety timeout)**
   - `performStartupDebounce` can block up to 60s in solar-only.
   - Behavior may be acceptable but should be explicitly documented in hardware/startup requirements.
   - > *The recommendation understates the issue*: the voltage-based path (line 4890) has **no upper time bound** — it loops `while (!stable)` with 2-second `safeSleep` intervals, waiting for voltage to stabilize. If voltage never exceeds `startupDebounceVoltage`, it loops forever. Add a max-wait timeout (e.g., 5 minutes) and document the behavior.

---

## Quick Wins (<= 1 hour each)
1. ~~Add `StaticJsonDocument<512>` / `<2048>` declarations consistently~~ — **N/A (ArduinoJson v7)**
2. Extract `readTankSensor` switch branches into focused helper functions — **YES, good incremental cleanup**
3. Convert pulse sampling to non-blocking state machine (largest responsiveness gain) — **YES, highest priority**
4. Move power thresholds into remotely tunable `ClientConfig` — **YES**
5. ~~Split monolith into first 4-5 modules~~ — **DEFERRED (see §1)**

---

## Prioritized Implementation Order (Revised)
1. **Bug fixes first**: Relay timeout per-relay tracking (§8.1), startup debounce safety timeout (§8.4), solar state portability (§8.3), solar bat-fail 24h decay (§8.2)
2. Non-blocking pulse sampler (`readTankSensor` responsiveness/power impact)
3. `publishNote` stack buffer → static; replace `String` uses with fixed buffers
4. Config versioning + post-config baseline/power reset
5. Global alarm cap + remote-tunable power thresholds + health interval
6. Const-correctness pass + extract `readTankSensor` helper functions

---

## Optional Next Deep-Dive Tracks
- ~~Generate exact file-split skeleton and migration order~~ — Deferred
- Rewrite `readTankSensor` + pulse sampler into full state machine — **Recommended next step**
- Focused audit and hardening of solar-only + power-state transitions — **Recommended, especially given §8.2–8.4 findings**
