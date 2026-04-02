# Logic Review - 2026-04-02

## Scope
- Reviewed historical analytics control flow (month-over-month and year-over-year APIs) and related date handling.
- Primary file reviewed: `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`.

## Findings (ordered by severity)

### 1. High - Month-over-month hot-tier stats aggregate all snapshots, not the requested month
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11107`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11108`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11141`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11145`

Why this is a problem:
- Both current and previous hot-tier branches loop over every snapshot in the ring buffer without filtering by snapshot month/year.
- Returned monthly min/max/avg and computed deltas can be materially wrong.

Recommendation:
- Filter each hot-tier snapshot by timestamp month/year before including it in monthly aggregates.

### 2. High - Year-over-year hot-tier paths also ignore calendar boundaries
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11261`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11265`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11374`

Why this is a problem:
- "Current year" (all-sensor YoY) and "current month" (single-sensor YoY detail) both aggregate all retained hot-tier snapshots.
- This produces misleading annual/monthly baselines and invalid YoY comparisons.

Recommendation:
- Apply explicit timestamp filtering in both branches:
  - Year branch: include only snapshots in `currentYear`.
  - Month branch: include only snapshots in `currentYear/currentMonth`.

### 3. Medium - `prevInHotTier` detection condition does not match intended behavior
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11077`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11141`

Why this is a problem:
- `prevInHotTier` is true only when requested previous period equals the current system month.
- This conflicts with the comment "Rare case: previous month still in hot tier" and bypasses hot-tier use for real previous-month queries.

Recommendation:
- Replace the boolean heuristic with a data-driven check:
  - Scan hot-tier timestamps for at least one snapshot in `(prevYear, prevMonth)`.

### 4. Low - YoY fallback year is hardcoded to 2025 when time is unavailable
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11241`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11242`

Why this is a problem:
- If time sync is missing, analytics are anchored to a static year/month default.
- This can silently mislabel outputs and mislead operators.

Recommendation:
- Return a clear error/"time unavailable" flag instead of silently defaulting, or persist a last-known-good RTC epoch for fallback.

## Suggested Tests
- MoM API with snapshots spanning two months verifies only requested month samples are included.
- YoY summary/detail APIs verify year/month filtering against synthetic timestamped fixture data.
- No-time-sync scenario returns explicit degraded status, not hardcoded year values.
