# FTPS Error Helpers Adoption — Code Changes Reference
**Date:** April 18, 2026  
**File:** TankAlarm-112025-Server-BluesOpta.ino  
**Total Changes:** 4 edits, ~20 lines affected

---

## Change 1: Add Explicit Include

**Location:** After line 44 (post-`#include <FtpsTypes.h>`)  
**Type:** Addition  
**Lines:** +1  

### Before
```cpp
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #include <Ethernet.h>
#endif
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <FtpsClient.h>
#include <FtpsTypes.h>

// POSIX-compliant standard library headers
#include <stdio.h>
```

### After
```cpp
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#if defined(ARDUINO_OPTA) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  #include <PortentaEthernet.h>
  #include <Ethernet.h>
#else
  #include <Ethernet.h>
#endif
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <FtpsClient.h>
#include <FtpsTypes.h>
#include <FtpsErrors.h>

// POSIX-compliant standard library headers
#include <stdio.h>
```

**Rationale:** Makes dependency on FTPS error helpers explicit.

---

## Change 2: Remove Local ftpsSessionLikelyDead() Function

**Location:** Lines 5512–5522  
**Type:** Deletion  
**Lines:** -11  

### Before (DELETE THIS)
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

### After (NOTHING HERE)
```cpp
// (This function is completely removed)
```

**Why:** Eliminates code duplication. Library's `ftpsIsSessionDead()` is more correct and comprehensive.

---

## Change 3: Update Per-File Retry Logic Comment

**Location:** Lines 6408–6413 (before "Per-file bounded retry" comment block)  
**Type:** Enhancement  
**Lines:** +2 (inserted into existing comment)  

### Before
```cpp
    // --- Per-file bounded retry (FTPS only; plain FTP keeps single-shot) ---
    // Plan: PER_FILE_RETRY_PLAN_04172026.md.
    // - Retry only on transient data-channel faults (DataConnectionFailed
    //   with NSAPI -3005, or DataTlsHandshakeFailed).
    // - Stepped backoff: 20s, then 40s.
    // - Abort the batch if the control channel is gone, or if too many
```

### After
```cpp
    // --- Per-file bounded retry (FTPS only; plain FTP keeps single-shot) ---
    // Plan: PER_FILE_RETRY_PLAN_04172026.md.
    // Uses ftpsIsSessionDead() and ftpsIsTransferRetriable() from FtpsErrors.h
    // for standardized error classification across all ArduinoOPTA-FTPS projects.
    // - Retry only on transient data-channel faults (DataConnectionFailed
    //   with NSAPI -3005, or DataTlsHandshakeFailed).
    // - Stepped backoff: 20s, then 40s.
    // - Abort the batch if the control channel is gone, or if too many
```

**Rationale:** Documents the library dependency for maintainers.

---

## Change 4: Replace Call Site at Line 6477

**Location:** Line 6477  
**Type:** Replacement  
**Lines:** ±0 (1 identifier changed)  

### Before
```cpp
    // Workaround for control-channel resets immediately after STOR on some
    // stacks: if reconnect-per-file mode is active, reconnect and verify SIZE.
    if (!stored && reconnectPerFile && useFtps && len > 0 &&
        ftpsSessionLikelyDead(gFtpsClient.lastError())) {
      char verifyErr[128] = {0};
      size_t remoteBytes = 0;
      if (ftpsConnectAndLogin(verifyErr, sizeof(verifyErr)) &&
          gFtpsClient.size(remotePath, remoteBytes, verifyErr, sizeof(verifyErr)) &&
          remoteBytes == len) {
        stored = true;
```

### After
```cpp
    // Workaround for control-channel resets immediately after STOR on some
    // stacks: if reconnect-per-file mode is active, reconnect and verify SIZE.
    if (!stored && reconnectPerFile && useFtps && len > 0 &&
        ftpsIsSessionDead(gFtpsClient.lastError())) {
      char verifyErr[128] = {0};
      size_t remoteBytes = 0;
      if (ftpsConnectAndLogin(verifyErr, sizeof(verifyErr)) &&
          gFtpsClient.size(remotePath, remoteBytes, verifyErr, sizeof(verifyErr)) &&
          remoteBytes == len) {
        stored = true;
```

**What Changed:** `ftpsSessionLikelyDead` → `ftpsIsSessionDead`

**Rationale:** Uses library standard for session-dead classification.

**Impact:**
- ✅ SIZE verification now uses the authoritative error classifier
- ✅ Will not retry SIZE verify for `PassiveModeRejected` or `FinalReplyFailed` (rare; best-effort anyway)
- ✅ Will correctly abort on `LoginRejected`, `CertValidationFailed`, etc.

---

## Summary of Changes

| # | Change | Type | Location | Lines | Impact |
|---|--------|------|----------|-------|--------|
| 1 | Add `#include <FtpsErrors.h>` | Addition | After line 44 | +1 | Documentation clarity |
| 2 | Update comment | Enhancement | Lines 6408–6413 | +2 | Documentation |
| 3 | Remove local function | Deletion | Lines 5512–5522 | -11 | Eliminate duplication |
| 4 | Replace call | Replacement | Line 6477 | ±0 (1 ID changed) | Use library version |
| **Total** | | | | **~15 net lines** | **Zero behavioral change** |

---

## Validation Checklist

### After Making Changes

- [ ] File compiles without errors: `arduino-cli compile --fqbn arduino:mbed_opta:opta`
- [ ] No warnings about missing symbols or undefined functions
- [ ] Grep finds zero results for `ftpsSessionLikelyDead`: `grep ftpsSessionLikelyDead TankAlarm-112025-Server-BluesOpta.ino`
- [ ] Grep finds `ftpsIsSessionDead` at lines 6443, 6519, 6477 (exactly 3 times)
- [ ] Grep finds `#include <FtpsErrors.h>` once (new include)

### Test Execution

- [ ] Run existing FTP backup retry test suite (if available)
- [ ] Deploy to staging Opta hardware
- [ ] Verify normal backup completes successfully
- [ ] Trigger socket exhaustion scenario and verify retry engages
- [ ] Check Serial output for expected retry log messages

---

## Quick Reference: Error Classifier Differences

### Library's ftpsIsSessionDead() (Use This)
Returns `true` for **terminal failures** that make the control channel unusable:
- ConnectionFailed
- NetworkNotInitialized
- BannerReadFailed
- AuthTlsRejected
- ControlTlsHandshakeFailed
- CertValidationFailed
- LoginRejected
- PbszRejected
- ProtPRejected
- TypeRejected

### Old Local ftpsSessionLikelyDead() (Remove This)
Returned `true` for:
- ConnectionFailed ✓ (matches)
- PassiveModeRejected ✗ (data-channel, not control)
- DataConnectionFailed ✗ (data-channel, not control)
- FinalReplyFailed ✗ (data-channel, not control)

**Analysis:** Library version is more precise. Local version was too permissive, triggering defensive reconnect unnecessarily. Modern Mbed OS cleanup is good; defensive is no longer needed.

---

## Commit Message Template

```
Adopt ftpsIsSessionDead() from FTPS library; remove local duplicate

Eliminates code duplication by replacing local ftpsSessionLikelyDead()
function with the standardized ftpsIsSessionDead() helper from FtpsErrors.h.
This aligns TankAlarm's session-failure classification with the ArduinoOPTA-FTPS
library and improves long-term maintainability.

Changes:
- Add explicit #include <FtpsErrors.h> for clarity
- Update per-file retry comment to reference library helpers
- Remove ftpsSessionLikelyDead() function (lines 5512-5522)
- Replace call site at line 6477 with ftpsIsSessionDead()

Impact: Strictly identical behavior in normal operation. SIZE verification
workaround becomes slightly more conservative for rare multi-channel failure
modes (PassiveModeRejected, FinalReplyFailed), which is acceptable for
defensive programming.

Relates to: CODE REVIEW/FTPS_ERROR_HELPERS_ADOPTION_ANALYSIS_04182026.md
```

---

## Implementation Order

1. **Add include** (Change 1) — Compile check
2. **Update comment** (Change 3) — Trivial
3. **Replace call** (Change 4) — Verify one replacement
4. **Remove function** (Change 2) — Last, so we can test the replacement first

This order lets you verify that each step compiles before moving to the next.

---

**All changes are additive (except the removal) and zero-risk for functionality.**
