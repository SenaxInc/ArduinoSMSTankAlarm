# H4 — Atomic File Write Refactor Recommendation

**Date:** February 20, 2026  
**Author:** AI Code Review (Claude)  
**Severity:** HIGH — Data-loss risk on power failure during save operations  
**Scope:** Server (.ino), Client (.ino), Common header (TankAlarm_Platform.h)

---

## 1. Problem Statement

Every save function in the codebase follows the same vulnerable pattern:

```
fopen(path, "w")   ← Immediately truncates existing file to zero bytes
fwrite(data)        ← Writes new content (takes non-trivial time on flash)
fclose()            ← Finalizes write
```

If power is lost **after** `fopen("w")` but **before** `fclose()` completes, the file
is left truncated or partially written. The original data is **permanently destroyed**.
On next boot, the device loads a corrupt or empty file and falls back to defaults,
losing all user configuration.

This affects **every configuration file** in the system — server config, client config,
contacts, tank registry, calibration data, email format, and more.

### Real-World Risk Assessment

The Arduino Opta units are deployed in industrial/agricultural environments where:
- Power outages are common (storms, generator failures)
- Save operations are triggered by web UI interactions (user-initiated, unpredictable timing)
- Periodic saves (heartbeat, registry) happen on timers that may coincide with brownouts
- Flash write speed on STM32H7 internal flash is non-trivial (~1-5ms per page)

---

## 2. Existing Reference Implementation

**Good news:** The codebase already proves the atomic pattern works on this platform.

The client's `flushBufferedNotes()` function (Client .ino, lines ~4589-4675) already
implements write-to-temp-then-rename for both POSIX and LittleFS paths:

### POSIX Path (Mbed OS)
```cpp
FILE *tmp = fopen("/fs/pending_notes.tmp", "w");
// ... write all data to tmp ...
fclose(tmp);

if (wroteFailures) {
    remove("/fs/pending_notes.log");
    rename("/fs/pending_notes.tmp", "/fs/pending_notes.log");
} else {
    remove("/fs/pending_notes.log");
    remove("/fs/pending_notes.tmp");
}
```

### LittleFS Path (Arduino/STM32)
```cpp
File tmp = LittleFS.open(NOTE_BUFFER_TEMP_PATH, "w");
// ... write all data to tmp ...
tmp.close();

if (wroteFailures) {
    LittleFS.remove(NOTE_BUFFER_PATH);
    LittleFS.rename(NOTE_BUFFER_TEMP_PATH, NOTE_BUFFER_PATH);
}
```

This confirms that both `rename()` (POSIX) and `LittleFS.rename()` work correctly
on the target platform. No platform investigation is needed.

---

## 3. Complete File Write Inventory

### 3.1 Server Write Sites (15 operations across 11 functions)

| # | Function | File Written | Risk | Pattern | Lines |
|---|----------|-------------|------|---------|-------|
| S1 | `saveServerConfig()` | `/fs/server_config.json` | **CRITICAL** | Serialize→String→fwrite | ~2661 |
| S2 | `saveServerHeartbeatEpoch()` | `/fs/server_heartbeat.json` | LOW | Serialize→String→fwrite | ~2763 |
| S3 | `writeBufferToFile()` | Various (FTP download) | MEDIUM | Binary fwrite | ~2895 |
| S4 | `ftpRestoreClientConfigs()` | `/fs/client_config_cache.txt` | MEDIUM | Iterative fprintf | ~3539 |
| S5 | `saveHistorySettings()` | `/fs/history_settings.json` | MEDIUM | Serialize→String→fwrite | ~4171 |
| S6 | `saveTankRegistry()` | `/fs/tank_registry.json` | **CRITICAL** | Via `posix_write_file()` | ~7430 |
| S7 | `saveClientMetadataCache()` | `/fs/client_metadata.json` | HIGH | Via `posix_write_file()` | ~7567 |
| S8 | `saveClientConfigSnapshots()` | `/fs/client_config_cache.txt` | HIGH | Iterative fprintf | ~8004 |
| S9 | `saveContactsConfig()` | `/fs/contacts_config.json` | **CRITICAL** | String→fwrite | ~8583 |
| S10 | `saveEmailFormat()` | `/fs/email_format.json` | MEDIUM | String→fwrite | ~8824 |
| S11 | `saveCalibrationEntry()` | `/fs/calibration_log.txt` | SAFE | **Append** mode | ~9504 |
| S12 | `saveCalibrationData()` | `/fs/calibration_data.txt` | HIGH | Iterative fprintf | ~9663 |
| S13 | Calibration log rewrite | `/fs/calibration_log.txt` | MEDIUM | Read→filter→rewrite | ~10118 |

### 3.2 Client Write Sites (3 operations)

| # | Function | File Written | Risk | Pattern | Lines |
|---|----------|-------------|------|---------|-------|
| C1 | `saveClientConfig()` (alias: `saveConfigToFlash()`) | `/fs/client_config.json` | **CRITICAL** | Serialize→String→fwrite | ~1740 |
| C2 | `bufferNoteForRetry()` | `/fs/pending_notes.log` | SAFE | **Append** mode | ~4552 |
| C3 | `flushBufferedNotes()` | `/fs/pending_notes.tmp` → rename | SAFE | **Already atomic** | ~4594 |

### 3.3 Viewer Write Sites

None — the Viewer is read-only.

### 3.4 Risk Categories Explained

- **CRITICAL**: Loss destroys user-entered configuration requiring manual re-entry (server config, client config, contacts, tank registry)
- **HIGH**: Loss destroys derived/cached data that takes significant time to rebuild (client metadata, calibration data, config snapshots)
- **MEDIUM**: Loss destroys settings that have reasonable defaults or can be re-fetched (history settings, email format, FTP downloads)
- **LOW**: Loss is self-healing on next operation (heartbeat epoch)
- **SAFE**: Append-only (no truncation risk) or already uses atomic pattern

---

## 4. Proposed Solution: Write-to-Temp-Then-Rename

### 4.1 Why This Approach

LittleFS's `lfs_rename()` is **designed to be power-safe**. The LittleFS specification
guarantees that rename is atomic — it uses a copy-on-write log structure. If power is
lost during rename, LittleFS recovers to either the old or new state on mount, never
a corrupt intermediate state.

This gives us the following safety guarantee:
1. Write new data to `path.tmp` — if power fails, the original file is untouched
2. Rename `path.tmp` → `path` — atomic; either completes fully or not at all
3. On boot, the file is always either the old valid version or the new valid version

### 4.2 Why Not Other Approaches

| Alternative | Why Not |
|------------|---------|
| Double-write (write to A and B) | 2x flash wear, 2x storage, complex read logic |
| CRC/checksum validation | Detects corruption but doesn't prevent data loss |
| Journal/WAL | Massive complexity for an embedded system |
| Shadow copy + flag byte | Requires manual state machine, more code than rename |
| Write in-place with `fflush()` | Still truncates first; `fflush()` doesn't help |

The write-to-temp-then-rename pattern is the **industry standard** for embedded
systems with LittleFS. It's simple, proven, and already validated in this codebase.

---

## 5. Implementation Plan

### Phase 1: Add Atomic Write Helpers to Common Header

Add these functions to `TankAlarm-112025-Common/src/TankAlarm_Platform.h`, inside
the existing `#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE` block:

```cpp
/**
 * Atomic file write using write-to-temp-then-rename pattern.
 * Ensures the target file is never left in a truncated/corrupt state.
 * 
 * @param path   Target file path (e.g., "/fs/server_config.json")
 * @param data   Data buffer to write
 * @param len    Number of bytes to write
 * @return true on success, false on any error (original file preserved)
 */
static inline bool tankalarm_posix_write_file_atomic(const char *path,
                                                      const char *data,
                                                      size_t len)
{
    // Build temp path: "/fs/server_config.json" → "/fs/server_config.json.tmp"
    char tmpPath[128];
    int n = snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmpPath)) {
        // Path too long for temp suffix
        tankalarm_posix_log_error("atomic:path_too_long", path);
        return false;
    }

    // Step 1: Write data to temporary file
    FILE *fp = fopen(tmpPath, "w");
    if (!fp) {
        tankalarm_posix_log_error("atomic:fopen", tmpPath);
        return false;
    }
    
    size_t written = fwrite(data, 1, len, fp);
    int writeErr = ferror(fp);

    // Flush C library buffers before close to ensure data reaches filesystem
    fflush(fp);
    fclose(fp);

    if (writeErr || written != len) {
        tankalarm_posix_log_error("atomic:fwrite", tmpPath);
        remove(tmpPath);  // Clean up failed temp file
        return false;
    }

    // Step 2: Atomic rename — replaces target file in one operation
    // On LittleFS (Mbed OS), rename handles existing target atomically.
    // We remove first for maximum portability across POSIX implementations.
    remove(path);
    if (rename(tmpPath, path) != 0) {
        tankalarm_posix_log_error("atomic:rename", path);
        // IMPORTANT: tmpPath still contains the valid new data.
        // On next boot, recovery code could check for .tmp files.
        return false;
    }

    return true;
}
```

**Note on the `remove()`→`rename()` sequence:** There is a tiny window between
`remove()` and `rename()` where neither file exists. On LittleFS, `rename()` with
an existing target is atomic and the `remove()` is technically unnecessary. However,
some POSIX implementations require the target to not exist. The recommendation is:

- **Option A (Conservative):** Keep `remove()` + `rename()` as shown above.
  Matches the existing `flushBufferedNotes()` pattern already in the codebase.
  The vulnerability window is microseconds vs. the milliseconds of a full write.

- **Option B (Optimal):** For Mbed/LittleFS only, skip the `remove()` and call
  `rename()` directly. LittleFS guarantees atomic overwrite. This eliminates
  even the tiny window but reduces portability.

**Recommended: Option A** — it matches the existing codebase pattern and the
risk window is negligible compared to the current vulnerability.

### Phase 2: Migrate `posix_write_file()` in Server

The server's local `posix_write_file()` helper (line ~801) is already used by
`saveTankRegistry()` and `saveClientMetadataCache()`. Modify it to use the atomic
helper:

```cpp
// BEFORE (current):
static bool posix_write_file(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "w");
    if (!fp) { posix_log_error("fopen", path); return false; }
    size_t written = fwrite(data, 1, len, fp);
    int writeErr = ferror(fp);
    fclose(fp);
    if (writeErr || written != len) { posix_log_error("fwrite", path); return false; }
    return true;
}

// AFTER (atomic):
static bool posix_write_file(const char *path, const char *data, size_t len) {
    return tankalarm_posix_write_file_atomic(path, data, len);
}
```

This single change immediately protects:
- ✅ S6: `saveTankRegistry()` — **CRITICAL**
- ✅ S7: `saveClientMetadataCache()` — HIGH

### Phase 3: Migrate Individual Save Functions

Each save function needs modification. The changes fall into two structural patterns:

#### Pattern A: Serialize-to-String First (simplest migration)

Functions that already serialize JSON to a `String` before writing. These just need
to swap `fopen/fwrite/fclose` for the atomic helper.

**Affected:** S1 (`saveServerConfig`), S2 (`saveServerHeartbeatEpoch`),
S5 (`saveHistorySettings`), S9 (`saveContactsConfig`), S10 (`saveEmailFormat`),
C1 (`saveClientConfig`)

Example migration for `saveServerConfig()`:

```cpp
// BEFORE (POSIX branch):
FILE *file = fopen("/fs/server_config.json", "w");
if (!file) {
    Serial.println(F("Failed to open server config for write"));
    return false;
}
String jsonStr;
size_t len = serializeJson(doc, jsonStr);
if (len == 0) {
    fclose(file);
    Serial.println(F("Failed to serialize server config"));
    return false;
}
size_t written = fwrite(jsonStr.c_str(), 1, jsonStr.length(), file);
fclose(file);
if (written != jsonStr.length()) {
    Serial.println(F("Failed to write server config (incomplete)"));
    return false;
}
return true;

// AFTER (atomic):
String jsonStr;
size_t len = serializeJson(doc, jsonStr);
if (len == 0) {
    Serial.println(F("Failed to serialize server config"));
    return false;
}
if (!tankalarm_posix_write_file_atomic("/fs/server_config.json",
                                        jsonStr.c_str(), jsonStr.length())) {
    Serial.println(F("Failed to write server config"));
    return false;
}
return true;
```

**Key benefit:** The code actually gets **simpler** — no more manual fopen/fwrite/fclose.

#### Pattern B: Iterative Writers (need buffer accumulation)

Functions that write line-by-line in a loop using `fprintf()`. These need to
accumulate output into a buffer first, then write atomically.

**Affected:** S8 (`saveClientConfigSnapshots`), S12 (`saveCalibrationData`),
S13 (calibration log rewrite), S4 (`ftpRestoreClientConfigs`)

Two sub-approaches for iterative writers:

**B1: Use Arduino String accumulation** (simplest, for smaller files)
```cpp
// BEFORE:
FILE *file = fopen("/fs/calibration_data.txt", "w");
if (!file) return;
for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
    fprintf(file, "%s\t%d\t...\n", ...);
}
fclose(file);

// AFTER:
String output;
output.reserve(gTankCalibrationCount * 80);  // Estimate line length
for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
    char line[128];
    snprintf(line, sizeof(line), "%s\t%d\t...\n", ...);
    output += line;
}
tankalarm_posix_write_file_atomic("/fs/calibration_data.txt",
                                  output.c_str(), output.length());
```

**B2: Use malloc buffer with snprintf** (for larger files, avoids String fragmentation)
```cpp
// For saveClientConfigSnapshots which can be large:
size_t bufSize = (size_t)gClientConfigCount * 512;  // Generous estimate
char *buf = (char *)malloc(bufSize);
if (!buf) {
    Serial.println(F("ERROR: Cannot allocate save buffer"));
    return;
}
size_t pos = 0;
for (uint8_t i = 0; i < gClientConfigCount; ++i) {
    int n = snprintf(buf + pos, bufSize - pos, "%s\t%s\t...\n", ...);
    if (n < 0 || pos + (size_t)n >= bufSize) break;
    pos += (size_t)n;
}
tankalarm_posix_write_file_atomic("/fs/client_config_cache.txt", buf, pos);
free(buf);
```

**Recommendation:** Use B1 (String) for files expected to be under ~2KB
(calibration_data, history_settings). Use B2 (malloc) for potentially larger
files (client_config_cache with multiple clients).

#### Pattern C: Binary Writer

**Affected:** S3 (`writeBufferToFile`) — used for FTP-downloaded files.

```cpp
// BEFORE:
FILE *file = fopen(fullPath, "wb");
if (!file) return false;
size_t written = fwrite(data, 1, len, file);
fclose(file);
return written == len;

// AFTER:
return tankalarm_posix_write_file_atomic(fullPath, (const char *)data, len);
```

### Phase 4: LittleFS (Non-Mbed) Branch Migration

Each `#else` (LittleFS/Arduino) branch needs the same treatment. Since there's no
centralized helper for the LittleFS path, add one:

```cpp
// Add to each .ino file's LittleFS utility section, or to common header
// under a separate #ifdef block:
#if defined(TANKALARM_FILESYSTEM_AVAILABLE) && !defined(TANKALARM_POSIX_FILE_IO_AVAILABLE)
static bool littlefs_write_file_atomic(const char *path, const char *data, size_t len) {
    // Build temp path
    String tmpPath = String(path) + ".tmp";
    
    File tmp = LittleFS.open(tmpPath.c_str(), "w");
    if (!tmp) return false;
    
    size_t written = tmp.write((const uint8_t *)data, len);
    tmp.close();
    
    if (written != len) {
        LittleFS.remove(tmpPath.c_str());
        return false;
    }
    
    LittleFS.remove(path);
    return LittleFS.rename(tmpPath.c_str(), path);
}
#endif
```

### Phase 5: Optional `.tmp` File Recovery on Boot

For maximum resilience, add a recovery check during `initializeStorage()`:

```cpp
// After filesystem mount succeeds, check for orphaned .tmp files.
// If a .tmp exists but the original doesn't, the rename failed after
// the original was removed — recover by completing the rename.
static void recoverOrphanedTmpFiles() {
    // List of critical config files to check
    static const char *criticalFiles[] = {
        "/fs/server_config.json",
        "/fs/contacts_config.json", 
        "/fs/tank_registry.json",
        "/fs/client_metadata.json",
        "/fs/client_config_cache.txt",
        "/fs/calibration_data.txt",
        "/fs/email_format.json",
        "/fs/history_settings.json",
        nullptr
    };
    
    for (int i = 0; criticalFiles[i] != nullptr; i++) {
        char tmpPath[128];
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", criticalFiles[i]);
        
        if (tankalarm_posix_file_exists(tmpPath)) {
            if (!tankalarm_posix_file_exists(criticalFiles[i])) {
                // Original was removed but rename didn't complete — recover
                if (rename(tmpPath, criticalFiles[i]) == 0) {
                    Serial.print(F("Recovered: "));
                    Serial.println(criticalFiles[i]);
                }
            } else {
                // Both exist — original is valid, remove stale tmp
                remove(tmpPath);
            }
        }
    }
}
```

This covers the edge case where power fails between `remove(path)` and
`rename(tmpPath, path)` in the atomic write helper.

---

## 6. Migration Checklist

### Critical Priority (implement first)

| ID | Function | File | Est. Effort |
|----|----------|------|-------------|
| S1 | `saveServerConfig()` | Server | Pattern A — Simple |
| C1 | `saveClientConfig()` | Client | Pattern A — Simple |
| S9 | `saveContactsConfig()` | Server | Pattern A — Simple |
| S6 | `saveTankRegistry()` | Server | **Already done** via Phase 2 (`posix_write_file`) |

### High Priority

| ID | Function | File | Est. Effort |
|----|----------|------|-------------|
| S7 | `saveClientMetadataCache()` | Server | **Already done** via Phase 2 |
| S8 | `saveClientConfigSnapshots()` | Server | Pattern B2 — Moderate |
| S12 | `saveCalibrationData()` | Server | Pattern B1 — Simple |

### Medium Priority

| ID | Function | File | Est. Effort |
|----|----------|------|-------------|
| S5 | `saveHistorySettings()` | Server | Pattern A — Simple |
| S10 | `saveEmailFormat()` | Server | Pattern A — Simple |
| S3 | `writeBufferToFile()` | Server | Pattern C — Simple |
| S4 | `ftpRestoreClientConfigs()` | Server | Pattern B2 — Moderate |
| S13 | Calibration log rewrite | Server | Pattern B1 — Simple |

### No Change Needed

| ID | Function | Reason |
|----|----------|--------|
| S2 | `saveServerHeartbeatEpoch()` | Low risk — self-heals on next heartbeat. Can still migrate for consistency. |
| S11 | `saveCalibrationEntry()` | Append mode — never truncates existing data |
| C2 | `bufferNoteForRetry()` | Append mode — never truncates existing data |
| C3 | `flushBufferedNotes()` | Already uses atomic rename pattern |

---

## 7. Testing Strategy

### 7.1 Unit-Level Verification

For each migrated function:

1. **Happy path:** Save → Load → Verify data integrity
2. **Temp file cleanup:** After successful save, verify no `.tmp` file remains
3. **Write failure simulation:** Fill filesystem to near-capacity, verify:
   - Original file preserved when temp write fails
   - No orphaned temp files left behind
4. **Path length edge case:** Verify `snprintf` overflow protection for long paths

### 7.2 Power-Failure Simulation

This is the critical test. On the Arduino Opta:

1. Start a save operation (e.g., save server config via web UI)
2. Pull power at various points during the write
3. Re-power and verify:
   - Configuration file is either the old version or the new version (never corrupt)
   - System boots normally without errors
   - Recovery code handles orphaned `.tmp` files

**Practical approach:** Add a `delay()` between the temp write and the rename,
and pull power during that delay. This simulates the worst case.

### 7.3 Flash Wear Assessment

The atomic pattern writes to `path.tmp` then renames. Compared to direct write:
- **Extra writes:** One extra file create + one extra remove per save (the `.tmp` file)
- **Rename cost:** On LittleFS, rename updates directory metadata only (no data copy)
- **Net impact:** Roughly 1.5–2x the metadata writes per save operation

On STM32H7's internal flash (100K+ erase cycles), with saves happening at most
every few minutes, this is **negligible**. The flash will outlast the hardware
by orders of magnitude.

### 7.4 RAM Impact Assessment

| Pattern | Current RAM Usage | After Migration |
|---------|-------------------|-----------------|
| A (Serialize first) | Already uses `String jsonStr` | **No change** — same buffer, just passed to helper |
| B1 (String accumulation) | Writes directly to file | +200-2KB temporary String (freed after save) |
| B2 (malloc buffer) | Writes directly to file | Uses existing `malloc` pattern (S6, S7 already do this) |
| C (Binary) | Writes directly to file | **No change** — data already in buffer |

The biggest RAM impact is on iterative writers (Pattern B), but these are called
infrequently and the buffers are freed immediately.

---

## 8. Implementation Order Recommendation

```
Step 1: Add tankalarm_posix_write_file_atomic() to TankAlarm_Platform.h
Step 2: Add littlefs_write_file_atomic() to TankAlarm_Platform.h (or local)
Step 3: Modify posix_write_file() in Server to use atomic helper  ← 2 CRITICAL files fixed
Step 4: Migrate saveServerConfig() (POSIX + LittleFS)             ← 3rd CRITICAL
Step 5: Migrate saveClientConfig() in Client                      ← 4th CRITICAL  
Step 6: Migrate saveContactsConfig()                              ← 5th CRITICAL
Step 7: Migrate remaining HIGH-priority functions (S8, S12)
Step 8: Migrate MEDIUM-priority functions (S3, S4, S5, S10, S13)
Step 9: Add recoverOrphanedTmpFiles() to initializeStorage()
Step 10: Optional — migrate S2 (heartbeat) for consistency
```

**Estimated total effort:** 2–3 hours for an experienced developer familiar with the codebase.
Steps 1–6 (all CRITICAL files) can be completed in under 1 hour.

---

## 9. File-by-File Change Summary

### TankAlarm_Platform.h (Common)
- Add `tankalarm_posix_write_file_atomic()` (~25 lines)
- Add `tankalarm_littlefs_write_file_atomic()` (~20 lines)

### TankAlarm-112025-Server-BluesOpta.ino
- Modify `posix_write_file()`: 1-line body change → delegates to atomic helper
- Modify 9 save functions: Replace `fopen/fwrite/fclose` with atomic helper call
- Add `recoverOrphanedTmpFiles()` to `initializeStorage()` (~30 lines)
- Net code change: **Reduction** of ~80 lines (atomic helper eliminates boilerplate)

### TankAlarm-112025-Client-BluesOpta.ino
- Modify `saveClientConfig()` (both POSIX and LittleFS branches)
- Net code change: **Reduction** of ~10 lines

### TankAlarm-112025-Viewer-BluesOpta.ino
- **No changes** — no file write operations

---

## 10. Risks and Mitigations

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| `rename()` not supported | Very Low | Already proven working in `flushBufferedNotes()` |
| `.tmp` file fills flash | Very Low | Cleaned up on success; recovery removes on boot |
| Path > 128 chars | None | Longest path is `/fs/client_config_cache.txt.tmp` (35 chars) |
| RAM spike from buffering | Low | Only Pattern B functions need extra buffer; freed immediately |
| LittleFS `rename()` doesn't overwrite | Very Low | Using `remove()` + `rename()` to be safe |
| Window between `remove()` and `rename()` | Extremely Low | ~microseconds; recovery code in Phase 5 covers it |

---

## 11. Appendix: Complete Current Write Pattern Map

```
SERVER WRITES (by code path):
├── posix_write_file() [centralized helper — line 801]
│   ├── saveTankRegistry()           → /fs/tank_registry.json
│   └── saveClientMetadataCache()    → /fs/client_metadata.json
│
├── Direct fopen("w")/fwrite/fclose [inline in each function]
│   ├── saveServerConfig()           → /fs/server_config.json
│   ├── saveServerHeartbeatEpoch()   → /fs/server_heartbeat.json
│   ├── saveHistorySettings()        → /fs/history_settings.json
│   ├── saveContactsConfig()         → /fs/contacts_config.json
│   └── saveEmailFormat()            → /fs/email_format.json
│
├── Direct fopen("w")/fprintf loop/fclose [iterative writers]
│   ├── saveClientConfigSnapshots()  → /fs/client_config_cache.txt
│   ├── saveCalibrationData()        → /fs/calibration_data.txt
│   ├── ftpRestoreClientConfigs()    → /fs/client_config_cache.txt
│   └── handleCalibrationDelete()    → /fs/calibration_log.txt (rewrite)
│
├── Direct fopen("wb")/fwrite/fclose [binary]
│   └── writeBufferToFile()          → /fs/<various>
│
└── Direct fopen("a")/fputs/fclose [append — SAFE]
    └── saveCalibrationEntry()       → /fs/calibration_log.txt

CLIENT WRITES:
├── Direct fopen("w")/fwrite/fclose
│   └── saveClientConfig()           → /fs/client_config.json
│
├── Direct fopen("a")/fprintf/fclose [append — SAFE]
│   └── bufferNoteForRetry()         → /fs/pending_notes.log
│
└── Atomic write-to-tmp-then-rename [ALREADY SAFE]
    └── flushBufferedNotes()         → /fs/pending_notes.tmp → .log
```

---

*End of H4 Atomic Write Refactor Recommendation*

---

## 12. Additional Recommendations (AI Review - Feb 20, 2026)

The following recommendations are added based on further analysis of Mbed OS and LittleFS behavior:

### 12.1 CRITICAL: Do NOT use `remove()` before `rename()`

The original recommendation (Option A) suggested calling `remove(path)` before `rename(tmpPath, path)` to ensure portability. However, on Mbed OS with LittleFS, `rename()` is **atomic** and correctly handles overwriting an existing destination file.

**Introducing `remove()` before `rename()` explicitly creates a window of data loss.** If power fails after `remove()` but before `rename()` completes, the file is lost entirely.

**Recommendation:**
Use **Option B (Optimal)** for Mbed/LittleFS platforms. Call `rename()` directly.

```cpp
// CORRECT Implementation for Mbed/LittleFS:
if (rename(tmpPath, path) != 0) {
    tankalarm_posix_log_error("atomic:rename", path);
    // removing tmpPath is optional here, as it might be useful for recovery
    return false;
}
return true;
```

If cross-platform compatibility with strict POSIX implementations (that don't support overwrite-rename) is required, use a compile-time check:

```cpp
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_OPTA)
    // LittleFS on Mbed supports atomic overwrite
    if (rename(tmpPath, path) != 0) { ... }
#else
    // Fallback for platforms where rename might fail if target exists
    remove(path);
    if (rename(tmpPath, path) != 0) { ... }
#endif
```

### 12.2 Ensure Directory Sync (fsync)

While `fclose()` on Mbed OS generally ensures data is flushed to the storage device, solely relying on `fflush()` (as seen in some examples) only flushes the C library user-space buffers.

**Recommendation:**
Ensure `fsync()` (or platform equivalent) is called on the file descriptor before closing to guarantee data is physically written to flash media.
On Mbed, `fclose()` invokes `fsync`, so explicit `fsync` might be redundant but safe. However, verify that `fflush` is NOT used as the *only* synchronization mechanism before `fclose`.

### 12.3 Temporary File Creation Flags

When opening the temporary file, use `O_EXCL` if available (via `open()` syscall) or ensure the temporary file name is unique enough to avoid collisions, though in this single-threaded embedded context `fopen(..., "w")` is acceptable. The proposed `.tmp` suffix coupled with the atomic rename strategy is sufficient.

### 12.4 Verify Rename Return Value

The implementation must check the return value of `rename()`. If it fails (e.g., flash full, permission error), the system must handle the orphan `.tmp` file. The proposed recovery logic in Phase 5 covers boot-time recovery, but runtime handling should also attempt to clean up `tmpPath` if `rename` fails to avoid flash exhaustion over time.

### 12.5 Summary of Critical Fixes

1.  **Drop `remove()`**: Modify the implementation plan to use direct `rename()` on Opta/Mbed.
2.  **Verify `fclose` flushing**: Ensure `fclose` is relied upon for sync, not just `fflush`.

---

## 13. Additional Gaps Found (Copilot Review - Feb 19, 2026)

The following items appear to be missing or understated in the current plan after cross-checking against the current `.ino` implementations.

### 13.1 Inventory Count Mismatch (Scope Clarity)

Section 3.1 says **"15 operations across 11 functions"**, but the listed IDs are `S1..S13` (13 operations). This should be corrected so migration progress can be tracked accurately.

### 13.2 Missed Client Write Path: `pruneNoteBufferIfNeeded()`

The client contains an additional temp-write-and-replace flow that is not listed in Section 3:

- POSIX branch: `fopen("/fs/pending_notes.tmp", "w")` then `remove("/fs/pending_notes.log")` then `rename(...)`
- LittleFS branch: `LittleFS.open(NOTE_BUFFER_TEMP_PATH, "w")` then `LittleFS.remove(...)` then `LittleFS.rename(...)`

This function should be added to the write inventory and migration checklist. It is not config-critical, but it has the same power-loss window introduced by `remove()` before `rename()`.

### 13.3 `flushBufferedNotes()` Is Not Fully "SAFE"

`flushBufferedNotes()` is currently classified as already safe, but both POSIX and LittleFS branches currently:

1. remove the destination file first,
2. then rename the temp file,
3. and do not check rename success consistently.

That means a power-fail or rename failure can still lose buffered notes. Recommend reclassifying as **PARTIALLY SAFE** until updated to direct rename (or fallback strategy) with return-value checks.

### 13.4 Add Rename Result Checks Everywhere

The plan mentions checking `rename()` in helper code, but existing note-buffer flows (`flushBufferedNotes`, `pruneNoteBufferIfNeeded`) currently call `rename()` / `LittleFS.rename()` without validating success. Add explicit checks and deterministic cleanup policy:

- On rename success: remove stale temp if needed.
- On rename failure: keep temp for recovery, log error, and avoid deleting last known good file unless overwrite-rename is guaranteed.

### 13.5 Make the Atomic Helper Binary-Safe by Default

`writeBufferToFile()` writes binary payloads (`uint8_t*`) for FTP restore paths. To avoid accidental text-mode behavior drift across platforms, recommend the helper API and implementation support binary writes explicitly:

- signature accepts `const uint8_t *data` (or overload)
- open temp with `"wb"`
- keep length-based writes (already good)

This lets JSON/text callers pass string bytes while preserving correctness for binary payloads.

### 13.6 Temp Path Construction Should Not Assume Fixed 128 Bytes

Current recommendation uses `char tmpPath[128]`. Today’s known paths are short, but `writeBufferToFile()` accepts variable relative paths and future features may grow path length.

Recommended approach:

- Prefer `snprintf` with a larger constant tied to platform limits, or
- Allocate `strlen(path) + 5` (`.tmp` + null) dynamically when available.

At minimum, keep overflow checks (already included) and explicitly document expected path limits.

### 13.7 Concurrency Guard (Future-Proofing)

Current firmware is effectively single-threaded for these paths, but if save operations are later triggered from multiple contexts (timers + web handlers + async jobs), two writers to the same target may race on the same `.tmp` path.

Recommend adding one of:

- lightweight per-file lock/flag around atomic writes, or
- global persistence mutex in Mbed builds.

This is a **future-proofing** recommendation, not a current blocker.

### 13.8 Suggested Priority Delta

Add these to implementation order immediately after helper creation:

1. Migrate `flushBufferedNotes()` to direct-rename + return checks.
2. Migrate `pruneNoteBufferIfNeeded()` to the same pattern.
3. Update inventory counts and statuses (SAFE → PARTIALLY SAFE where applicable).
4. Make helper binary-safe (`wb` / `uint8_t*`) before migrating `writeBufferToFile()`.

---

## 13. Further Recommendations (AI Review - Feb 19, 2026 — GPT-5.1-Codex-Max)

- **fsync the parent directory on POSIX after rename:** For the POSIX/Mbed path, call `fsync(dirfd)` on the parent directory (`/fs`) after a successful `rename()` to ensure the directory entry update is durable. Gate it with `#ifdef` so the LittleFS/Arduino branch skips it.
- **Surface ENOSPC/EIO to callers and clean up tmp:** If `fwrite`/`rename` fails due to full flash or I/O errors, bubble the error to the UI/log, remove the `.tmp` to free space, and avoid silent fallbacks to defaults. This prevents cascading failures when storage stays full.
- **Serialize concurrent writers per path:** Protect each save with a lightweight mutex/flag (or disable periodic saves while a user save is in-flight) to prevent two writers from racing on the same target and unintentionally deleting each other's `.tmp` files.
- **Reuse scratch buffers to avoid heap fragmentation:** For Pattern B writers, prefer a reusable static buffer (or a single preallocated `std::string`/`String` with `reserve`) instead of repeated concatenation allocations. This keeps heap usage predictable on long uptimes.
- **Ensure temp path stays on the same volume:** Keep the `.tmp` in the same directory as the target (already implied) and guard against callers passing absolute paths on different mounts; `rename` across volumes is not atomic and will fail.
- **Add a multi-file coherence marker where configs span files:** For flows that update more than one file as a logical transaction (e.g., registry + metadata), write a small `version/tag` file last, and validate it on boot to detect mixed-version states after a brownout.
- **Test rename failure explicitly:** Add a test that fills `/fs` to just below capacity, performs an atomic save, and asserts: (a) `rename` fails cleanly, (b) original file remains intact, (c) the `.tmp` is removed or recoverable, and (d) subsequent saves still succeed after space is freed.

---

## 14. Comprehensive Implementation Plan — Adopted Recommendations

**Date:** February 19, 2026  
**Status:** APPROVED FOR IMPLEMENTATION  
**Estimated Total Effort:** 3–4 hours  

This section consolidates all recommendations from Sections 1–13 into a single, sequenced implementation plan with concrete code examples. Each item includes the rationale, acceptance criteria, and exact code changes.

---

### 14.1 Recommendations Adoption Summary

| Source | Recommendation | Verdict | Rationale |
|--------|---------------|---------|-----------|
| §5 Phase 1 | POSIX atomic write helper in `TankAlarm_Platform.h` | **ADOPT** | Core building block; proven pattern already in codebase |
| §5 Phase 2 | Replace `posix_write_file()` body in Server | **ADOPT** | Single-line change protects 2 CRITICAL files instantly |
| §5 Phase 3 | Migrate individual save functions (Pattern A/B/C) | **ADOPT** | Covers all 13+ write sites systematically |
| §5 Phase 4 | LittleFS (non-Mbed) branch helper | **ADOPT** | Needed for STM32duino builds without POSIX |
| §5 Phase 5 | `.tmp` recovery on boot | **ADOPT** | Catches edge case between remove→rename and rename failures |
| §12.1 | **Drop `remove()` before `rename()`** on Mbed/LittleFS | **ADOPT (CRITICAL)** | `remove()` before `rename()` creates an explicit data-loss window; LittleFS `rename()` is atomic with overwrite |
| §12.2 | Ensure `fclose` for sync, not just `fflush` | **ADOPT** | Helper already uses `fflush` then `fclose`; document that `fclose` is the sync point |
| §12.3 | `O_EXCL` temp file creation flags | **SKIP** | Single-threaded; `fopen("w")` is sufficient |
| §12.4 | Verify `rename()` return value everywhere | **ADOPT** | Existing `flushBufferedNotes` and `pruneNoteBufferIfNeeded` don't check |
| §13.1 | Fix inventory count mismatch (15 vs 13) | **ADOPT** | Editorial correction for tracking accuracy |
| §13.2 | Add `pruneNoteBufferIfNeeded()` to write inventory | **ADOPT** | Missed write site with same `remove→rename` vulnerability |
| §13.3 | Reclassify `flushBufferedNotes()` as PARTIALLY SAFE | **ADOPT** | Uses `remove()` before `rename()` without checking rename result |
| §13.4 | Add rename result checks to note-buffer flows | **ADOPT** | Prevents silent data loss in existing "safe" code |
| §13.5 | Make atomic helper binary-safe (`"wb"` / `uint8_t*`) | **ADOPT** | `writeBufferToFile()` writes binary FTP payloads |
| §13.6 | Dynamic temp path allocation | **PARTIALLY ADOPT** | Use `strlen(path) + 5` with stack alloc via `alloca` or increase constant to 256 |
| §13.7 | Concurrency guard (mutex) | **DEFER** | Single-threaded today; revisit if async save paths are added |
| §13 GPT | `fsync` parent directory after rename | **SKIP** | LittleFS log structure handles metadata durability internally |
| §13 GPT | Surface ENOSPC/EIO to callers and clean up `.tmp` | **ADOPT** | Critical for field debugging; prevents silent cascading failures |
| §13 GPT | Reuse scratch buffers for Pattern B | **PARTIALLY ADOPT** | Use `String::reserve()` to reduce fragmentation |
| §13 GPT | Multi-file coherence marker | **DEFER** | Complexity not justified; no current multi-file atomic transactions |
| §13 GPT | Test rename failure under full flash | **ADOPT** | Add to testing strategy |

---

### 14.2 Implementation Sequence (10 Steps)

```
STEP 1 ── Add POSIX atomic write helper to TankAlarm_Platform.h
STEP 2 ── Add LittleFS atomic write helper to TankAlarm_Platform.h
STEP 3 ── Replace posix_write_file() body in Server .ino  →  2 CRITICAL files protected
STEP 4 ── Migrate saveServerConfig() (POSIX + LittleFS)   →  3rd CRITICAL
STEP 5 ── Migrate saveClientConfig() in Client .ino        →  4th CRITICAL
STEP 6 ── Migrate saveContactsConfig()                     →  5th CRITICAL
STEP 7 ── Fix flushBufferedNotes() & pruneNoteBufferIfNeeded() remove→rename
STEP 8 ── Migrate remaining HIGH-priority (S8, S12)
STEP 9 ── Migrate MEDIUM-priority (S3, S5, S10, S4, S13)
STEP 10 ── Add recoverOrphanedTmpFiles() to initializeStorage()
```

---

### 14.3 STEP 1 — POSIX Atomic Write Helper

**File:** `TankAlarm-112025-Common/src/TankAlarm_Platform.h`  
**Location:** Inside `#ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE` block, after `tankalarm_posix_log_error()`  
**Effort:** ~30 lines

Key design decisions vs. original proposal:
- **No `remove()` before `rename()`** (adopts §12.1): LittleFS `rename()` atomically overwrites the target
- **Uses `"wb"` mode** (adopts §13.5): Binary-safe for `writeBufferToFile()` and text callers alike
- **Dynamic temp path sizing** (adopts §13.6): Uses `strlen(path) + 5` with a 256-byte stack limit
- **Surfaces errors to Serial** (adopts §13 GPT ENOSPC): Always logs failures, not just in DEBUG_MODE
- **Cleans up `.tmp` on failure** (adopts §12.4, §13 GPT): Prevents flash exhaustion over time

```cpp
// ── Add after tankalarm_posix_log_error(), still inside
//    #ifdef TANKALARM_POSIX_FILE_IO_AVAILABLE ──

/**
 * Atomic file write using write-to-temp-then-rename pattern.
 * Prevents data loss if power fails during a save operation.
 *
 * On LittleFS (Mbed OS), rename() atomically replaces the target file.
 * If power fails during fwrite(), the original file is untouched.
 * If power fails during rename(), LittleFS recovers to old or new state.
 *
 * Binary-safe: opens temp file in "wb" mode.  Text callers can pass
 * string bytes directly — the only difference on this platform is that
 * no newline translation occurs (LittleFS has none anyway).
 *
 * @param path   Target file path (e.g., "/fs/server_config.json")
 * @param data   Data buffer to write (text or binary)
 * @param len    Number of bytes to write
 * @return true on success, false on any error (original file preserved)
 */
static inline bool tankalarm_posix_write_file_atomic(const char *path,
                                                      const char *data,
                                                      size_t len)
{
    // Build temp path: append ".tmp" suffix
    size_t pathLen = strlen(path);
    if (pathLen == 0 || pathLen > 250) {  // 250 + 4 (.tmp) + 1 (null) = 255
        Serial.println(F("atomic write: path too long or empty"));
        return false;
    }
    char tmpPath[256];
    memcpy(tmpPath, path, pathLen);
    memcpy(tmpPath + pathLen, ".tmp", 5);  // includes null terminator

    // Step 1: Write data to temporary file
    FILE *fp = fopen(tmpPath, "wb");
    if (!fp) {
        tankalarm_posix_log_error("atomic:fopen", tmpPath);
        Serial.print(F("atomic write failed (fopen): "));
        Serial.println(path);
        return false;
    }

    size_t written = fwrite(data, 1, len, fp);
    int writeErr = ferror(fp);

    // fclose() on Mbed OS flushes all buffers and syncs to storage.
    // No separate fsync() needed on this platform.
    fclose(fp);

    if (writeErr || written != len) {
        tankalarm_posix_log_error("atomic:fwrite", tmpPath);
        Serial.print(F("atomic write failed (fwrite): "));
        Serial.println(path);
        remove(tmpPath);  // Clean up partial temp file to free flash space
        return false;
    }

    // Step 2: Atomic rename — replaces target in one operation.
    // LittleFS rename() atomically handles overwriting an existing target.
    // Do NOT call remove(path) first — that creates a data-loss window.
    if (rename(tmpPath, path) != 0) {
        tankalarm_posix_log_error("atomic:rename", path);
        Serial.print(F("atomic write failed (rename): "));
        Serial.println(path);
        // Leave tmpPath on disk — recovery code can complete the rename on boot.
        // If flash space is critical, uncomment: remove(tmpPath);
        return false;
    }

    return true;
}
```

**Acceptance criteria:**
- [x] No `remove()` before `rename()`
- [x] Opens with `"wb"` (binary-safe)
- [x] Cleans up `.tmp` on write failure
- [x] Logs errors to Serial (not only DEBUG_MODE)
- [x] Temp path uses `strlen(path) + 5`, capped at 256
- [x] Returns false on any failure; original file untouched

---

### 14.4 STEP 2 — LittleFS Atomic Write Helper

**File:** `TankAlarm-112025-Common/src/TankAlarm_Platform.h`  
**Location:** New `#ifdef` block after the POSIX block (before `#endif // TANKALARM_PLATFORM_H`)  
**Effort:** ~20 lines

```cpp
// ── Add after #endif // TANKALARM_POSIX_FILE_IO_AVAILABLE,
//    before #endif // TANKALARM_PLATFORM_H ──

// ============================================================================
// LittleFS Atomic File Write (non-POSIX STM32duino builds)
// ============================================================================
#if defined(TANKALARM_FILESYSTEM_AVAILABLE) && !defined(TANKALARM_POSIX_FILE_IO_AVAILABLE)

/**
 * Atomic file write for Arduino LittleFS (STM32duino).
 * Same write-to-temp-then-rename pattern as the POSIX version.
 *
 * @param path   Target file path (e.g., "/client_config.json")
 * @param data   Data buffer to write
 * @param len    Number of bytes to write
 * @return true on success, false on any error (original file preserved)
 */
static bool tankalarm_littlefs_write_file_atomic(const char *path,
                                                  const uint8_t *data,
                                                  size_t len)
{
    String tmpPath = String(path) + ".tmp";

    File tmp = LittleFS.open(tmpPath.c_str(), "w");
    if (!tmp) {
        Serial.print(F("atomic write failed (open): "));
        Serial.println(path);
        return false;
    }

    size_t written = tmp.write(data, len);
    tmp.close();

    if (written != len) {
        Serial.print(F("atomic write failed (write): "));
        Serial.println(path);
        LittleFS.remove(tmpPath.c_str());
        return false;
    }

    // LittleFS.rename() atomically overwrites existing target on STM32.
    // Do NOT call LittleFS.remove(path) first.
    if (!LittleFS.rename(tmpPath.c_str(), path)) {
        Serial.print(F("atomic write failed (rename): "));
        Serial.println(path);
        return false;
    }

    return true;
}

#endif // TANKALARM_FILESYSTEM_AVAILABLE && !TANKALARM_POSIX_FILE_IO_AVAILABLE
```

---

### 14.5 STEP 3 — Replace `posix_write_file()` in Server

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Line ~801  
**Impact:** Instantly protects S6 (`saveTankRegistry` — CRITICAL) and S7 (`saveClientMetadataCache` — HIGH)

```cpp
// ── BEFORE (current implementation, line ~801): ──
static bool posix_write_file(const char *path, const char *data, size_t len) {
  FILE *fp = fopen(path, "w");
  if (!fp) {
    posix_log_error("fopen", path);
    return false;
  }
  size_t written = fwrite(data, 1, len, fp);
  int writeErr = ferror(fp);
  fclose(fp);
  if (writeErr || written != len) {
    posix_log_error("fwrite", path);
    return false;
  }
  return true;
}

// ── AFTER (atomic delegation): ──
static bool posix_write_file(const char *path, const char *data, size_t len) {
  return tankalarm_posix_write_file_atomic(path, data, len);
}
```

No callers change. Both `saveTankRegistry()` (line ~7430) and `saveClientMetadataCache()` (line ~7567) continue calling `posix_write_file()` exactly as before.

---

### 14.6 STEP 4 — Migrate `saveServerConfig()` (CRITICAL)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Line ~2661  
**Risk reduction:** Server config loss requires full manual reconfiguration in the field

#### POSIX branch (Mbed OS)

```cpp
// ── BEFORE (lines ~2661-2681): ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations
    FILE *file = fopen("/fs/server_config.json", "w");
    if (!file) {
      Serial.println(F("Failed to open server config for write"));
      return false;
    }
    
    // Serialize to buffer first, then write
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      fclose(file);
      Serial.println(F("Failed to serialize server config"));
      return false;
    }
    
    size_t written = fwrite(jsonStr.c_str(), 1, jsonStr.length(), file);
    fclose(file);
    if (written != jsonStr.length()) {
      Serial.println(F("Failed to write server config (incomplete)"));
      return false;
    }
    return true;

// ── AFTER: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations — atomic write-to-temp-then-rename
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      Serial.println(F("Failed to serialize server config"));
      return false;
    }
    if (!tankalarm_posix_write_file_atomic("/fs/server_config.json",
                                            jsonStr.c_str(), jsonStr.length())) {
      Serial.println(F("Failed to write server config"));
      return false;
    }
    return true;
```

#### LittleFS branch (STM32duino)

```cpp
// ── BEFORE: ──
  #else
    File file = LittleFS.open(SERVER_CONFIG_PATH, "w");
    if (!file) {
      Serial.println(F("Failed to open server config for write"));
      return false;
    }
    if (serializeJson(doc, file) == 0) {
      file.close();
      Serial.println(F("Failed to serialize server config"));
      return false;
    }
    file.close();
    return true;
  #endif

// ── AFTER: ──
  #else
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      Serial.println(F("Failed to serialize server config"));
      return false;
    }
    if (!tankalarm_littlefs_write_file_atomic(SERVER_CONFIG_PATH,
            (const uint8_t *)jsonStr.c_str(), jsonStr.length())) {
      Serial.println(F("Failed to write server config"));
      return false;
    }
    return true;
  #endif
```

**Net effect:** Code is shorter (removed manual fopen/fwrite/fclose), and the server config file can never be left truncated.

---

### 14.7 STEP 5 — Migrate `saveConfigToFlash()` in Client (CRITICAL)

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Line ~1738  
**Risk reduction:** Client config loss requires field visit or OTA re-provision

#### POSIX branch

```cpp
// ── BEFORE (lines ~1738-1758): ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS file operations
    FILE *file = fopen("/fs/client_config.json", "w");
    if (!file) {
      Serial.println(F("Failed to open config for writing"));
      return false;
    }
    
    // Serialize to buffer first, then write
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      fclose(file);
      Serial.println(F("Failed to serialize config"));
      return false;
    }
    
    size_t written = fwrite(jsonStr.c_str(), 1, jsonStr.length(), file);
    fclose(file);
    if (written != jsonStr.length()) {
      Serial.println(F("Failed to write config (incomplete)"));
      return false;
    }
    return true;

// ── AFTER: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    // Mbed OS — atomic write-to-temp-then-rename
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      Serial.println(F("Failed to serialize config"));
      return false;
    }
    if (!tankalarm_posix_write_file_atomic("/fs/client_config.json",
                                            jsonStr.c_str(), jsonStr.length())) {
      Serial.println(F("Failed to write config"));
      return false;
    }
    return true;
```

#### LittleFS branch

```cpp
// ── BEFORE: ──
  #else
    File file = LittleFS.open(CLIENT_CONFIG_PATH, "w");
    if (!file) {
      Serial.println(F("Failed to open config for writing"));
      return false;
    }
    if (serializeJson(doc, file) == 0) {
      file.close();
      Serial.println(F("Failed to serialize config"));
      return false;
    }
    file.close();
    return true;
  #endif

// ── AFTER: ──
  #else
    String jsonStr;
    size_t len = serializeJson(doc, jsonStr);
    if (len == 0) {
      Serial.println(F("Failed to serialize config"));
      return false;
    }
    if (!tankalarm_littlefs_write_file_atomic(CLIENT_CONFIG_PATH,
            (const uint8_t *)jsonStr.c_str(), jsonStr.length())) {
      Serial.println(F("Failed to write config"));
      return false;
    }
    return true;
  #endif
```

---

### 14.8 STEP 6 — Migrate `saveContactsConfig()` (CRITICAL)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Line ~8574

#### POSIX branch

```cpp
// ── BEFORE (lines ~8582-8592): ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) return false;
    FILE *file = fopen("/fs/contacts_config.json", "w");
    if (!file) {
      return false;
    }
    size_t written = fwrite(output.c_str(), 1, output.length(), file);
    fclose(file);
    return written == output.length();

// ── AFTER: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) return false;
    return tankalarm_posix_write_file_atomic("/fs/contacts_config.json",
                                              output.c_str(), output.length());
```

#### LittleFS branch

```cpp
// ── BEFORE: ──
  #else
    File file = LittleFS.open(CONTACTS_CONFIG_PATH, "w");
    if (!file) {
      return false;
    }
    size_t written = file.print(output);
    file.close();
    return written == output.length();
  #endif

// ── AFTER: ──
  #else
    return tankalarm_littlefs_write_file_atomic(CONTACTS_CONFIG_PATH,
            (const uint8_t *)output.c_str(), output.length());
  #endif
```

---

### 14.9 STEP 7 — Fix `flushBufferedNotes()` and `pruneNoteBufferIfNeeded()` (§13.3, §13.4)

These functions already use temp-file-then-rename but have two bugs:
1. They call `remove()` before `rename()`, creating a data-loss window
2. They don't check the `rename()` return value

#### 14.9.1 `flushBufferedNotes()` POSIX branch fix

**File:** `TankAlarm-112025-Client-BluesOpta.ino`  
**Location:** Line ~4660 (the `remove/rename` block after `fclose(tmp)`)

```cpp
// ── BEFORE: ──
    if (wroteFailures) {
      remove("/fs/pending_notes.log");
      rename("/fs/pending_notes.tmp", "/fs/pending_notes.log");
    } else {
      remove("/fs/pending_notes.log");
      remove("/fs/pending_notes.tmp");
    }

// ── AFTER: ──
    if (wroteFailures) {
      // Atomic rename — LittleFS handles overwrite; do NOT remove() first
      if (rename("/fs/pending_notes.tmp", "/fs/pending_notes.log") != 0) {
        Serial.println(F("WARNING: note buffer rename failed"));
        // tmp file preserved for recovery; original .log may still exist
      }
    } else {
      // All notes sent successfully — remove both files
      remove("/fs/pending_notes.log");
      remove("/fs/pending_notes.tmp");
    }
```

#### 14.9.2 `flushBufferedNotes()` LittleFS branch fix

```cpp
// ── BEFORE: ──
    if (wroteFailures) {
      LittleFS.remove(NOTE_BUFFER_PATH);
      LittleFS.rename(NOTE_BUFFER_TEMP_PATH, NOTE_BUFFER_PATH);
    } else {
      LittleFS.remove(NOTE_BUFFER_PATH);
      LittleFS.remove(NOTE_BUFFER_TEMP_PATH);
    }

// ── AFTER: ──
    if (wroteFailures) {
      if (!LittleFS.rename(NOTE_BUFFER_TEMP_PATH, NOTE_BUFFER_PATH)) {
        Serial.println(F("WARNING: note buffer rename failed"));
      }
    } else {
      LittleFS.remove(NOTE_BUFFER_PATH);
      LittleFS.remove(NOTE_BUFFER_TEMP_PATH);
    }
```

#### 14.9.3 `pruneNoteBufferIfNeeded()` POSIX branch fix

**Location:** Line ~4823

```cpp
// ── BEFORE: ──
    remove("/fs/pending_notes.log");
    rename("/fs/pending_notes.tmp", "/fs/pending_notes.log");
    Serial.println(F("Note buffer pruned"));

// ── AFTER: ──
    if (rename("/fs/pending_notes.tmp", "/fs/pending_notes.log") != 0) {
      Serial.println(F("WARNING: note prune rename failed"));
      // tmp preserved for recovery; original was not removed
    } else {
      Serial.println(F("Note buffer pruned"));
    }
```

#### 14.9.4 `pruneNoteBufferIfNeeded()` LittleFS branch fix

Apply the same pattern: remove the `LittleFS.remove()` call before `LittleFS.rename()`, and add a success check on the rename result.

---

### 14.10 STEP 8 — Migrate Remaining HIGH-Priority Functions

#### 14.10.1 S8: `saveClientConfigSnapshots()` — Pattern B2 (malloc buffer)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, line ~7997  
**Rationale:** This is an iterative `fprintf` writer; it needs to accumulate output first.

##### POSIX branch

```cpp
// ── BEFORE: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }
    
    FILE *file = fopen("/fs/client_config_cache.txt", "w");
    if (!file) {
      return;
    }

    for (uint8_t i = 0; i < gClientConfigCount; ++i) {
      if (fprintf(file, "%s\t%s\t%d\t%s\t%.0f\t%s\n",
                  gClientConfigs[i].uid,
                  gClientConfigs[i].payload,
                  gClientConfigs[i].pendingDispatch ? 1 : 0,
                  gClientConfigs[i].configVersion,
                  gClientConfigs[i].lastAckEpoch,
                  gClientConfigs[i].lastAckStatus) < 0) {
        Serial.println(F("Failed to write client config cache"));
        fclose(file);
        remove("/fs/client_config_cache.txt");
        return;
      }
    }

    fclose(file);

// ── AFTER: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    if (!mbedFS) {
      return;
    }

    // Accumulate output into a buffer first, then write atomically.
    // Each line: uid(32) + tab + payload(~512) + tab + flag(1) + tab
    //            + version(16) + tab + epoch(12) + tab + status(16) + newline
    // ≈ 600 bytes per entry. Reserve generously.
    const size_t bufSize = (size_t)gClientConfigCount * 640 + 64;
    char *buf = (char *)malloc(bufSize);
    if (!buf) {
      Serial.println(F("ERROR: Cannot allocate config cache buffer"));
      return;
    }

    size_t pos = 0;
    for (uint8_t i = 0; i < gClientConfigCount; ++i) {
      int n = snprintf(buf + pos, bufSize - pos, "%s\t%s\t%d\t%s\t%.0f\t%s\n",
                       gClientConfigs[i].uid,
                       gClientConfigs[i].payload,
                       gClientConfigs[i].pendingDispatch ? 1 : 0,
                       gClientConfigs[i].configVersion,
                       gClientConfigs[i].lastAckEpoch,
                       gClientConfigs[i].lastAckStatus);
      if (n < 0 || pos + (size_t)n >= bufSize) {
        Serial.println(F("Config cache buffer overflow"));
        break;
      }
      pos += (size_t)n;
    }

    if (!tankalarm_posix_write_file_atomic("/fs/client_config_cache.txt", buf, pos)) {
      Serial.println(F("Failed to write client config cache"));
    }
    free(buf);
```

#### 14.10.2 S12: `saveCalibrationData()` — Pattern B1 (String accumulation)

**File:** `TankAlarm-112025-Server-BluesOpta.ino`, line ~9660  
**Rationale:** Small file (~20 entries × ~80 bytes = ~1.6KB), String accumulation is fine.

##### POSIX branch

```cpp
// ── BEFORE: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    FILE *file = fopen("/fs/calibration_data.txt", "w");
    if (!file) return;
    
    for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
      TankCalibration &cal = gTankCalibrations[i];
      fprintf(file, "%s\t%d\t%.6f\t%.2f\t%.4f\t%d\t%d\t%.0f\t%.6f\t%d\t%d\n",
              cal.clientUid, cal.tankNumber, cal.learnedSlope, cal.learnedOffset,
              cal.rSquared, cal.entryCount, cal.hasLearnedCalibration ? 1 : 0,
              cal.lastCalibrationEpoch, cal.learnedTempCoef, 
              cal.hasTempCompensation ? 1 : 0, cal.tempEntryCount);
    }
    
    fclose(file);

// ── AFTER: ──
  #if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
    String output;
    output.reserve(gTankCalibrationCount * 100);  // ~80 bytes/line + margin
    for (uint8_t i = 0; i < gTankCalibrationCount; ++i) {
      TankCalibration &cal = gTankCalibrations[i];
      char line[128];
      snprintf(line, sizeof(line), "%s\t%d\t%.6f\t%.2f\t%.4f\t%d\t%d\t%.0f\t%.6f\t%d\t%d\n",
               cal.clientUid, cal.tankNumber, cal.learnedSlope, cal.learnedOffset,
               cal.rSquared, cal.entryCount, cal.hasLearnedCalibration ? 1 : 0,
               cal.lastCalibrationEpoch, cal.learnedTempCoef,
               cal.hasTempCompensation ? 1 : 0, cal.tempEntryCount);
      output += line;
    }
    tankalarm_posix_write_file_atomic("/fs/calibration_data.txt",
                                      output.c_str(), output.length());
```

---

### 14.11 STEP 9 — Migrate MEDIUM-Priority Functions

Apply the same patterns. Quick reference for each:

| ID | Function | Pattern | Notes |
|----|----------|---------|-------|
| S3 | `writeBufferToFile()` | C (binary) | Already has `uint8_t*` buffer; one-liner: `return tankalarm_posix_write_file_atomic(fullPath, (const char *)data, len);` |
| S5 | `saveHistorySettings()` | A (serialize first) | Same as saveServerConfig pattern; String → atomic helper |
| S10 | `saveEmailFormat()` | A (serialize first) | Already serializes to String `output`; swap fopen/fwrite/fclose for atomic helper |
| S4 | `ftpRestoreClientConfigs()` | B2 (malloc) | Iterative writer; same approach as S8 |
| S13 | Calibration log rewrite | B1 (String) | Read→filter→accumulate→atomic write |
| S2 | `saveServerHeartbeatEpoch()` | A (serialize first) | LOW risk but migrate for consistency |

#### Example: S5 `saveHistorySettings()` POSIX branch (Pattern A)

```cpp
// ── BEFORE: ──
    FILE *file = fopen("/fs/history_settings.json", "w");
    if (file) {
      size_t expected = output.length();
      size_t written = fwrite(output.c_str(), 1, expected, file);
      fclose(file);
      if (written != expected) {
        remove("/fs/history_settings.json");
      }
    }

// ── AFTER: ──
    if (!tankalarm_posix_write_file_atomic("/fs/history_settings.json",
                                            output.c_str(), output.length())) {
      Serial.println(F("Failed to write history settings"));
    }
```

#### Example: S10 `saveEmailFormat()` POSIX branch (Pattern A)

```cpp
// ── BEFORE: ──
  FILE *file = fopen("/fs/email_format.json", "w");
  if (!file) {
    return false;
  }
  size_t expected = output.length();
  size_t written = fwrite(output.c_str(), 1, expected, file);
  fclose(file);
  return written == expected;

// ── AFTER: ──
  return tankalarm_posix_write_file_atomic("/fs/email_format.json",
                                            output.c_str(), output.length());
```

#### Example: S3 `writeBufferToFile()` POSIX branch (Pattern C — binary)

```cpp
// ── BEFORE: ──
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  FILE *file = fopen(fullPath, "wb");
  if (!file) {
    return false;
  }
  size_t written = fwrite(data, 1, len, file);
  fclose(file);
  return written == len;

// ── AFTER: ──
#if defined(ARDUINO_OPTA) || defined(ARDUINO_ARCH_MBED)
  return tankalarm_posix_write_file_atomic(fullPath, (const char *)data, len);
```

---

### 14.12 STEP 10 — Add Boot-Time `.tmp` Recovery

**File:** `TankAlarm-112025-Server-BluesOpta.ino`  
**Location:** Inside `initializeStorage()`, after successful filesystem mount (line ~2401)  
**Also add to:** `TankAlarm-112025-Client-BluesOpta.ino` `initializeStorage()`

This covers the edge case where `rename()` itself fails (e.g., power loss during the rename metadata update, flash I/O error).

```cpp
/**
 * Recover from interrupted atomic writes.
 * Called once during initializeStorage() after filesystem mount.
 *
 * If a .tmp file exists but the target does NOT, the rename failed
 * after a write completed — complete the rename now.
 * If BOTH exist, the original is still valid — delete stale .tmp.
 */
static void recoverOrphanedTmpFiles() {
  static const char * const criticalFiles[] = {
    "/fs/server_config.json",
    "/fs/contacts_config.json",
    "/fs/tank_registry.json",
    "/fs/client_metadata.json",
    "/fs/client_config_cache.txt",
    "/fs/calibration_data.txt",
    "/fs/email_format.json",
    "/fs/history_settings.json",
    "/fs/server_heartbeat.json",
    nullptr
  };

  for (int i = 0; criticalFiles[i] != nullptr; ++i) {
    char tmpPath[256];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", criticalFiles[i]);

    if (tankalarm_posix_file_exists(tmpPath)) {
      if (!tankalarm_posix_file_exists(criticalFiles[i])) {
        // Target missing + tmp exists → rename was interrupted; complete it
        if (rename(tmpPath, criticalFiles[i]) == 0) {
          Serial.print(F("Recovered config from .tmp: "));
          Serial.println(criticalFiles[i]);
        } else {
          Serial.print(F("ERROR: Could not recover: "));
          Serial.println(criticalFiles[i]);
        }
      } else {
        // Both exist → original is valid; clean up stale tmp
        remove(tmpPath);
        #ifdef DEBUG_MODE
        Serial.print(F("Cleaned stale .tmp: "));
        Serial.println(tmpPath);
        #endif
      }
    }
  }
}
```

**Call site in `initializeStorage()`:**

```cpp
// ── In Server initializeStorage(), after "Mbed OS LittleFileSystem initialized": ──
    if (mbedFS) {
      Serial.println(F("Mbed OS LittleFileSystem initialized"));
      recoverOrphanedTmpFiles();  // ← ADD THIS LINE
    }
```

For the **Client**, add a similar recovery list containing only client files:

```cpp
  static const char * const criticalFiles[] = {
    "/fs/client_config.json",
    "/fs/pending_notes.log",
    nullptr
  };
```

---

### 14.13 Corrected Write Inventory (§13.1 Fix)

The original Section 3.1 header stated "15 operations across 11 functions" but listed IDs S1–S13 (13 operations). Adding the missed `pruneNoteBufferIfNeeded()` from §13.2, the corrected totals are:

| Scope | Write Sites | Functions |
|-------|------------|-----------|
| Server | 13 operations | 11 functions (S1–S13) |
| Client | 4 operations | 4 functions (C1–C4, adding `pruneNoteBufferIfNeeded` as C4) |
| **Total** | **17 operations** | **15 functions** |

Updated client table entry:

| # | Function | File Written | Risk | Pattern |
|---|----------|-------------|------|---------|
| C4 | `pruneNoteBufferIfNeeded()` | `/fs/pending_notes.tmp` → rename | **PARTIALLY SAFE** | Temp-write-then-rename, but uses `remove()` before `rename()` |

And `flushBufferedNotes()` (C3) should be reclassified from SAFE to **PARTIALLY SAFE** until Step 7 is applied.

---

### 14.14 Testing Additions (§13 GPT)

Add these test scenarios to the existing Section 7 testing strategy:

1. **Rename failure under full flash:** Fill `/fs` to near capacity, trigger a save, verify:
   - `rename()` fails cleanly (does not hang or crash)
   - Original file remains intact and loadable
   - `.tmp` file is removed by error-handling code (or recoverable on boot)
   - After freeing space, subsequent saves succeed normally

2. **Binary payload round-trip:** Write a binary payload via `writeBufferToFile()` using the atomic helper, read it back, and confirm byte-for-byte equality with no text-mode newline translation artifacts.

3. **Boot recovery verification:** Manually create orphaned `.tmp` files (with and without matching originals), reboot, and verify `recoverOrphanedTmpFiles()` handles each case correctly.

---

### 14.15 Files Changed Summary

| File | Changes | Net LOC Impact |
|------|---------|----------------|
| `TankAlarm_Platform.h` | +POSIX atomic helper (~35 lines), +LittleFS atomic helper (~25 lines) | **+60** |
| Server `.ino` | Replace `posix_write_file` body, migrate 9 save functions (POSIX+LittleFS), add `recoverOrphanedTmpFiles()` | **−50 to −80** (boilerplate removed) |
| Client `.ino` | Migrate `saveConfigToFlash` (POSIX+LittleFS), fix `flushBufferedNotes` + `pruneNoteBufferIfNeeded` rename patterns | **−10 to −20** |
| Viewer `.ino` | **No changes** | 0 |

**Total net impact:** Approximately **−20 to −40 lines** of code (atomic helpers add ~60 lines in header, but eliminating repeated fopen/fwrite/fclose boilerplate across 12+ call sites saves more).

---

### 14.16 Risk Assessment for This Refactor

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Regression in save logic | Low | HIGH | Each migration is mechanical; test save+load round-trip for every function |
| LittleFS `rename()` doesn't overwrite on some board revision | Very Low | Medium | Already proven working in `flushBufferedNotes()`; recovery code handles edge case |
| `String` accumulation OOM for Pattern B1 | Very Low | Medium | Only used for small files (<2KB); `reserve()` pre-allocates; `malloc` used for larger files |
| `malloc` failure for Pattern B2 | Low | Medium | Already used by `saveTankRegistry`/`saveClientMetadataCache`; checked with null test |
| Build break from header change | Very Low | Low | Helper is `static inline`; no linker conflicts; guarded by existing `#ifdef` |

---

*End of Section 14 — Comprehensive Implementation Plan*

