# Per-File Retry Logic — Implementation Plan

**Date:** 2026-04-17
**Scope:** Application-side change in `TankAlarm-112025-Server-BluesOpta.ino`,
specifically inside `performFtpBackupDetailed()`. **No library changes
required.**
**Goal:** When a single file upload fails because the Opta LWIP socket
pool is briefly exhausted, retry that file rather than aborting the
entire remaining backup.

## Current Behavior (Baseline)

Inside `performFtpBackupDetailed()` — the per-file loop in
`TankAlarm-112025-Server-BluesOpta.ino`:

```cpp
if (useFtps && ftpsSessionLikelyDead(gFtpsClient.lastError())) {
  abortRemainingTransfers = true;
  // ... record errorMessage, break next iteration
}
```

Any single `STOR` failure where the FTPS session is judged dead causes
the loop to set `abortRemainingTransfers = true` and skip every
remaining file. Verified at proc=4 with `client_metadata.json` failing
on `-3005`: the next 4 files were never attempted.

This is conservative and correct for **control-channel** failures (the
control TCP socket is gone, no point trying), but it is **too
aggressive** for **data-channel** `-3005` failures. The control channel
is fine; only the data PCB is temporarily missing.

## Failure Mode Classification

| Symptom | Library reports | What actually died | Retry useful? |
| ------- | --------------- | ------------------ | ------------- |
| `xport:open-failed:-3005` | `DataConnectionFailed` | Data PCB pool empty (transient) | **Yes** — wait, retry |
| `xport:connect-failed:-3005` | `DataConnectionFailed` | Data PCB pool empty (transient) | **Yes** — wait, retry |
| `xport:data:tls-handshake` failure | `TlsHandshakeFailed` / `DataConnectionFailed` | TLS issue, often transient | **Maybe** — bounded retry |
| Control reply timeout | `ControlIoFailed` / similar | Control socket dead | **No** — re-login needed |
| Server `5xx` on STOR | `StoreFailed` | Server-side problem | **No** — won't change on retry |

The simplification: if the library returns `DataConnectionFailed`
specifically, retry. For everything else, keep current abort behavior.

## Proposed Algorithm

```
for each file:
    if i > 0: wait inter-file delay
    read file from /fs into buffer
    if read fails: log skip, continue

    attempts = 0
    max_attempts = 3
    retry_extra_wait_ms = 30000   // additional wait before each retry

    while attempts < max_attempts:
        attempts += 1
        stored = ftpsStoreBuffer(...)
        if stored:
            break

        if not is_data_pool_failure(gFtpsClient.lastError()):
            // hard failure: control dead, server 5xx, etc.
            break

        if attempts < max_attempts:
            log "FTP retry: %s attempt %d/%d after data-pool failure"
            wait retry_extra_wait_ms ms (servicing watchdog)

    if stored:
        result.filesProcessed += 1
    else:
        result.filesFailed += 1
        result.addFailedFile(entry.remoteName)

        // ONLY abort the rest of the backup if the failure looks like
        // a dead control channel. A data-pool exhaustion that survived
        // 3 attempts is bad luck on this file but the next file might
        // be fine after another inter-file wait.
        if ftpsSessionLikelyDead(gFtpsClient.lastError()) and
           not is_data_pool_failure(gFtpsClient.lastError()):
            abortRemainingTransfers = true
```

### Helper: `isDataPoolFailure()`

The library's public surface today exposes `FtpsError lastError()`.
`DataConnectionFailed` is the closest signal we have; on Opta nearly
all data-side failures are `-3005`. A first-cut helper:

```cpp
static bool isDataPoolFailure(FtpsError err) {
  return err == FtpsError::DataConnectionFailed;
}
```

Future enhancement (optional, library-side): expose
`int lastNsapiCode()` so the integrator can match `-3005` exactly and
treat other `DataConnectionFailed` codes (e.g. `-3008`
`NSAPI_ERROR_CONNECTION_TIMEOUT`) differently. Not required for v1.

### Helper: `ftpsSessionLikelyDead()` already exists

Reuse the existing `ftpsSessionLikelyDead()` helper for the *abort*
decision. The new logic is: only abort if the session is dead **and**
the failure was not a pure data-pool exhaustion.

## Constants to Add

Place near the top of the file with the other FTP defaults:

```cpp
#ifndef FTP_BACKUP_PER_FILE_MAX_ATTEMPTS
#define FTP_BACKUP_PER_FILE_MAX_ATTEMPTS 3
#endif

#ifndef FTP_BACKUP_RETRY_WAIT_MS
#define FTP_BACKUP_RETRY_WAIT_MS 30000UL
#endif
```

## Diff Footprint Estimate

- **+~25 lines** inside `performFtpBackupDetailed()`'s per-file loop
  (the `attempts < max_attempts` while loop wrapping the existing
  store call, plus the watchdog-friendly retry wait).
- **+1 helper function** (`isDataPoolFailure`).
- **+2 `#ifndef` blocks** for the new constants.
- **No library changes.**
- **No new global state.**

## Test Plan

1. **Happy path regression:** Backup with all 8 files succeeding on
   first attempt → `proc=8 failed=0`, no "FTP retry:" log lines.
2. **Forced data-pool failure:** Drop the inter-file wait to 5 s
   temporarily so file 2 reliably hits `-3005`. Expected: file 2 logs
   `FTP retry: ... attempt 2/3` and the second attempt succeeds (or
   third), backup completes. Restore the 65 s wait afterwards.
3. **Forced control failure:** Kill the pyftpdlib server mid-backup.
   Expected: next file fails, retries do NOT fire because
   `lastError()` is not `DataConnectionFailed`, `abortRemainingTransfers`
   is set, remaining files are skipped. Existing behavior preserved.
4. **Retry exhaustion:** Cap pool externally (e.g. open 3 concurrent
   browser tabs polling the dashboard) so `-3005` persists across all
   3 attempts. Expected: file marked failed, but the *next* file is
   still attempted because the failure is `DataConnectionFailed`-only.

## Why This Won't Make Things Worse

- The retry wait is bounded (`max_attempts * retry_wait = 90 s`
  worst-case extra time per failed file).
- Watchdog is serviced inside the wait loop, identical to the
  existing inter-file wait.
- If retries don't resolve the pool exhaustion, the file is marked
  failed and the loop continues — strictly more information than today,
  never less.
- Today's "control died → abort" path is preserved verbatim.

## Cross-References

- `MULTI_FILE_BACKUP_FOLLOWUPS_04172026.md` — broader optimization
  catalog. Retry is item 2 in that doc.
- `OPTA_LWIP_BACKUP_RECIPE_04172026.md` — application integration
  recipe (already references `xport:open-failed:-3005`).
- `TankAlarm-112025-Server-BluesOpta.ino` — `performFtpBackupDetailed()`
  contains `// SPEED-OPT:` markers at the points where future
  throughput tuning is safe.
