# Per-File Retry Logic — Implementation Plan (v2, consolidated)

**Date:** 2026-04-17 (revised)
**Scope:** Bounded per-file retry inside `performFtpBackupDetailed()` in
`TankAlarm-112025-Server-BluesOpta.ino`, plus one small library-side
addition in `ArduinoOPTA-FTPS` (`lastNsapiError()`) and the removal of
one diagnostic probe.
**Goal:** When a single file upload fails because the Opta LWIP socket
pool is briefly exhausted, retry that file rather than aborting the
entire remaining backup — without retrying failures that genuinely
indicate the control channel or server is gone.

This plan supersedes the v1 draft and folds in the three independent
reviews:

- `../../ArduinoOPTA-FTPS/CODE REVIEW/FTPS_RETRY_PROPOSALS_SUMMARY_04172026.md`
- `../../ArduinoOPTA-FTPS/CODE REVIEW/FTPS_RETRY_REVIEW-OPUS4-04172026.md`
- `FTPS_FILE_RETRY_RECOMMENDATIONS_04172026_COPILOT.md`

---

## Consensus Across the Three Reviews

All three reviews independently agreed on the following changes
relative to the v1 plan:

1. **Reclassify `DataConnectionFailed` out of "session dead".** Today
   `ftpsSessionLikelyDead()` lumps it with control failures, which
   would cause the v1 retry loop to abort the batch on the very first
   data-pool failure even though it just retried.
2. **Split the classifier in two:** `ftpsSessionDead()` (abort batch)
   vs. `ftpsTransferRetriable()` (retry this file).
3. **Use stepped backoff, not flat 30 s.** Exact values vary slightly
   across reviewers (15/35 vs. 20/35–45) but the principle is the
   same: short first wait catches mid-drain TIME_WAIT, longer second
   wait catches a full TIME_WAIT cycle.
4. **Gate retries on the actual NSAPI code (`-3005`), not just the
   `FtpsError` enum,** so that `-3008` (timeout), `-3001` (DNS) and
   other overloaded-into-`DataConnectionFailed` codes are not retried.
   Requires a small library addition.
5. **Remove the post-STOR `isControlAlive()` NOOP probe.** It can
   itself kill the session and would poison retry decisions. (Opus
   review §2.1.)
6. **Add observability counters** (`filesRetried`,
   `retryAttemptsTotal`, `filesRecoveredByRetry`,
   `filesRetryExhausted`) so the inter-file delay can be tuned with
   evidence.
7. **Keep the inter-file delay at 65 s for the initial rollout.** Only
   reduce it (target 45 s) once retry telemetry shows it is safe.
8. **Add a "consecutive files exhausting retries" guard.** If two
   files in a row burn all retry attempts, abort the batch — the
   problem is systemic and burning ~90 s × N more files is wasteful.

---

## Failure Mode Classification (revised)

| Symptom | `lastError()` | `lastNsapiError()` | `lastPhase()` | Class | Action |
| ------- | ------------- | ------------------ | ------------- | ----- | ------ |
| LWIP pool empty before connect | `DataConnectionFailed` | `-3005` | `store:data-open` / `retrieve:data-open` | DataPathTransient | **Retry** |
| Data TLS handshake transient | `DataTlsHandshakeFailed` | varies | `store:data-tls` | DataPathTransient | **Retry (bounded, ≤2)** |
| Connect timeout | `DataConnectionFailed` | `-3008` | `store:data-open` | DataPathPersistent | **Skip file, no retry** |
| DNS / unreachable | `DataConnectionFailed` | `-3001` | `connect` | ControlPath | **Abort batch** |
| Control reply timeout | `ControlIoFailed` | any | `cmd:*` / `reply:*` | ControlPath | **Abort batch** |
| `connect()` failed at session start | `ConnectionFailed` | any | `connect` | ControlPath | **Abort batch** |
| TLS trust failure (control) | `TlsHandshakeFailed` | n/a | `tls:handshake` | TrustFailure | **Abort batch, no retry** |
| Server `5xx` on STOR | `StoreFailed` | n/a | `store:reply` | ServerReject | **Skip file, no retry** |
| Auth/login failure | `LoginFailed` | n/a | `auth` | TrustFailure | **Abort batch, no retry** |

The simplification: `ftpsTransferRetriable(err, nsapi)` returns `true`
only for the **Retry** rows; `ftpsSessionDead(err)` returns `true`
only for the rows marked **Abort batch**.

---

## Phased Implementation Order

### Phase 0 — Pre-cleanup (no retry behavior yet)

Safe, small changes that should land before the retry loop itself.

- **P0-1: Remove the post-STOR `isControlAlive()` probe.** In
  `ftpsStoreBuffer()` (around line 5685 of the server `.ino`), delete
  the `if (!gFtpsClient.isControlAlive(...))` block that runs after
  every successful `store()`. It mutates the control session by
  sending NOOP under the 15 s reply timeout; on a slow server reply
  this can mark the session dead even though STOR succeeded. (Opus
  review §2.1.)
- **P0-2: Split `ftpsSessionLikelyDead()` into two helpers.**

  ```cpp
  // Control gone — no point continuing the batch
  static bool ftpsSessionDead(FtpsError err) {
    return err == FtpsError::ConnectionFailed ||
           err == FtpsError::ControlIoFailed ||
           err == FtpsError::LoginFailed ||
           err == FtpsError::TlsHandshakeFailed; // control-path TLS
  }

  // Transfer-only fault — file may be retriable, session likely OK
  static bool ftpsTransferRetriable(FtpsError err, int nsapiCode) {
    if (err == FtpsError::DataConnectionFailed) {
      // -3005 = NSAPI_ERROR_NO_SOCKET — pool exhaustion (retry)
      // -3008 = NSAPI_ERROR_CONNECTION_TIMEOUT — server slow (skip)
      // -3001 = NSAPI_ERROR_DNS_FAILURE — permanent (skip)
      return nsapiCode == -3005;
    }
    if (err == FtpsError::DataTlsHandshakeFailed) {
      return true; // bounded retries handle this
    }
    return false;
  }
  ```

  Either delete `ftpsSessionLikelyDead()` and update its callers, or
  keep it as a thin wrapper that returns `ftpsSessionDead(err)`.

- **P0-3: Add `int lastNsapiError() const` to `FtpsClient`** (library
  side). The transport already records `_lastTcpError` /
  `_lastTlsError`; the client needs to expose whichever is most
  recent. Single new getter, no API breakage. Without this,
  `ftpsTransferRetriable()` cannot reliably gate on `-3005` and
  degrades to:

  ```cpp
  static bool ftpsTransferRetriable(FtpsError err, int /*nsapi*/) {
    return err == FtpsError::DataConnectionFailed
        || err == FtpsError::DataTlsHandshakeFailed;
  }
  ```

  Acceptable for v1 if we want to avoid touching the library. Coarser
  but still strictly better than today.

### Phase 1 — Per-file retry loop

Inside `performFtpBackupDetailed()`'s per-file loop, replace the
single `ftpsStoreBuffer()` call with a bounded retry block.

```cpp
// New constants near the other FTP_BACKUP_* defaults
#ifndef FTP_BACKUP_PER_FILE_MAX_ATTEMPTS
#define FTP_BACKUP_PER_FILE_MAX_ATTEMPTS 3
#endif

#ifndef FTP_BACKUP_MAX_CONSECUTIVE_FAIL_FILES
#define FTP_BACKUP_MAX_CONSECUTIVE_FAIL_FILES 2
#endif

// Stepped backoff. Index 0 used between attempt 1 and attempt 2,
// index 1 used between attempt 2 and attempt 3.
static const uint32_t kRetryBackoffMs[] = { 20000UL, 40000UL };
```

Per-file inner loop (pseudocode, ASCII-only comments):

```
attempts = 0
stored = false
while attempts < FTP_BACKUP_PER_FILE_MAX_ATTEMPTS:
    attempts += 1
    stored = ftpsStoreBuffer(...)
    if stored: break

    err   = gFtpsClient.lastError()
    nsapi = gFtpsClient.lastNsapiError()   // -1 if P0-3 not landed

    if ftpsSessionDead(err):
        abortRemainingTransfers = true
        break

    if not ftpsTransferRetriable(err, nsapi):
        break    // non-retriable data fault, skip file but keep batch

    if attempts < FTP_BACKUP_PER_FILE_MAX_ATTEMPTS:
        idx     = min(attempts - 1, len(kRetryBackoffMs) - 1)
        waitMs  = kRetryBackoffMs[idx]
        log "ftp-retry: file=%s attempt=%u/%u nsapi=%d wait=%lu"
        result.retryAttemptsTotal += 1
        watchdog-friendly delay(waitMs)

if stored:
    if attempts > 1: result.filesRecoveredByRetry += 1
    result.filesProcessed += 1
    consecutiveFailFiles = 0
else:
    result.filesFailed += 1
    result.addFailedFile(entry.remoteName)
    if attempts == FTP_BACKUP_PER_FILE_MAX_ATTEMPTS:
        result.filesRetryExhausted += 1
        consecutiveFailFiles += 1
        if consecutiveFailFiles >= FTP_BACKUP_MAX_CONSECUTIVE_FAIL_FILES:
            log "ftp-retry: aborting batch — N consecutive files exhausted retries"
            abortRemainingTransfers = true
```

Notes:
- The watchdog-friendly delay must follow the same pattern as the
  existing inter-file wait (call `serviceTransferWatchdog()` every
  ~100 ms inside the loop).
- New counters (`retryAttemptsTotal`, `filesRecoveredByRetry`,
  `filesRetryExhausted`) need to be added to `FtpBackupResult` and
  surfaced in the dashboard summary.
- The inter-file delay (`65000UL`, marked `// SPEED-OPT:` in the
  `.ino`) stays at 65 s for this phase.

### Phase 2 — Reconnect-on-retry (optional, Copilot review §3)

Only after Phase 1 has been observed in production for at least a few
backup cycles. Adds one secondary recovery step: if the second retry
attempt also fails, force a control-channel reconnect (`quit()` +
`connect()`) and try the file one final time.

Gated behind:

```cpp
#ifndef FTP_BACKUP_RECONNECT_ON_RETRY
#define FTP_BACKUP_RECONNECT_ON_RETRY 0   // off by default until Phase 1 proven
#endif
```

Risk: each reconnect itself produces TIME_WAIT entries on the control
socket. Should not be enabled unless retry telemetry shows a clear
class of failures that survive 3 data-channel retries but recover
after a reconnect.

### Phase 3 — Tune inter-file delay

Once Phase 1 telemetry is healthy:

- If `retryAttemptsTotal` is consistently 0 across many cycles,
  reduce `FTP_BACKUP_INTER_FILE_DELAY_MS` from 65000 to 45000 and
  watch for retry counter growth. If retries appear, hold or increase.
- Promote the inter-file delay from a magic literal (currently beside
  a `// SPEED-OPT:` comment) to a `#define` so it can be tuned
  without code-search regressions.

### Phase 4 — Optional library quality-of-life

- `int lastFtpReplyCode() const` — distinguishes server protocol
  rejects (`5xx`) from network failures, lets the retry classifier
  treat permission/quota errors as `Skip file, no retry` explicitly.
- Optional library failure-class enum (Copilot review §8):
  `ControlPathFailure / DataPathTransient / DataPathPermanent /
  TlsTrustFailure`. Nice-to-have; not required for v1.

---

## Rejected / Deferred Suggestions and Why

- **Bundle / single-STOR upload** (Summary §3, Opus §1.3): defer. The
  throughput case is real (656 s → ~10 s) but the restore path needs
  to be designed first, atomic-failure semantics change (one byte
  error loses all 8 files), and a 16 KB heap buffer must be validated.
  Tracked in `MULTI_FILE_BACKUP_FOLLOWUPS_04172026.md`.
- **Reconnect per file as primary strategy** (Summary §4): rejected.
  Even with the `SO_LINGER` fix landed, full TLS handshake costs
  ~1.5 s per file and the control socket also accumulates TIME_WAIT.
  Keep `FTP_BACKUP_FTPS_RECONNECT_PER_FILE_DIAG` as a debug-only
  escape hatch.
- **Truncation guard for the 2 KB file buffer** (Opus §2.4): real
  bug, but separate from retry. File a follow-up; do not bundle into
  the retry change to keep its diff small and reviewable.
- **Nightly-backup failure dashboard flag** (Opus §1.4): real gap,
  belongs in `NIGHTLY_BACKUP_PLAN_04172026.md`.

---

## Test Plan

| # | Scenario | Setup | Expected |
|---|----------|-------|----------|
| 1 | Happy path | Standard backup, 8 files | `proc=8 failed=0`, no `ftp-retry:` lines, `retryAttemptsTotal=0` |
| 2 | Forced data-pool exhaustion | Temporarily drop inter-file delay to 5 s so file 2 hits `-3005` | File 2 logs `ftp-retry: ... attempt=2/3 nsapi=-3005`, second or third attempt succeeds, `filesRecoveredByRetry >= 1` |
| 3 | Control-channel kill | Stop pyftpdlib mid-batch | Next file fails with `ControlIoFailed`, no retries fire, `abortRemainingTransfers=true`, remaining files skipped |
| 4 | Single retry exhaustion | Open 3 dashboard tabs polling so `-3005` persists across all 3 attempts on one file | That file marked failed (`filesRetryExhausted=1`), next file still attempted, batch continues |
| 5 | Consecutive retry exhaustion | Sustained pool pressure across multiple files | After 2 consecutive files exhaust retries, batch aborts with `ftp-retry: aborting batch` |
| 6 | Hard auth failure | Wrong password in config | `LoginFailed` at session start, no retries, batch aborts cleanly |
| 7 | Server 5xx on STOR | Configure pyftpdlib to deny one filename | `StoreFailed` for that file, no retries, next file attempted |
| 8 | Watchdog regression | Backup with retries firing | No watchdog reset across retry waits |

Tests 1, 2, 3 are mandatory before merging. Tests 4–8 should run once
before declaring Phase 1 complete.

---

## Diff Footprint

- **Library (`ArduinoOPTA-FTPS`):** ~5 lines for `lastNsapiError()`
  getter + glue in transport (P0-3). Optional.
- **Server `.ino` Phase 0:**
  - `-15 lines` removing `isControlAlive()` post-STOR block.
  - `+25 lines` adding `ftpsSessionDead()` /
    `ftpsTransferRetriable()` helpers (or rewriting
    `ftpsSessionLikelyDead()` in place).
- **Server `.ino` Phase 1:**
  - `+~40 lines` for the retry loop, constants, counters, and logging
    inside `performFtpBackupDetailed()`.
  - `+4 fields` on `FtpBackupResult`.
- **Server `.ino` Phase 3:**
  - `+1 #define`, `-1 magic literal`.
- **No changes to nightly-backup, web UI, or restore path in this
  work item.**

---

## Cross-References

- `MULTI_FILE_BACKUP_FOLLOWUPS_04172026.md` — broader optimization
  catalog. Retry is item 2; bundle/single-STOR is the long-term
  follow-up rejected for this iteration.
- `OPTA_LWIP_BACKUP_RECIPE_04172026.md` — application integration
  recipe; describes `xport:open-failed:-3005` as the canonical
  "pool empty" trace.
- `NIGHTLY_BACKUP_PLAN_04172026.md` — scheduled nightly backup work
  that should layer on top of the retry telemetry.
- `../../ArduinoOPTA-FTPS/CODE REVIEW/FTPS_RETRY_PROPOSALS_SUMMARY_04172026.md`
- `../../ArduinoOPTA-FTPS/CODE REVIEW/FTPS_RETRY_REVIEW-OPUS4-04172026.md`
- `FTPS_FILE_RETRY_RECOMMENDATIONS_04172026_COPILOT.md`
- `TankAlarm-112025-Server-BluesOpta.ino` —
  `performFtpBackupDetailed()` and `ftpsStoreBuffer()` are the only
  functions touched in Phases 0 and 1. `// SPEED-OPT:` markers in the
  per-file loop point at the exact lines that change in Phase 3.
