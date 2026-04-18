# FTPS File Retry Recommendations (Cross-Repo Review)

**Date:** 2026-04-17  
**Reviewer:** GitHub Copilot (GPT-5.3-Codex)  
**Repositories reviewed:**
- `ArduinoSMSTankAlarm`
- `ArduinoOPTA-FTPS`

## Goal

Provide practical improvements to the current FTPS per-file retry proposal so backups are more resilient to transient data-channel failures (especially Opta LWIP socket-pool pressure) without masking hard control-channel failures.

## Inputs Reviewed

### Tank Alarm (application side)
- `CODE REVIEW/PER_FILE_RETRY_PLAN_04172026.md`
- `CODE REVIEW/MULTI_FILE_BACKUP_FOLLOWUPS_04172026.md`
- `CODE REVIEW/NIGHTLY_BACKUP_PLAN_04172026.md`
- `CODE REVIEW/OPTA_LWIP_BACKUP_RECIPE_04172026.md`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`

### ArduinoOPTA-FTPS (library side)
- `src/FtpsClient.cpp`
- `src/FtpsClient.h`
- `src/FtpsErrors.h`
- `src/transport/MbedSecureSocketFtpsTransport.cpp`
- `CHANGELOG.md`

## What Is Already Strong In The Current Proposal

1. Correctly identifies `-3005` (`NSAPI_ERROR_NO_SOCKET`) as common transient data-channel pressure.
2. Correctly keeps retry logic application-side first (smallest change surface).
3. Correctly preserves watchdog servicing inside waits.
4. Correctly avoids forcing a full backup abort for all error classes.

## Key Improvements Recommended

## 1. Change "session dead" classification before adding retries

### Current risk
`ftpsSessionLikelyDead()` in Tank Alarm currently treats `FtpsError::DataConnectionFailed` as a dead session signal. In practice, `DataConnectionFailed` often means passive data connect failed while control can still be alive.

### Recommendation
- Remove `DataConnectionFailed` from the "dead session" bucket.
- Abort-all should be reserved for clear control-path loss (for example `ConnectionFailed` after control command/reply failure).

### Why
If `DataConnectionFailed` remains in the dead bucket, per-file retry will still prematurely stop the batch on first transient data open failure.

## 2. Retry by failure class, not by a single enum value

### Current proposal (baseline)
Retry all `DataConnectionFailed` failures.

### Improved policy
Use **`lastError + lastPhase + error text`** together:

- Retry eligible:
  - `DataConnectionFailed` with `lastPhase == "store:data-open"` or `"retrieve:data-open"`
  - `DataTlsHandshakeFailed` (bounded, low retry count)
  - `SessionReuseRequired` (reconnect once then retry once)
  - `TransferFailed` only when phase indicates data write/read path (`store:write`, `retrieve:read`)
- Not retry eligible:
  - cert/fingerprint/login/auth failures
  - explicit server command rejection that is likely permanent for that file (path/permission/quota style failures)

This avoids retrying non-transient failures while still recovering from actual socket pressure.

## 3. Add a bounded retry state machine with reconnect as a secondary step

Use this order for each file:

1. Attempt transfer once.
2. If retry-eligible failure:
   - wait with backoff (watchdog-safe), then retry.
3. If still failing and control appears unhealthy:
   - reconnect once, retry once.
4. If still failing:
   - mark file failed and continue to next file unless a true control-path hard failure is confirmed.

This produces high recovery value without unbounded runtime.

## 4. Suggested retry constants (v1)

```cpp
#ifndef FTP_BACKUP_PER_FILE_MAX_ATTEMPTS
#define FTP_BACKUP_PER_FILE_MAX_ATTEMPTS 3
#endif

#ifndef FTP_BACKUP_RETRY_WAIT_MS
#define FTP_BACKUP_RETRY_WAIT_MS 20000UL
#endif

#ifndef FTP_BACKUP_RETRY_WAIT_MAX_MS
#define FTP_BACKUP_RETRY_WAIT_MAX_MS 45000UL
#endif

#ifndef FTP_BACKUP_RECONNECT_ON_RETRY
#define FTP_BACKUP_RECONNECT_ON_RETRY 1
#endif
```

### Backoff suggestion
- Attempt 1 -> 2: wait 20 s
- Attempt 2 -> 3: wait 35-45 s (cap)
- Add small jitter (for example 0-2 s) if multiple systems may retry at once.

## 5. Keep per-file wait and retry logically separate

Do not replace inter-file pacing with retry.

- Inter-file wait controls steady-state pool pressure.
- Retry wait handles transient contention/unexpected pressure.

A good rollout is:
1. Land retry first at current 65 s inter-file wait.
2. Observe failure counters.
3. Then cautiously reduce inter-file delay (for example toward 45 s).

## 6. Add "verify on retry success" for data integrity

If a file succeeds on attempt > 1, optionally verify remote size once:

- call `size(remotePath, remoteBytes, ...)`
- accept success only if `remoteBytes == localLen`

This protects against partial transfer edge cases after transport turbulence.

## 7. Improve observability for operations and tuning

Add counters to backup result/logs:

- `filesRetried`
- `retryAttemptsTotal`
- `filesRecoveredByRetry`
- `filesRetryExhausted`
- `lastRetryReason`

Add per-attempt trace lines:

- `ftp-retry:start file=<name> attempt=<n>/<max> reason=<...>`
- `ftp-retry:wait ms=<...>`
- `ftp-retry:reconnect`
- `ftp-retry:success`
- `ftp-retry:exhausted`

This is essential to tune wait values safely.

## 8. ArduinoOPTA-FTPS library improvements to make retries safer and simpler

Current public FTPS API already exposes:
- `lastError()`
- `lastPhase()`

Recommended additions:

1. `int lastNsapiError() const`
- Expose the last transport NSAPI code (`-3005`, etc.) directly.
- Removes fragile string parsing.

2. `int lastFtpReplyCode() const`
- Distinguish protocol rejections from network failures more reliably.

3. Optional: a small failure-class enum
- `ControlPathFailure`
- `DataPathTransient`
- `DataPathPermanent`
- `TlsTrustFailure`

This would make application retry logic much cleaner and less error-prone.

## 9. Guardrails regarding close mode

Retry behavior depends on transport close behavior.

- If `FTPS_ABANDON_ON_CLOSE == 1`, reconnect-heavy strategies can become unsafe due to leak pressure.
- If full cleanup mode is active (default in current code), retry + reconnect strategies are much more viable.

Recommendation: emit the active close mode in startup diagnostics so deployment behavior is explicit.

## 10. Proposed implementation sequence

1. **Tank Alarm only**
- Update dead-session classifier.
- Add bounded per-file retry loop.
- Add retry metrics/logging.

2. **Validation**
- Run forced `-3005` scenarios and confirm later files continue.
- Verify no watchdog regressions.

3. **Then optimize runtime**
- Reduce inter-file wait only after retry metrics are healthy.

4. **Library enhancement (optional but high value)**
- Add `lastNsapiError()` and `lastFtpReplyCode()`.

## Test Matrix Additions

1. Transient data-open exhaustion (`-3005`)
- Expected: file retries and often recovers; batch continues.

2. Control-channel drop mid-run
- Expected: retry path does not spin; abort remaining with clear message.

3. TLS data handshake transient failure
- Expected: bounded retries, optional reconnect, then fail file only.

4. Hard auth/cert failure
- Expected: fail fast, no retries.

5. Retry exhaustion
- Expected: file marked failed, counters incremented, next file attempted unless control is truly dead.

## Recommended Decisions

1. Land per-file retry now, but **first** remove `DataConnectionFailed` from dead-session classification.
2. Use phase-aware retry gating (not just error enum).
3. Add retry counters before tuning timing constants.
4. Add `lastNsapiError()` to ArduinoOPTA-FTPS as the next library quality-of-life improvement.

## Bottom Line

The current per-file retry proposal is directionally correct. The highest-impact improvement is to reclassify `DataConnectionFailed` from "session dead" to "usually retryable data-path fault" and combine retries with phase-aware gating plus bounded reconnect. That keeps backups progressing through transient Opta socket-pool contention while still failing fast on true control-channel loss.