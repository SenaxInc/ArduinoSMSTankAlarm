# Issue #247 Review — February 26, 2026

**Issue:** "Refactor & Optimize `TankAlarmClient.ino` for v1.2.0 – Safety, RAM/Flash Savings, Maintainability & Bulletproof Power Management"
**Author:** dorkmo
**Reviewer:** Claude
**Status:** Open
**Prior Review Context:** CODE_REVIEW_02192026_COMPREHENSIVE_CLAUDE, CODE_REVIEW_FIX_IMPLEMENTATION_02202026

---

## Overview

Issue #247 proposes six categories of changes for a v1.2.0 release. Several proposals overlap with work already completed in the Feb 20 fix implementation, contain factual inaccuracies about the current codebase state, or have ordering concerns. Each category is evaluated below against the current source.

**Note:** The issue states the sketch is "2,800+ lines." The current `TankAlarm-112025-Client-BluesOpta.ino` is **6,347 lines**. All analysis below is based on the actual current file.

---

## Category-by-Category Evaluation

---

### Category 1: Quick Wins

#### 1a. Add `freeRam()` helper with diagnostic output at end of `setup()`
**Verdict: IMPLEMENT**

Free heap monitoring is absent from both the client and server. The Feb 19 comprehensive review explicitly flagged this as Architecture Note #2: "Neither the server nor client logs or checks free heap. On a 1MB SRAM device with frequent malloc/free and String operations, fragmentation-related crashes could develop over weeks of continuous operation with no diagnostic telemetry."

A `freeRam()` call at the end of `setup()` costs nothing at runtime and would surface memory pressure issues during field testing and debugging. This directly fills a known gap.

#### 1b. Convert remaining `Serial.print("…")` to `Serial.print(F("…"))`
**Verdict: ALREADY DONE — No work needed**

A search of the current sketch confirms `F()` macros are used throughout all `Serial.print` and `Serial.println` calls. There are no remaining bare string literals in serial output calls. This was already addressed in prior development. No action required.

#### 1c. Replace `strcpy` with `strlcpy`
**Verdict: ALREADY DONE — No work needed**

The Feb 19 comprehensive review (Architecture Note #4) explicitly confirmed: "Consistent use of `strlcpy()` throughout with a polyfill for non-Mbed platforms. No `strcpy()` or `sprintf()` without size bounds." No `strcpy` calls exist in the current sketch. No action required.

#### 1d. Insert `gConfigDirty = true` after runtime solar-only configuration toggles
**Verdict: NOT NEEDED — Already handled**

`applyConfigUpdate()` unconditionally sets `gConfigDirty = true` at line 2879, after all field updates including the `solarOnlyConfig` block (lines 2656–2687). Every successful call to `applyConfigUpdate()` will mark the config dirty regardless of which specific fields were updated. Adding field-level dirty flags inside the `solarOnlyConfig` block would be redundant. No action required.

---

### Category 2: JSON Document Sizing (StaticJsonDocument vs DynamicJsonDocument)

**Verdict: DO NOT IMPLEMENT — This is a regression**

The proposal advocates using `StaticJsonDocument<512>` for small note types and `DynamicJsonDocument` with a 2048-byte cap for daily reports. However, the Feb 20 fix implementation (M1) **already completed a full migration** away from both of these v6 API types. All 17 `DynamicJsonDocument` and all 11 `StaticJsonDocument` instances in the server were migrated to ArduinoJson v7's `JsonDocument` (auto-sizing). The client follows the same pattern.

Re-introducing `StaticJsonDocument<512>` would:
1. **Revert completed work** — the v6/v7 mixing problem (M1 from Feb 19 review) was fully resolved
2. **Re-introduce silent data truncation risk** — `StaticJsonDocument` with an undersized capacity silently drops fields; v7's `JsonDocument` auto-sizes to prevent this
3. **Require re-testing all affected paths** — every note type would need capacity validation across all real-world payloads

The v7 `JsonDocument` approach that is already in place is the correct long-term direction. The flash/RAM savings from stack-allocated documents are real but minor compared to the reliability regression. **Skip this category entirely.**

---

### Category 3: Safe Sleep Helper Function

**Verdict: IMPLEMENT — Addresses a real gap**

The proposal to extract a `safeSleep(unsigned long ms)` helper is well-motivated. The current state of the codebase has:

1. **One correct chunked implementation** (lines 1182–1192) — the main loop sleep function kicks the watchdog in `(WATCHDOG_TIMEOUT_SECONDS * 1000UL) / 2` chunks
2. **Two bare `rtos::ThisThread::sleep_for()` calls** (lines 4696 and 4723) — these do **not** kick the watchdog, creating a window where a sleep longer than `WATCHDOG_TIMEOUT_SECONDS` (30 seconds default) could trigger an unintentional reset

Wrapping all delay/sleep paths in a single `safeSleep()` function that handles chunking and watchdog kicking would:
- Fix the two unprotected sleep calls
- Enforce consistent watchdog safety across all sleep sites
- Make future sleep additions safe by default

This is a targeted, low-risk improvement. Priority: **implement before v1.2.0 if any sleep durations at lines 4696/4723 could exceed the watchdog timeout.**

---

### Category 4: Power-State Machine Refinements

**Verdict: PARTIAL — Rate-limited logging yes, hysteresis streamlining no**

The Feb 19 review praised the power-state machine as "well-engineered." Modifications here carry more risk than benefit unless specifically addressing a known problem.

**Rate-limited state transition logging: IMPLEMENT** — Adding rate limiting to state transition log messages prevents log flooding during voltage oscillation near threshold boundaries. This is additive and low-risk.

**Streamlining hysteresis logic: DEFER** — The current 4-state system (NORMAL → ECO → LOW_POWER → CRITICAL_HIBERNATE) with its hysteresis and debounce logic is tested and working across four hardware configurations. Refactoring tested power management logic without a specific bug to fix introduces unnecessary risk.

**Automatic relay de-energization on CRITICAL_HIBERNATE: VERIFY FIRST** — This behavior change could have unintended consequences for alarm relay wiring. The current behavior (whatever it is) should be documented before adding automatic de-energization. Do not implement without confirming whether leaving relays energized during hibernate is intentional.

---

### Category 5: Modular File Structure

**Verdict: DEFER — Document natural boundaries now, implement later**

The Feb 19 review (Architecture Note #1) flagged the monolithic file sizes as an area for improvement. At 6,347 lines, the client sketch has grown substantially. The proposed modules (Config, Sensors, PowerManager, NotecardManager, SolarOnly) map to real logical boundaries in the code.

However, splitting an Arduino Mbed sketch into multiple `.h`/`.cpp` files in the same folder is non-trivial:
- The Arduino build system has different include resolution behavior for files in the sketch folder vs. library folders
- Global variable declarations shared across files require careful `extern` management
- The Mbed platform's LTO and compilation order behavior can introduce hard-to-reproduce bugs

**Recommended approach:** Document the natural module boundaries in a separate architecture note now (which files would go where, what globals each module needs). Use that as a map for the actual split in a future release with dedicated integration testing time. Do not attempt this as part of v1.2.0 alongside other changes.

---

### Category 6: Minor Optimizations

#### 6a. Mark conversion helpers `constexpr`
**Verdict: IMPLEMENT** — Zero-risk, small flash savings, improves compile-time safety for functions that are already pure calculations.

#### 6b. Remove unnecessary zero-initializations
**Verdict: LOW PRIORITY** — The compiler optimizes away redundant zero-initialization in global/static contexts. This change would reduce source clutter marginally but has no measurable runtime impact and carries a small risk of accidentally removing an initialization that was actually functional. Skip unless found during a targeted readability pass.

#### 6c. Add `DEBUG_PRINTLN` macros for field debugging
**Verdict: CONDITIONAL** — Only add if there is a project-level convention for a `DEBUG` compile flag and a plan for what output level the macros produce. Adding macros that compile out unconditionally (because `DEBUG` is never defined) adds dead code. If there is already a `DEBUG` or `VERBOSE_LOGGING` flag in the codebase, extending it here is reasonable.

---

## Summary and Recommended Scope for v1.2.0

| Category | Proposal | Recommendation |
|----------|----------|---------------|
| 1a | `freeRam()` diagnostic helper | **Implement** |
| 1b | `F()` macro conversions | Skip — already done |
| 1c | `strlcpy` replacements | Skip — already done |
| 1d | `gConfigDirty` after solar toggle | Skip — unconditionally set at line 2879 |
| 2 | JSON StaticJsonDocument sizing | **Do not implement** — regression to v6 API |
| 3 | `safeSleep()` helper | **Implement** — fixes unguarded sleep calls at lines 4696/4723 |
| 4 (partial) | Rate-limited state transition logging | **Implement** |
| 4 (partial) | Hysteresis streamlining | Defer |
| 4 (partial) | Auto relay de-energize on hibernate | Verify before implementing |
| 5 | Modular file split | **Defer** — document module map now, implement separately |
| 6a | `constexpr` conversion helpers | **Implement** |
| 6b | Remove redundant zero-inits | Skip |
| 6c | `DEBUG_PRINTLN` macros | Conditional on existing debug flag |

### Suggested v1.2.0 Scope

The three highest-value, lowest-risk items from this issue are:

1. **`freeRam()` at end of `setup()`** — fills a documented monitoring gap, zero runtime cost
2. **`safeSleep()` helper** — fixes two watchdog-unprotected sleep calls and prevents future regression
3. **`constexpr` conversion helpers + rate-limited power state logging** — clean, low-risk improvements

The JSON re-sizing proposal should be explicitly closed as "not applicable" given the v7 migration already completed. The modular split should be tracked separately as a v1.3.0 or later architectural item.

---

## Open Items from Prior Reviews Relevant to This Issue

The Feb 19/20 review cycle left one High-severity item deferred that overlaps with this issue's themes:

- **H4 (Non-Atomic File Writes)** — `saveConfig()`, `saveContactsConfig()`, and other save functions use `fopen("w")` which truncates immediately. Power loss during write corrupts the file. The write-to-tmp + `rename()` fix was deferred in Feb 20. If v1.2.0 is touching persistence code, this should be included.

---

*Review completed February 26, 2026. Recommended next action: confirm with issue author that the JSON sizing proposal is superseded by the v7 migration, then scope v1.2.0 to the three high-value items above plus H4 atomic writes.*
