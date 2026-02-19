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
