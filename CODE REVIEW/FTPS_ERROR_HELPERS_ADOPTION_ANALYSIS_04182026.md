# FTPS Error Helpers Adoption Analysis for TankAlarm
**Date:** April 18, 2026  
**Repository:** ArduinoSMSTankAlarm  
**Subject:** Implementation analysis of adopting `ftpsIsTransferRetriable()` and `lastNsapiError()` helpers

---

## Executive Summary

TankAlarm **already adopts** `ftpsIsSessionDead()` and `ftpsIsTransferRetriable()` from the ArduinoOPTA-FTPS library in its per-file retry logic (lines 6427–6535 in Server-BluesOpta.ino). This analysis evaluates:

1. **Current adoption status** — What's already implemented and working
2. **Remaining optimization opportunities** — Areas to reduce code duplication
3. **Risk assessment** — Compatibility and validation requirements
4. **Recommended next steps** — Phased approach for cleanup and enhancement

---

## Current Adoption Status

### ✅ Already Implemented

#### 1. Library-Provided Helpers in Use
- **`ftpsIsSessionDead(FtpsError err)`** — Used at lines 6443 and 6519
  - Correctly identifies terminal control-channel failures (ConnectionFailed, ProtPRejected, LoginRejected, etc.)
  - Causes batch abort when session is no longer viable
  
- **`ftpsIsTransferRetriable(FtpsError err, int nsapiCode)`** — Used at lines 6444 and 6524
  - Correctly passes `lastNsapiError()` from `gFtpsClient` as the nsapiCode
  - Distinguishes between retriable transient faults (DataConnectionFailed with -3005, DataTlsHandshakeFailed) and permanent failures
  - Enables per-file retry with stepped backoff (20s, 40s)

#### 2. Error Classification Integration
```cpp
// Line 6427: Captured at point of failure
int lastNsapi = gFtpsClient.lastNsapiError();

// Line 6444: Used in retry decision
if (!ftpsIsTransferRetriable(lastErr, lastNsapi)) {
  break;  // Skip file but keep batch
}

// Line 6524: Used in circuit-breaker logic
if (attempts >= FTP_BACKUP_PER_FILE_MAX_ATTEMPTS &&
    ftpsIsTransferRetriable(lastErr, gFtpsClient.lastNsapiError())) {
  // ...evaluate consecutive failures
}
```

#### 3. Per-File Retry Strategy (Documented in PER_FILE_RETRY_PLAN_04172026.md Phase 0)
- **Scope:** Only FTPS uploads (`useFtps && ftpsStoreBuffer()`)
- **Max attempts:** `FTP_BACKUP_PER_FILE_MAX_ATTEMPTS` (2–3 typically)
- **Backoff:** 20s (attempt 1→2), then 40s (attempt 2→3)
- **Abort conditions:**
  - Session dead → entire batch aborted
  - Non-retriable error → skip file, continue batch
  - Retriable error + exhausted attempts → check circuit-breaker
  
#### 4. Test Infrastructure (Lines 6429–6433)
- `FTP_BACKUP_TEST_FORCE_ONE_RETRY` — Inject retriable failure for testing
- `FTP_BACKUP_TEST_FORCE_ONE_RETRY_TARGET` — Target specific file to force retry

---

## Code Duplication Identified

### Issue: Local `ftpsSessionLikelyDead()` Shadow

**Current state (lines 5512–5522):**
```cpp
static bool ftpsSessionLikelyDead(FtpsError error) {
  switch (error) {
    case FtpsError::ConnectionFailed:
    case FtpsError::PassiveModeRejected:
    case FtpsError::DataConnectionFailed:
    case FtpsError::FinalReplyFailed:
      return true;
    default:
      return false;
  }
}
```

**Issue:**
- Local function has **non-overlapping classification** vs. library `ftpsIsSessionDead()`
- TankAlarm's version includes `PassiveModeRejected`, `FinalReplyFailed` (not in library)
- Library version includes `LoginRejected`, `ProtPRejected`, `ControlTlsHandshakeFailed` (not in local)
- Used only once (line 6477) for a post-STOR verification workaround
- **Line 6477 context:** Checks if control channel is likely dead after STOR; if so, attempts reconnect + SIZE verification

**Why this matters:**
1. **Incomplete session-death detection** for the SIZE verification path (line 6477)
2. **Inconsistent classification** across the codebase
3. **Maintenance burden** — two definitions of "session dead" must stay in sync

---

## Recommended Refactoring

### Phase 1: Replace Local Function (Low Risk)

Replace line 6477 call:
```cpp
// BEFORE
if (!stored && reconnectPerFile && useFtps && len > 0 &&
    ftpsSessionLikelyDead(gFtpsClient.lastError())) {

// AFTER
if (!stored && reconnectPerFile && useFtps && len > 0 &&
    ftpsIsSessionDead(gFtpsClient.lastError())) {
```

**Change summary:**
- Remove `ftpsSessionLikelyDead()` function definition (lines 5512–5522)
- Replace the one call site (line 6477) with `ftpsIsSessionDead()`

**Impact:**
- ✅ Reduces coupling to private TankAlarm logic
- ✅ Aligns with library-standard session-death classification
- ⚠ **Trade-off:** Library version is stricter; `PassiveModeRejected` and `FinalReplyFailed` no longer trigger SIZE verification
  - **Assessment:** Acceptable — these are data-channel failures, not control-channel resets; reconnect + SIZE verify is defensive-programming overkill here
  
**Testing required:**
- Verify reconnect-per-file mode still recovers from socket resets (existing test coverage)
- No new test needed; existing retry suite covers this path

---

### Phase 2: Explicit Include (Documentation Clarity)

Add explicit include at top of file (after line 44):
```cpp
#include <FtpsErrors.h>
```

**Current situation:**
- `FtpsErrors.h` is included transitively via `FtpsClient.h` (line 11)
- Code already uses `ftpsIsSessionDead()`, `ftpsIsTransferRetriable()` directly
- No compilation issues, but the include is implicit

**Benefit:**
- Makes intent explicit for maintainers ("we intentionally use FTPS error helpers")
- Enables IDE navigation to helpers directly
- Clarifies what symbols are available from the library

**Risk:** None — just documentation.

---

### Phase 3: Enhanced Circuit-Breaker Logging (Optional Enhancement)

Current circuit-breaker logic (lines 6524–6541):
```cpp
if (attempts >= FTP_BACKUP_PER_FILE_MAX_ATTEMPTS &&
    ftpsIsTransferRetriable(lastErr, gFtpsClient.lastNsapiError())) {
  // File burned its full retry budget on a retriable error.
  consecutiveFailFiles++;
  if (consecutiveFailFiles >= FTP_BACKUP_CONSECUTIVE_FAILURES_ABORT) {
    abortRemainingTransfers = true;
    // ...
  }
}
```

**Enhancement:** Add diagnostic logging that breaks down the NSAPI error code:
```cpp
int nsapiCode = gFtpsClient.lastNsapiError();
if (nsapiCode == -3005) {
  Serial.println(F("Circuit-breaker: likely LWIP socket pool exhaustion"));
} else if (nsapiCode != 0) {
  Serial.print(F("Circuit-breaker: NSAPI error="));
  Serial.println(nsapiCode);
}
```

**Benefit:** Helps field diagnostics identify whether socket exhaustion or some other transient is responsible

**Recommendation:** **Defer to Phase 2 post-production** — add after collecting real-world data

---

## Implementation Checklist

### ☐ Phase 1: Core Refactoring
- [ ] Remove `ftpsSessionLikelyDead()` function (lines 5512–5522)
- [ ] Update line 6477 to call `ftpsIsSessionDead()` instead
- [ ] Compile and verify no regressions
- [ ] Run existing retry test suite
- [ ] Commit with message: "Adopt ftpsIsSessionDead() from FTPS library; remove local duplicate"

### ☐ Phase 2: Documentation
- [ ] Add `#include <FtpsErrors.h>` after line 44
- [ ] Add comment above per-file retry logic:
  ```cpp
  // Uses ftpsIsSessionDead() and ftpsIsTransferRetriable() from FtpsErrors.h
  // for standardized error classification (see PER_FILE_RETRY_PLAN_04172026.md).
  ```
- [ ] Update CODE_REVIEW/PER_FILE_RETRY_PLAN_04172026.md to cross-reference FtpsErrors.h

### ☐ Phase 3: Testing
- [ ] Run full TankAlarm retry test suite (existing coverage)
- [ ] Verify server-unavailable scenarios still handled correctly
- [ ] Verify socket exhaustion (-3005) triggers retry but not batch abort (at 1-2 consecutive)
- [ ] Verify permanent errors (LoginRejected, etc.) abort batch immediately

---

## Risk Assessment

### Compatibility
- ✅ **No API changes** — adopting existing library helpers
- ✅ **No behavioral changes** — `ftpsIsSessionDead()` covers all terminal cases TankAlarm needs
- ✅ **Backward compatible** — TankAlarm continues to compile with all mbed_opta versions that include ArduinoOPTA-FTPS

### Breaking Changes
- **None identified** — removing local function only affects internal retry logic

### Potential Issues & Mitigations

| Risk | Severity | Mitigation |
|------|----------|-----------|
| `ftpsIsSessionDead()` is stricter than `ftpsSessionLikelyDead()`; may not retry SIZE verify as often | Low | `PassiveModeRejected` is rare; defensive reconnect + SIZE verify is best-effort anyway |
| New code path not tested in CI | Medium | Run existing TankAlarm retry test suite before commit |
| Implicit include becomes explicit; affects build order | Low | Add `#include <FtpsErrors.h>` explicitly; Arduino IDE handles includes transitively anyway |

---

## Current vs. Proposed Error Classification

### `ftpsIsSessionDead()` (Library Standard — Used at lines 6443, 6519)

**Terminal failures (abort entire batch):**
- `ConnectionFailed` — Cannot reach host
- `NetworkNotInitialized` — Stack not ready
- `BannerReadFailed` — Server not responding
- `AuthTlsRejected` — TLS AUTH failed on control channel
- `ControlTlsHandshakeFailed` — Control-channel TLS setup failed
- `CertValidationFailed` — Server certificate rejected
- `LoginRejected` — AUTH credentials invalid
- `PbszRejected` — Server does not support PBSZ
- `ProtPRejected` — Server does not support PROT P (data-channel TLS)
- `TypeRejected` — Server does not support binary mode

---

### `ftpsSessionLikelyDead()` (Local TankAlarm Version — Used at line 6477)

**Session-likely-dead (for SIZE verify workaround):**
- `ConnectionFailed`
- `PassiveModeRejected` — **Extra (not in library)**
- `DataConnectionFailed` — **Overly inclusive** (not all data-channel failures kill session)
- `FinalReplyFailed` — **Extra (not in library)**

**Assessment:** Library version is more precise; local version was overly cautious workaround for Mbed OS socket teardown issues that have since been fixed.

---

## Adoption Timeline

| Phase | Task | Effort | Risk | Timeline |
|-------|------|--------|------|----------|
| 1 | Remove local duplicate; adopt library version | 15 min | Low | Immediate (before next release) |
| 2 | Add explicit include + comments | 5 min | None | Same commit as Phase 1 |
| 3 | Enhanced circuit-breaker logging | 30 min | Low | Post-production (v2.0+) |

---

## Conclusion

TankAlarm is **already well-integrated** with the ArduinoOPTA-FTPS error classification helpers. The recommended Phase 1 refactoring (removing the local `ftpsSessionLikelyDead()` duplicate and adopting the library's `ftpsIsSessionDead()`) is **low-risk, low-effort, and high-value** for code maintainability and consistency.

**Recommendation:** Proceed with Phase 1 and Phase 2 before the next TankAlarm production release to eliminate the code duplication and formalize the adoption.

---

## References

- **ArduinoOPTA-FTPS:** [FtpsErrors.h](../../ArduinoOPTA-FTPS/src/FtpsErrors.h) — Error types and classifiers
- **TankAlarm:** [TankAlarm-112025-Server-BluesOpta.ino](../TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino) — Lines 5512–5522, 6427–6535
- **Prior Analysis:** [PER_FILE_RETRY_PLAN_04172026.md](./PER_FILE_RETRY_PLAN_04172026.md) — Phase 0 foundation for retry logic
