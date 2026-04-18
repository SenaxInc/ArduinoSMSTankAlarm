# Scheduled Nightly FTPS Backup — Implementation Plan

**Date:** 2026-04-17
**Scope:** Application-side feature in `TankAlarm-112025-Server-BluesOpta.ino`.
**Goal:** Optionally trigger `performFtpBackup()` once per day at a
configured local time, in addition to the existing on-change and
manual triggers.
**Status:** Planning only. No code changes.

## Why

Today the server backs up to FTPS in two situations:

- **On change** (`gConfig.ftpBackupOnChange`) — fires after any
  config save that mutates persisted state. Bursty; can run several
  times in a busy day.
- **Manual** — the operator clicks "Backup Now" in the web UI.

A scheduled nightly backup gives a guaranteed, low-traffic window
(typically 02:00–04:00 local) where:

- No operator is using the dashboard, so the LWIP socket pool is
  unconstrained.
- The current 11-minute synchronous backup duration is acceptable.
- Failures get one fresh attempt every day rather than only when an
  operator notices.

## Existing Infrastructure to Reuse

The daily email feature already implements exactly this pattern; the
nightly backup should follow the same structure to stay consistent and
not introduce a new scheduler concept.

| Daily-email field | Nightly-backup analog |
| ----------------- | --------------------- |
| `ServerConfig::dailyHour` (uint8_t, 0-23) | `ftpNightlyHour` (uint8_t, 0-23, 0xFF = disabled) |
| `ServerConfig::dailyMinute` (uint8_t, 0-59) | `ftpNightlyMinute` (uint8_t, 0-59) |
| `gNextDailyEmailEpoch` (double) | `gNextNightlyBackupEpoch` (double) |
| Daily-email scheduling logic in `loop()` | Mirror the same scheduler block immediately after it |

`computeNextDailyEpoch()` (or whatever the existing daily-email
scheduler is named) can almost certainly be reused unchanged by
parameterizing the hour/minute it accepts.

## Proposed Config Surface

### `ServerConfig` struct additions

```cpp
struct ServerConfig {
  // ... existing fields ...
  uint8_t  ftpNightlyHour;    // 0-23 local time, 0xFF = disabled
  uint8_t  ftpNightlyMinute;  // 0-59
};
```

Defaults: `ftpNightlyHour = 0xFF` (disabled), `ftpNightlyMinute = 0`.

### JSON wire format

In `populateServerConfigJson()` / `applyServerConfigJson()`:

```jsonc
{
  "ftp": {
    "enabled": true,
    "backupOnChange": false,
    "nightlyEnabled": true,        // shorthand: maps to (hour != 0xFF)
    "nightlyHour": 2,              // optional, defaults to 2
    "nightlyMinute": 0             // optional, defaults to 0
  }
}
```

Use the same nested-then-flat fallback the other FTP fields use so old
clients keep working.

### Web UI

Add to the existing FTP block in `SERVER_SETTINGS_HTML` (near the
"Auto-backup on save" toggle):

- New checkbox: `Nightly backup`
- New `<input type="time">` field: `ftpNightlyTime` (default `02:00`)
- Wire both into the existing `populate()` and submit handler that
  already manage `ftpBackupOnChange` and `dailyEmailTime`.

## Scheduler Logic (in `loop()`)

Drop a block right next to the daily-email scheduler:

```cpp
// SCHEDULED-BACKUP: Fire once per day at gConfig.ftpNightlyHour:Minute
// when nightly backup is enabled (hour != 0xFF) and FTP itself is
// enabled. Reuses the existing daily-email scheduling helper.
if (gConfig.ftpEnabled &&
    gConfig.ftpNightlyHour != 0xFF &&
    nowEpoch >= gNextNightlyBackupEpoch &&
    !gPendingFtpBackup &&            // don't double-trigger
    !gBackupInProgress) {            // and don't preempt a running one
  Serial.println(F("Scheduled nightly FTP backup triggered"));
  gPendingFtpBackup = true;
  gNextNightlyBackupEpoch = computeNextLocalEpoch(
      nowEpoch,
      gConfig.ftpNightlyHour,
      gConfig.ftpNightlyMinute);
}
```

The actual backup execution then runs through the existing
`gPendingFtpBackup` path that already handles `gWebServer.end()` /
`begin()` and `gBackupInProgress`, so no additional plumbing.

## Edge Cases

| Case | Behavior |
| ---- | -------- |
| Time not yet set (NTP/Notecard hasn't synced) | Skip; `nowEpoch` will be 0 or far in the past, scheduler won't fire until time syncs and `gNextNightlyBackupEpoch` is initialized. |
| User changes `ftpNightlyHour` | Re-compute `gNextNightlyBackupEpoch` immediately on save (same way daily-email handles its time change). |
| Backup runs longer than 24 h (won't happen, but defensively) | The completion path resets `gPendingFtpBackup = false`; the scheduler will then re-evaluate and pick the next future occurrence. |
| Power cycle during the scheduled window | On boot, recompute `gNextNightlyBackupEpoch` to next future occurrence; missed runs are not retried. |
| User disables nightly via UI mid-day | `ftpNightlyHour = 0xFF` short-circuits the scheduler check on the next loop iteration. |
| Backup fails (e.g. all 8 files fail with -3005) | Log to serial + transmission log. Optionally email or SMS — see Open Questions below. |

## Diff Footprint Estimate

- **+2 fields** in `ServerConfig`.
- **+1 default-init** line in `applyDefaultConfig()`.
- **+~6 lines** in the JSON populate/apply pair.
- **+1 toggle + 1 time input** in `SERVER_SETTINGS_HTML` (and small JS
  to hook them into populate/submit).
- **+1 scheduler block (~10 lines)** in `loop()`.
- **+1 helper** (or reuse existing daily-email scheduler).
- **No library changes.**

## Test Plan

1. **Disabled by default:** Fresh config has `ftpNightlyHour = 0xFF`,
   no scheduled backup ever fires.
2. **Enable + set to current minute:** Wait one minute, observe
   `Scheduled nightly FTP backup triggered` in serial, observe full
   backup run.
3. **Reschedule for tomorrow:** After successful run,
   `gNextNightlyBackupEpoch` advances to next day's same time.
4. **Manual + scheduled don't collide:** Click "Backup Now" while the
   scheduler would otherwise fire; verify the scheduler skips because
   `gBackupInProgress == true`, then re-evaluates next loop.
5. **Reboot:** Disable, enable, reboot device. After NTP sync, confirm
   the next scheduled run is set to the correct future epoch.
6. **Time-zone correctness:** Set nightly to `02:00`. Confirm backup
   fires at `02:00` local time, not UTC. (Daily-email already gets
   this right; reusing the same helper inherits the behavior.)

## Open Questions

These are deferrable; defaults below are reasonable for v1.

1. **Notify on failure?** Default no — failures go to serial and
   transmission log only. SMS or email alerts for backup failures
   could be added later if operators want them.
2. **Notify on success?** Default no — too noisy.
3. **Skip if already backed up via on-change today?** Default no —
   nightly is a guarantee, not an optimization.
4. **Multiple times per day?** Default no — a single nightly slot is
   enough. Could become a list of times in v2 if needed.

## Cross-References

- `MULTI_FILE_BACKUP_FOLLOWUPS_04172026.md` — broader optimization
  catalog; nightly is referenced as item 0 (foundational).
- `PER_FILE_RETRY_PLAN_04172026.md` — retry logic; recommended to land
  before nightly so failed files in the unattended window are not
  silently lost to a single transient -3005.
- `OPTA_LWIP_BACKUP_RECIPE_04172026.md` — application integration
  recipe (the nightly path uses the same `gWebServer.end()` /
  `begin()` and `gBackupInProgress` pattern as the manual path).
