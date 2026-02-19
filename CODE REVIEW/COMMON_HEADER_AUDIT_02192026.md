# TankAlarm Common Header Audit — February 19, 2026

## Scope
Audit of `TankAlarm_Common.h` (and sub-headers) vs. the Server, Client, and Viewer `.ino` files to identify:
- Constants that should be centralized in the shared library
- Hardcoded string literals that should use shared defines
- Naming mismatches between Common.h and the .ino files
- Missing notefile definitions
- Structural improvements

---

## A. Missing from Common.h — Constants Defined Locally That Should Be Shared

### A1. Notefile: `CONFIG_ACK_INBOX_FILE` (HIGH priority)

| Item | Detail |
|---|---|
| **Current location** | Server .ino line 156 |
| **Current value** | `"config_ack.qi"` |
| **Why share** | The client sends config acknowledgments via `config.qo` (using `CONFIG_OUTBOX_FILE`), and the server receives them as `config_ack.qi`. But `CONFIG_ACK_INBOX_FILE` is **only defined in the server .ino** — it's not in `TankAlarm_Common.h` alongside all other notefile defines. If a future client needs to reference this name, or if the Viewer needs it, it won't be available. Also breaks the naming pattern of the rest of the notefile constants. |
| **Recommendation** | Add to `TankAlarm_Common.h` in the "Config notefiles" section, right after `CONFIG_INBOX_FILE` / `CONFIG_OUTBOX_FILE`. Also add a matching `CONFIG_ACK_OUTBOX_FILE "config_ack.qo"` for the client side (the Route transforms client `config_ack.qo` → server `config_ack.qi`). |

### A2. `MAX_RELAYS` — Duplicated in Server and Client (MEDIUM)

| Item | Detail |
|---|---|
| **Server** | Line 192: `#define MAX_RELAYS 4` |
| **Client** | Line 606: `#define MAX_RELAYS 4` |
| **Why share** | Both are 4 (Arduino Opta hardware constant). If a different Opta variant is used, both files must be updated independently. |
| **Recommendation** | Move to `TankAlarm_Common.h` or `TankAlarm_Platform.h` (hardware-specific). |

### A3. `CLIENT_SERIAL_BUFFER_SIZE` — Duplicated (MEDIUM)

| Item | Detail |
|---|---|
| **Server** | Line 200: `#define CLIENT_SERIAL_BUFFER_SIZE 50` |
| **Client** | Line 154: `#define CLIENT_SERIAL_BUFFER_SIZE 50` |
| **Why share** | Identical value, same semantic meaning. If one changes, the other should too. |
| **Recommendation** | Centralize in `TankAlarm_Common.h`. |

### A4. `DFU_CHECK_INTERVAL_MS` — Redundantly redefined (LOW)

| Item | Detail |
|---|---|
| **Common.h** | Line 187: `#define DFU_CHECK_INTERVAL_MS (60UL * 60UL * 1000UL)` (i.e., 3600000) |
| **Server** | Line 785: `#define DFU_CHECK_INTERVAL_MS 3600000UL` |
| **Client** | Line 588: `#define DFU_CHECK_INTERVAL_MS 3600000UL` |
| **Issue** | Same value, but Server and Client hardcode the literal while Common.h uses the expression. Because Common.h wraps it in `#ifndef`, the .ino local definitions silently win if they're seen first. This is benign today but confusing — the local defines serve no purpose since the values match. |
| **Recommendation** | Remove the local `#define DFU_CHECK_INTERVAL_MS` from both Server (line 785) and Client (line 588). The `#ifndef` in Common.h already allows override — only keep local defines if an override is actually intended. |

### A5. `SOLAR_OUTBOUND_INTERVAL_MINUTES` — Client-only, not in Common (MEDIUM)

| Item | Detail |
|---|---|
| **Client** | Line 114: `#define SOLAR_OUTBOUND_INTERVAL_MINUTES 360` |
| **Common Config** | `TankAlarm_Config.h` has `SOLAR_INBOUND_INTERVAL_MINUTES` (60) and `GRID_INBOUND_INTERVAL_MINUTES` (10), but **NOT** `SOLAR_OUTBOUND_INTERVAL_MINUTES`. |
| **Why share** | Incomplete set in Common. The inbound intervals are in `TankAlarm_Config.h`, but the outbound interval is missing. A future server or viewer might need to reason about sync timing. |
| **Recommendation** | Add `SOLAR_OUTBOUND_INTERVAL_MINUTES` and `GRID_OUTBOUND_INTERVAL_MINUTES` to `TankAlarm_Config.h` alongside the existing inbound intervals. |

### A6. `MAX_TANKS` — Client-only (LOW)

| Item | Detail |
|---|---|
| **Client** | Line 174: `#define MAX_TANKS 8` |
| **Common** | `MAX_TANK_RECORDS` is 64 in Common.h, but this is the server's fleet-wide record count. `MAX_TANKS` is the per-client limit. |
| **Recommendation** | Consider adding `MAX_TANKS_PER_CLIENT` to `TankAlarm_Common.h` so the server can also reference this limit for validation. Low priority since semantics differ. |

### A7. `VIEWER_SUMMARY_INTERVAL_SECONDS` / `VIEWER_SUMMARY_BASE_HOUR` — Server + Viewer should share

| Item | Detail |
|---|---|
| **Server** | Line 184: `#define VIEWER_SUMMARY_INTERVAL_SECONDS 21600UL` (6h), Line 188: `#define VIEWER_SUMMARY_BASE_HOUR 6` |
| **Viewer** | Line 70: `#define SUMMARY_FETCH_INTERVAL_SECONDS 21600UL`, Line 74: `#define SUMMARY_FETCH_BASE_HOUR 6` |
| **Issue** | Same concept, same value, **different names**. If server changes its summary interval, the viewer won't know. |
| **Recommendation** | Define `VIEWER_SUMMARY_INTERVAL_SECONDS` and `VIEWER_SUMMARY_BASE_HOUR` in `TankAlarm_Common.h`. Have Viewer alias to those. |

### A8. `MAX_NOTES_PER_FILE_PER_POLL` — Server-only but potentially client-relevant (LOW)

| Item | Detail |
|---|---|
| **Server** | Line 252: `#define MAX_NOTES_PER_FILE_PER_POLL 10` |
| **Recommendation** | If client ever does batch notefile reads, this would need to be consistent. Low priority — keep server-only for now. |

---

## B. Hardcoded String Literals — Notefile Names Used as Raw Strings

### B1. Server: `SERIAL_REQUEST_FILE` re-defined with hardcoded string

| File | Line | Hardcoded String | Should Use |
|---|---|---|---|
| Server .ino | 224 | `"serial_request.qi"` | `SERIAL_REQUEST_FILE` (already defined in Common.h line 155) |

**Details**: Common.h already defines `SERIAL_REQUEST_FILE "serial_request.qi"`. The server re-defines it with the same hardcoded string instead of referencing the common define. The `#ifndef` guard means the Common.h definition actually wins (since `#include <TankAlarm_Common.h>` comes first), but the redundant local define with a hardcoded string is confusing and would silently shadow if include order changed.

**Fix**: Remove lines 223-224 from server .ino (the `#ifndef`/`#define` block). The Common.h definition is already correct.

### B2. Server: `LOCATION_REQUEST_FILE` re-defined with hardcoded string

| File | Line | Hardcoded String | Should Use |
|---|---|---|---|
| Server .ino | 232 | `"location_request.qi"` | `LOCATION_REQUEST_FILE` (already in Common.h line 161) |

**Fix**: Same as B1 — remove lines 231-232. Common.h already defines this correctly.

### B3. Server: `CONFIG_ACK_INBOX_FILE` hardcoded (before being centralized)

| File | Line | Hardcoded String | Should Use |
|---|---|---|---|
| Server .ino | 156 | `"config_ack.qi"` | Should be moved to Common.h (see A1) |

---

## C. Mismatches — Inconsistencies Between Components

### C1. Naming mismatch: `DEFAULT_SAMPLE_SECONDS` vs `DEFAULT_SAMPLE_INTERVAL_SEC`

| Component | Define Name | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h` line 39) | `DEFAULT_SAMPLE_INTERVAL_SEC` | `1800` |
| **Client** (.ino line 178) | `DEFAULT_SAMPLE_SECONDS` | `1800` |

**Impact**: Client ignores the Common.h define entirely because it uses a different name. If someone changes `DEFAULT_SAMPLE_INTERVAL_SEC` in Common.h, the client won't be affected.

**Fix**: Client should use `DEFAULT_SAMPLE_INTERVAL_SEC` from Common.h. Replace `DEFAULT_SAMPLE_SECONDS` with `DEFAULT_SAMPLE_INTERVAL_SEC` in client .ino, or add `#define DEFAULT_SAMPLE_SECONDS DEFAULT_SAMPLE_INTERVAL_SEC` as a local alias.

### C2. Naming mismatch: `DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES` vs `DEFAULT_LEVEL_CHANGE_THRESHOLD`

| Component | Define Name | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h` line 45) | `DEFAULT_LEVEL_CHANGE_THRESHOLD` | `0.0f` |
| **Client** (.ino line 182) | `DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES` | `0.0f` |

**Impact**: Same as C1 — the client's local name shadows the common define. Values match today, but divergence is likely if only Common.h is updated.

**Fix**: Align names. Either rename the Common.h define to include "INCHES" for clarity, or have client use the Common.h name.

### C3. `DEFAULT_REPORT_HOUR` / `DEFAULT_REPORT_MINUTE` — Redundant redefinition

| Component | Line(s) | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h`) | Lines 54, 58 | `5`, `0` |
| **Client** (.ino) | Lines 186, 190 | `5`, `0` |

**Impact**: No functional issue (both have `#ifndef` guards, Common.h wins because it's included first). But the client definitions are dead code — they never take effect.

**Fix**: Remove client's `DEFAULT_REPORT_HOUR` and `DEFAULT_REPORT_MINUTE` definitions (lines 185-190). They're already provided by Common.h via `TankAlarm_Config.h`.

### C4. `SOLAR_INBOUND_INTERVAL_MINUTES` — Redundant redefinition

| Component | Line | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h` line 81) | `60` |
| **Client** (.ino line 118) | `60` |

**Impact**: Same as C3 — dead code in client due to `#ifndef` guard.

**Fix**: Remove client lines 117-118.

### C5. `DEFAULT_PRODUCT_UID` — Name collision with different semantics

| Component | Define | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h` line 25) | `DEFAULT_PRODUCT_UID` | `"com.blues.tankalarm:fleet"` |
| **Client** (.ino line 109) | `DEFAULT_PRODUCT_UID` | `"com.senax.tankalarm112025"` |
| **Server** (.ino line 85) | `DEFAULT_SERVER_PRODUCT_UID` | `""` (set via web) |
| **Viewer** (.ino line 46) | `DEFAULT_VIEWER_PRODUCT_UID` | `"com.senax.tankalarm112025:viewer"` |

**Impact**: **The Common.h value is a placeholder** (`"com.blues.tankalarm:fleet"`). The Client's value (`"com.senax.tankalarm112025"`) is the real deployment value. Because Client includes Common.h first AND Common.h uses `#ifndef`, the **Common.h placeholder wins** in the client build — the client's local define on line 109 is **never used**!

**This is a potential deployment bug.** If Common.h is compiled first (which it is, via `#include <TankAlarm_Common.h>` on line 26), the `#ifndef` guard means the Common.h value (`"com.blues.tankalarm:fleet"`) takes precedence, and the client's intended override to `"com.senax.tankalarm112025"` is silently ignored.

**Fix (HIGH PRIORITY)**:
1. Remove `DEFAULT_PRODUCT_UID` from `TankAlarm_Config.h` or change it to a clearly-placeholder value that triggers a compile error if not overridden.
2. Alternatively, have each component define its UID **before** including `<TankAlarm_Common.h>`, which would correctly override via `#ifndef`. But the client currently defines it **after** the include (line 109 vs include on line 26).

### C6. Server daily email hour vs Common report hour

| Component | Define | Value |
|---|---|---|
| **Common** (`TankAlarm_Config.h`) | `DEFAULT_REPORT_HOUR` | `5` |
| **Server** (.ino line 113) | `DAILY_EMAIL_HOUR_DEFAULT` | `6` |

**Note**: These are intentionally different (clients report at 5 AM, server sends email summary at 6 AM). This is **not a bug**, but it should be documented in Common.h that the server email hour is deliberately offset.

---

## D. New Notefiles Not in Common.h

### D1. `config_ack.qi` / `config_ack.qo`

| Direction | Notefile | Used By |
|---|---|---|
| Client → Notehub | `config_ack.qo` (outbox) | Client sends ACK after applying config |
| Notehub → Server | `config_ack.qi` (inbox) | Server receives ACK |

**Status**: `CONFIG_ACK_INBOX_FILE` defined in server .ino only (line 156). Neither `CONFIG_ACK_INBOX_FILE` nor `CONFIG_ACK_OUTBOX_FILE` are in Common.h.

**Note**: The client currently uses `CONFIG_OUTBOX_FILE` (`"config.qo"`) for sending config acknowledgments, per the comment on line 139. However, the server is reading from `config_ack.qi` — this means the Route must transform `config.qo` → `config_ack.qi`, or there's a **notefile name mismatch** where the client sends on `config.qo` but the server expects to receive on `config_ack.qi` (not `config.qi`).

**This warrants verification** — if the Route delivers `config.qo` as `config.qi` (the normal config path), but the server reads `config_ack.qi`, then config ACKs from the client would never reach the server's `handleConfigAck` function. Either:
- The client should send ACKs on a dedicated `config_ack.qo` notefile, or
- The server should read ACKs from `config.qi`

### D2. No notefile missing beyond config_ack

All other notefiles (telemetry, alarm, daily, unload, serial_log, serial_ack, serial_request, location_request, location_response, command, relay, viewer_summary) are properly defined in Common.h.

---

## E. Other Improvements

### E1. Remove dead `#ifndef` redefinitions in Server and Client

The following defines in Server and Client .ino files are **dead code** — they can never take effect because Common.h (included first) already defines them with `#ifndef` guards:

**Server .ino — safe to remove:**
- Lines 223-224: `SERIAL_REQUEST_FILE` (already in Common.h)
- Lines 231-232: `LOCATION_REQUEST_FILE` (already in Common.h)
- Line 785: `DFU_CHECK_INTERVAL_MS` (already in Common.h)

**Client .ino — safe to remove:**
- Lines 117-118: `SOLAR_INBOUND_INTERVAL_MINUTES` (already in TankAlarm_Config.h)
- Lines 185-190: `DEFAULT_REPORT_HOUR`, `DEFAULT_REPORT_MINUTE` (already in TankAlarm_Config.h)
- Line 588: `DFU_CHECK_INTERVAL_MS` (already in Common.h)

### E2. Platform defines duplicated in .ino files

Both Server (lines 68-70) and Client (lines 58-60) locally define:
```cpp
#define POSIX_FS_PREFIX "/fs"
#define FILESYSTEM_AVAILABLE
#define POSIX_FILE_IO_AVAILABLE
```

These are already defined in `TankAlarm_Platform.h` as `TANKALARM_POSIX_FS_PREFIX`, `TANKALARM_FILESYSTEM_AVAILABLE`, `TANKALARM_POSIX_FILE_IO_AVAILABLE`. The local defines use **different names** (without the `TANKALARM_` prefix), so they're not redundant but create a parallel naming convention. 

**Recommendation**: Migrate .ino files to use the `TANKALARM_`-prefixed platform defines from the common library, or add backward-compatible aliases in `TankAlarm_Platform.h`.

### E3. Debug macros should be in Common

The Client defines `DEBUG_BEGIN`, `DEBUG_PRINT`, `DEBUG_PRINTLN`, `DEBUG_PRINTF` macros (lines 87-95). The Server uses `DEBUG_MODE` but doesn't define the same macros. This is inconsistent.

**Recommendation**: Add a `TankAlarm_Debug.h` to the common library with standardized debug macros used by all components.

### E4. Common.h section organization for notefile defines

The notefile defines in Common.h are well-organized but would benefit from one addition after the config section:

```cpp
// --- Config acknowledgment notefiles ---
#ifndef CONFIG_ACK_OUTBOX_FILE
#define CONFIG_ACK_OUTBOX_FILE "config_ack.qo"  // Client sends config ACK
#endif

#ifndef CONFIG_ACK_INBOX_FILE
#define CONFIG_ACK_INBOX_FILE "config_ack.qi"    // Server receives config ACK
#endif
```

### E5. `VIEWER_SUMMARY_FILE` alias pattern — consider standardizing

Both Server and Viewer create local aliases:
- Server: `VIEWER_SUMMARY_FILE` → `VIEWER_SUMMARY_OUTBOX_FILE`
- Viewer: `VIEWER_SUMMARY_FILE` → `VIEWER_SUMMARY_INBOX_FILE`

Same pattern as `TELEMETRY_FILE` → `TELEMETRY_OUTBOX_FILE` (client) and `SERIAL_LOG_FILE` → `SERIAL_LOG_INBOX_FILE` (server).

This alias pattern is useful but should be **documented** — perhaps with a comment block in each .ino explaining the perspective-based aliasing convention.

---

## Summary of Action Items (Priority Order)

| # | Priority | Action | Files to Change |
|---|---|---|---|
| 1 | **HIGH** | Fix `DEFAULT_PRODUCT_UID` collision — client's override is silently ignored (C5) | TankAlarm_Config.h, Client .ino |
| 2 | **HIGH** | Verify config_ack notefile routing — possible client/server mismatch (D1) | Server .ino, Client .ino, Notehub Route |
| 3 | **HIGH** | Add `CONFIG_ACK_OUTBOX_FILE` + `CONFIG_ACK_INBOX_FILE` to Common.h (A1, E4) | TankAlarm_Common.h, Server .ino |
| 4 | **MEDIUM** | Fix naming mismatch `DEFAULT_SAMPLE_SECONDS` → `DEFAULT_SAMPLE_INTERVAL_SEC` (C1) | Client .ino |
| 5 | **MEDIUM** | Fix naming mismatch `DEFAULT_LEVEL_CHANGE_THRESHOLD_INCHES` → align (C2) | Client .ino or TankAlarm_Config.h |
| 6 | **MEDIUM** | Move `MAX_RELAYS` to Common (A2) | TankAlarm_Common.h, Server .ino, Client .ino |
| 7 | **MEDIUM** | Move `CLIENT_SERIAL_BUFFER_SIZE` to Common (A3) | TankAlarm_Common.h, Server .ino, Client .ino |
| 8 | **MEDIUM** | Add `SOLAR_OUTBOUND_INTERVAL_MINUTES` to TankAlarm_Config.h (A5) | TankAlarm_Config.h |
| 9 | **MEDIUM** | Centralize `VIEWER_SUMMARY_INTERVAL_SECONDS` / `VIEWER_SUMMARY_BASE_HOUR` (A7) | TankAlarm_Common.h, Server .ino, Viewer .ino |
| 10 | **LOW** | Remove dead `#ifndef` redefinitions (E1) | Server .ino, Client .ino |
| 11 | **LOW** | Align platform define naming (E2) | Server .ino, Client .ino, TankAlarm_Platform.h |
| 12 | **LOW** | Standardize debug macros across components (E3) | New TankAlarm_Debug.h |
| 13 | **LOW** | Remove redundant `DFU_CHECK_INTERVAL_MS` from Server/Client (A4) | Server .ino, Client .ino |
| 14 | **LOW** | Consider `MAX_TANKS_PER_CLIENT` in Common (A6) | TankAlarm_Common.h |
