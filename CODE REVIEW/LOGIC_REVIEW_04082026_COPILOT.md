# Logic Review - 2026-04-08

## Scope
- Reviewed control flow and runtime behavior for relay handling, Notehub note-consumption semantics, server-side note processing, and historical analytics.
- Primary files reviewed:
  - `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
  - `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- This document includes the original findings from this review plus additional items I re-validated after comparing the recent review documents in `CODE REVIEW/`.

## Findings (ordered by severity)

### 1. High - Relay behavior is logically inconsistent between alarm-driven and manual command paths
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4803`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7022`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7090`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7166`

Why this is a problem:
- The alarm path records activation time and active-mask state, so momentary relays can be cleared automatically later.
- The manual command path only toggles the physical relay state and skips the runtime bookkeeping.
- That means the same relay can behave as timed or untimed depending only on how it was activated.

Recommendation:
- Push both paths through one relay state-machine function that updates hardware state, active masks, and activation timestamps together.
- Make the supported command semantics explicit: either manual commands participate in timeout logic, or they are formally latched-only commands.

### 2. High - Viewer summary delivery is treated as consumed before successful application
Evidence:
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:820`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:822`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:827`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:837`

Why this is a problem:
- The code comments describe a crash-safe peek-then-delete flow, but the implementation deletes even after parse failure or OOM.
- Operationally that changes the semantics from retry-until-applied to best-effort delivery.
- On a constrained viewer device, transient memory pressure can therefore cause silent dashboard staleness.

Recommendation:
- Only delete the note after successful parse and successful application to in-memory state.
- If repeated failures are a concern, add retry counting and explicit dead-letter handling.

### 3. High - Server note processing applies side effects before deletion and has no idempotency guard
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8039`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8073`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8083`

Why this is a problem:
- `processNotefile()` peeks a note, invokes the handler, then performs a second `note.get` with `delete=true`.
- A reset or crash between the handler side effects and the delete request causes the same note to be replayed on the next poll.
- That is not safe for alarm dispatch, serial logging, config acknowledgements, or any path with non-idempotent side effects.

Recommendation:
- Add idempotency keys or persistent "already processed" tracking for note handlers with side effects.
- If the platform supports it, bind deletion to the exact note that was processed instead of relying on a second generic fetch.

### 4. High - The client-recovery workflow is broken by summary-schema drift on the server
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1506`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6860`

Why this is a problem:
- The server API exposes client summaries under `cs`, and other pages in the same firmware already use `d.cs`.
- The Config Generator's cloud-load workflow expects `d.clients`, so it cannot enumerate existing clients when an operator needs to pull and edit a deployed configuration.
- This is a producer/consumer contract failure, not just a cosmetic front-end issue.

Recommendation:
- Standardize the schema in one place and reuse it everywhere.
- Prefer a shared normalization helper on the page side, or emit both names temporarily until all consumers are migrated.

### 5. High - Month-over-month hot-tier stats aggregate all snapshots, not the requested month
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11119`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11156`

Why this is a problem:
- In `handleHistoryCompare()`, both the current and previous hot-tier branches iterate all retained snapshots for the sensor.
- There is no timestamp filter for the requested month/year before min/max/avg are calculated.
- If the hot tier spans multiple months, the reported monthly comparison is statistically wrong.

Recommendation:
- Filter each hot-tier snapshot by UTC month/year before contributing to period aggregates.

### 6. High - Year-over-year hot-tier paths also ignore calendar boundaries
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11276`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11385`

Why this is a problem:
- The all-sensor current-year branch and the single-sensor current-month branch both aggregate all retained hot-tier snapshots.
- There is no year filter for the current-year summary and no month/year filter for the current-month detail branch.
- That makes year-over-year comparisons unreliable whenever retention spans older periods.

Recommendation:
- Apply explicit calendar filtering in both branches before computing min/max/avg values.

### 7. Medium - Copy-paste variable drift is breaking entire operator workflows, not isolated widgets
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1552`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1570`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1574`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1576`

Why this is a problem:
- The repeated `rec.*` references are not one isolated typo; they cut across calibration sensor selection, historical chart labels, site cards, and CSV export.
- That suggests the embedded UI logic is being duplicated without enough execution coverage.
- In practice, this blocks operator visibility and manual calibration workflows that are part of the server's core value.

Recommendation:
- Fix the variable names, then reduce duplication by extracting small shared rendering helpers.
- Add browser-side smoke checks for each server page that uses embedded JS templates inside the firmware source.

## Suggested Tests
- Manual relay ON commands should clear on the same schedule as alarm-driven momentary activations, or the UI/protocol should show that they are latched-only.
- Viewer should retry the same summary after injected parse failure or forced low-memory handling.
- Server should not replay non-idempotent note side effects after a reset between handler execution and note deletion.
- Config Generator cloud-load modal should populate from `/api/clients?summary=1` using the live schema.
- Compare endpoint tests should verify that month selection only includes samples from the requested month.
- Year-over-year endpoint tests should verify that current-year and current-month hot-tier branches exclude older snapshots.
- Calibration dropdown, historical charts, and historical CSV export should all execute without `ReferenceError`.
