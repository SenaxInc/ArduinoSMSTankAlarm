# FTPS Error Helpers Adoption — Implementation Guide
**Date:** April 18, 2026  
**Status:** Ready for Implementation  
**Effort:** ~30 minutes total

---

## Overview

This guide provides step-by-step instructions for adopting the standardized FTPS error helpers from ArduinoOPTA-FTPS into TankAlarm's backup retry logic, eliminating code duplication and improving maintainability.

**Deliverables:**
1. Remove local `ftpsSessionLikelyDead()` shadow function
2. Replace the one call site with library's `ftpsIsSessionDead()`
3. Add explicit `#include <FtpsErrors.h>`
4. Update comments to document library dependency

---

## Changes Required

### Change 1: Add Explicit FtpsErrors Include

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** After line 44 (after `#include <FtpsTypes.h>`)  
**Type:** Addition

**Rationale:** Makes the dependency on `FtpsErrors.h` symbols explicit for code readers and IDE navigation.

**Instructions:**
1. Open `TankAlarm-112025-Server-BluesOpta.ino`
2. Locate line 44: `#include <FtpsTypes.h>`
3. Add new line after it:
   ```cpp
   #include <FtpsErrors.h>
   ```

**Validation:** File should compile without errors.

---

### Change 2: Update Per-File Retry Logic Comment

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Line 6408 (just before "Per-file bounded retry" comment)  
**Type:** Enhancement

**Current:**
```cpp
    // --- Per-file bounded retry (FTPS only; plain FTP keeps single-shot) ---
    // Plan: PER_FILE_RETRY_PLAN_04172026.md.
    // - Retry only on transient data-channel faults...
```

**Proposed:**
```cpp
    // --- Per-file bounded retry (FTPS only; plain FTP keeps single-shot) ---
    // Plan: PER_FILE_RETRY_PLAN_04172026.md.
    // Uses ftpsIsSessionDead() and ftpsIsTransferRetriable() from FtpsErrors.h
    // for standardized error classification across all projects using ArduinoOPTA-FTPS.
    // - Retry only on transient data-channel faults...
```

**Validation:** Clarifies the connection between TankAlarm retry logic and FTPS library helpers.

---

### Change 3: Remove Local ftpsSessionLikelyDead() Function

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Lines 5512–5522  
**Type:** Deletion

**Current code to remove:**
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

**Instructions:**
1. Locate the function at lines 5512–5522
2. Delete all 11 lines (including the blank line after the closing brace)

**Validation:**
- Verify no compilation errors
- Run `grep -n ftpsSessionLikelyDead` to confirm zero results (except the one update in step 4)

---

### Change 4: Replace Call Site — Line 6477

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Line 6477  
**Type:** Replacement

**Current:**
```cpp
    // Workaround for control-channel resets immediately after STOR on some
    // stacks: if reconnect-per-file mode is active, reconnect and verify SIZE.
    if (!stored && reconnectPerFile && useFtps && len > 0 &&
        ftpsSessionLikelyDead(gFtpsClient.lastError())) {
```

**Proposed:**
```cpp
    // Workaround for control-channel resets immediately after STOR on some
    // stacks: if reconnect-per-file mode is active, reconnect and verify SIZE.
    if (!stored && reconnectPerFile && useFtps && len > 0 &&
        ftpsIsSessionDead(gFtpsClient.lastError())) {
```

**Instructions:**
1. Locate line 6477
2. Replace `ftpsSessionLikelyDead` with `ftpsIsSessionDead`

**Impact:**
- Uses library standard for session-dead classification
- Slightly stricter: won't retry SIZE verify for `PassiveModeRejected` or `FinalReplyFailed` (rare conditions; best-effort verify anyway)
- No behavioral change for normal operation

**Validation:**
- Code compiles
- Comment 2 lines above now makes sense: "standardized error classification"

---

## Testing Checklist

### ☑ Compilation
```powershell
# In TankAlarm-112025-Server-BluesOpta folder
arduino-cli compile --fqbn arduino:mbed_opta:opta --library \
  C:\GitHub\ArduinoOPTA-FTPS
```
**Expected:** ✅ Successful compilation, no warnings about `ftpsSessionLikelyDead` or `FtpsErrors.h`

### ☑ Grep Validation
```powershell
# Confirm local function is completely removed
grep -n "ftpsSessionLikelyDead" TankAlarm-112025-Server-BluesOpta.ino
```
**Expected:** ✅ No results

### ☑ Existing Test Suite
- Run existing FTP backup retry tests (if available in CI/CD)
- Verify all retry scenarios still pass:
  - ✅ Socket pool exhaustion (NSAPI -3005) triggers per-file retry
  - ✅ TLS handshake failure (DataTlsHandshakeFailed) triggers retry
  - ✅ Login failure (LoginRejected) aborts entire batch
  - ✅ Connection failed (ConnectionFailed) aborts entire batch
  - ✅ Circuit-breaker still prevents cascade failures

### ☑ Manual Verification (Optional)
1. Deploy to real Opta hardware with FTPS server running
2. Verify normal backup completes successfully
3. Trigger socket exhaustion scenario (kill/restart FTPS server)
4. Verify per-file retry engages and recovers files
5. Check Serial output matches expected retry log messages

---

## Git Commit Strategy

### Single Commit (Recommended)

```bash
git add TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
git commit -m "Adopt ftpsIsSessionDead() from FTPS library; remove local duplicate

Eliminates code duplication by replacing local ftpsSessionLikelyDead() function
with the standardized ftpsIsSessionDead() helper from FtpsErrors.h. This aligns
TankAlarm's session-failure classification with the ArduinoOPTA-FTPS library
and improves long-term maintainability.

Changes:
- Add explicit #include <FtpsErrors.h>
- Update per-file retry comment to reference library helpers
- Remove ftpsSessionLikelyDead() function (lines 5512-5522)
- Replace call site at line 6477 with ftpsIsSessionDead()

Impact: Strictly identical behavior in normal operation. SIZE verification
workaround becomes slightly more conservative for rare multi-channel failure
modes (PassiveModeRejected, FinalReplyFailed), which is acceptable for
defensive programming.

Relates to: CODE REVIEW/FTPS_ERROR_HELPERS_ADOPTION_ANALYSIS_04182026.md"
```

### Two-Commit Approach (If Separated by Review)

**Commit 1: Remove duplication**
```bash
git commit -m "refactor: Remove local ftpsSessionLikelyDead(); adopt library version

- Add #include <FtpsErrors.h>
- Remove ftpsSessionLikelyDead() function
- Replace call at line 6477 with ftpsIsSessionDead()"
```

**Commit 2: Add documentation**
```bash
git commit -m "docs: Document FTPS error classification dependency

- Update per-file retry logic comment to reference FtpsErrors.h
- Clarifies that TankAlarm now uses standardized helpers from ArduinoOPTA-FTPS"
```

---

## Rollback Plan

If unexpected behavior is observed:

### Option A: Quick Local Revert
```bash
git revert <commit-hash>
# Deploy previous build to device
# Investigate in CODE REVIEW/ subfolder
```

### Option B: Restore Local Function (Minimal)
If only the SIZE verification workaround needs the old logic:
```cpp
// Add back local function if needed
static bool ftpsSessionLikelyDeadLegacy(FtpsError error) {
  // ...old implementation...
}
// Call it only at line 6477
if (!stored && reconnectPerFile && useFtps && len > 0 &&
    ftpsSessionLikelyDeadLegacy(gFtpsClient.lastError())) {
```

**Likelihood:** Very low — library helper is well-tested in WebFileManager and other projects.

---

## Documentation Updates

### File: CODE REVIEW/FTPS_ERROR_HELPERS_ADOPTION_ANALYSIS_04182026.md
✅ Already created. References this guide.

### File: PER_FILE_RETRY_PLAN_04172026.md (Optional)
Add cross-reference (if that file exists):
```markdown
## Standardized Error Classification

As of [commit hash], TankAlarm uses the standardized error classifiers from
ArduinoOPTA-FTPS library (`FtpsErrors.h`):

- `ftpsIsSessionDead(err)` — Detect terminal control-channel failures
- `ftpsIsTransferRetriable(err, nsapiCode)` — Detect transient data-channel faults

This eliminates the need for local error classification logic and ensures
consistency across all projects using the FTPS library.
```

---

## Success Criteria

✅ **All tests pass without warnings**
✅ **Local duplicate function removed**
✅ **Call site updated to use library version**
✅ **Explicit include added**
✅ **Comments updated**
✅ **Existing retry test suite passes**
✅ **No behavioral change in normal operation**
✅ **Code compiles on mbed_opta 4.5.0+**

---

## Timeline

| Step | Task | Time | Cumulative |
|------|------|------|-----------|
| 1 | Add include & update comment | 2 min | 2 min |
| 2 | Remove local function | 1 min | 3 min |
| 3 | Replace call site | 1 min | 4 min |
| 4 | Compile check | 5 min | 9 min |
| 5 | Git validation | 2 min | 11 min |
| 6 | Full test suite | 10–20 min | 21–31 min |
| 7 | Manual verification (optional) | 15 min | 36–46 min |

**Total:** ~30 minutes for full implementation + testing.

---

## Next Steps

1. **Schedule:** Plan implementation for next TankAlarm maintenance window
2. **Review:** Have a team member review changes before commit
3. **Testing:** Run full retry test suite on staging hardware
4. **Documentation:** Update CHANGELOG.md with this improvement
5. **Release Notes:** Mention "Code quality: eliminated FTPS error duplication" in v1.1+ notes
