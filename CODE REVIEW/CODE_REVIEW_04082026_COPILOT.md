# Code Review - 2026-04-08

## Scope
- Reviewed the current Client, Server, Viewer, and shared DFU/control paths in the active source tree.
- Primary files reviewed:
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
  - `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino`
  - `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino`
  - `TankAlarm-112025-Common/src/TankAlarm_DFU.h`
- This document includes the original findings from this review plus additional items I re-validated after comparing the recent review documents in `CODE REVIEW/`.
- This was a static review only. I did not run a full Arduino build.

## Findings (ordered by severity)

### 1. Critical - IAP DFU erases application flash before the replacement image is fully available
Evidence:
- `TankAlarm-112025-Common/src/TankAlarm_DFU.h:362`
- `TankAlarm-112025-Common/src/TankAlarm_DFU.h:371`

Why this is a problem:
- The DFU path erases the application region first and only then starts the `dfu.get` loop to fetch chunks from the Notecard.
- Any power loss, I2C failure, Notecard timeout, decode failure, or flash program error after the erase leaves the application region blank or partially programmed.
- The failure path restores hub mode, but it does not restore the erased firmware image.

Recommendation:
- Stage the full image before erasing application flash, or move to an A/B or staging-slot design.
- At minimum, avoid destructive erase until the entire update payload is known to be present and valid.

### 2. Critical - IAP DFU reboots immediately after programming with no post-write integrity verification
Evidence:
- `TankAlarm-112025-Common/src/TankAlarm_DFU.h:497`
- `TankAlarm-112025-Common/src/TankAlarm_DFU.h:528`

Why this is a problem:
- After the final chunk is written, the code reports success and resets immediately.
- There is no readback CRC/hash/checksum validation between flash programming and reboot.
- A marginal flash write or truncated payload can therefore become the next boot image without detection.

Recommendation:
- Read back the programmed image and verify it against a trusted checksum from the DFU metadata before clearing DFU state and resetting.

### 3. High - Config Generator "Load From Cloud" is wired to a field the API does not return
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1506`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6860`

Why this is a problem:
- The Config Generator modal calls `d.clients.map(...)`, but the `/api/clients?summary=1` response is built under `doc["cs"]`.
- That throws when operators try to load an existing client configuration from the cloud list.
- This blocks one of the main recovery/edit workflows in the server UI.

Recommendation:
- Change the page to consume `d.cs`, or normalize the response once before rendering.
- Add a browser smoke test that opens the modal and verifies at least one client renders.

### 4. High - Multiple server UI pages reference an undefined `rec` variable and throw at runtime
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1552`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1570`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1574`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1576`

Why this is a problem:
- The calibration page dropdown population and the historical-data page chart/site/export rendering use `rec.site` / `rec.label`, but the active iterator variables are `tank` or `t`.
- In JavaScript this becomes a `ReferenceError`, so the affected view stops rendering instead of degrading gracefully.
- Impacted workflows include calibration sensor selection, historical chart labels, site-card rendering, and CSV export.

Recommendation:
- Replace each `rec.*` usage with the current iterator object (`tank.*` or `t.*` as appropriate).
- Add page-level smoke tests for calibration and historical-data views.

### 5. High - Viewer deletes summary notes even when parsing or allocation fails
Evidence:
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:822`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:827`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:837`

Why this is a problem:
- `fetchViewerSummary()` logs parse failure or OOM, but still issues a second `note.get` with `delete=true` afterward.
- That turns a transient parse or memory failure into permanent data loss for the pending summary note.
- The viewer can remain stale until the next summary cadence instead of retrying the same payload.

Recommendation:
- Delete the note only after `handleViewerSummary()` completes successfully.
- Keep the note queued on parse/OOM failure, or move it to a dead-letter path only after repeated failures.

### 6. High - First-login PIN setup accepts any 4-character string, not a 4-digit PIN
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1063`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5958`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5959`

Why this is a problem:
- The shared validator already enforces digit-only 4-character PINs.
- The first-login branch bypasses that validator and persists any 4-character string.
- That creates inconsistent auth behavior and weakens the intended admin PIN model.

Recommendation:
- Replace `strlen(pin) == 4` with `isValidPin(pin)` in the first-login path.

### 7. Medium - Manual relay commands bypass the momentary-timeout bookkeeping
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:4803`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:5997`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7022`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7090`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:7166`

Why this is a problem:
- Alarm-driven relay activation updates `gRelayActivationTime[]` and the per-monitor active masks, which is what `checkRelayMomentaryTimeout()` relies on.
- Manual relay commands received through `relay.qi` call `setRelayState()` directly and do not update any of that state.
- The result is inconsistent behavior: an alarm-driven momentary output times out, but a server-driven manual ON command can stay latched indefinitely unless another command clears it.

Recommendation:
- Route all relay actuation through one helper that updates both hardware state and timeout bookkeeping.
- If manual commands are intentionally latched-only, make that explicit in the protocol and UI.

### 8. Medium - Authentication rate limiting uses wrap-unsafe future timestamps
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1169`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1207`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1210`

Why this is a problem:
- The lockout window is tracked as `gNextAllowedAuthTime = now + delay`, then checked with `now < gNextAllowedAuthTime`.
- Around `millis()` wraparound, stale pre-wrap values can produce incorrect or effectively stuck lockouts.
- This is a long-uptime bug in a security-sensitive path.

Recommendation:
- Replace absolute future-time comparisons with subtraction-based elapsed-time checks.

### 9. Medium - Historical page injects raw site names into `innerHTML`
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1574`

Why this is a problem:
- `renderSites()` inserts `${siteName}` directly into an HTML string.
- Site names are operator-configurable data, so this is a stored-XSS style bug on the LAN dashboard.

Recommendation:
- Escape site names before interpolation, or build the header DOM via `textContent` instead of raw HTML.

### 10. Medium - Several JSON responses are manually concatenated without escaping dynamic strings
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13300`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13304`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:13592`

Why this is a problem:
- Dynamic values such as site names, details, and client UIDs are inserted into JSON with raw `String` concatenation.
- Quotes, backslashes, or control characters in those values can produce malformed JSON and break API consumers.

Recommendation:
- Build these responses with `JsonDocument` and `serializeJson()` instead of manual string assembly.

### 11. Medium - FTP response assembly still relies on unsafe `strcat()` calls
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:3475`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:3476`
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:3484`

Why this is a problem:
- The function does length accounting before appending, but the actual append operations still use `strcat()`, which has no awareness of the destination capacity.
- Any future maintenance mistake in the length math turns this into a stack-buffer corruption risk.

Recommendation:
- Replace the appends with bounded writes (`snprintf`, or an equivalent helper that tracks remaining capacity explicitly).

## Suggested Improvements
- Centralize `/api/clients` response normalization in one reusable JS helper across all server pages.
- Add smoke tests for the server UI fragments generated inside raw C++ string literals.
- Unify relay state transitions behind a single state-machine entry point so alarm-driven and manual-driven behavior cannot diverge.
- Rework DFU around staged writes plus explicit image verification before reboot.
- Move hand-built JSON APIs to `JsonDocument` helpers so escaping is handled consistently.

## Suggested Tests
- Interrupt DFU after the erase stage and verify the redesigned flow does not leave the device without a recoverable image.
- Corrupt a staged or programmed DFU image and verify checksum failure blocks reboot.
- Open Config Generator and verify "Load From Cloud" lists clients from `/api/clients?summary=1`.
- Open Calibration and Historical Data pages and confirm they render without JavaScript runtime errors.
- Verify first-login rejects non-digit PIN values.
- Inject a malformed viewer summary payload and verify the Viewer leaves the note queued instead of deleting it.
- Send both alarm-driven and manual relay activations and verify timeout behavior is consistent and explicit.

## Final Review
- Reviewed the claimed Server, Client, and Viewer fixes against the current source tree.
- Most of the listed fixes appear structurally correct on static review.
- Remaining changes needed are below.

### 1. Medium - `publishNote()` overflow is only mitigated, not actually fixed
Evidence:
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6455`
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6457`

Why this still needs work:
- The buffer was increased from 1024 to 2048 bytes, which reduces the likelihood of overflow.
- However, the failure behavior is unchanged: if serialized JSON still exceeds the buffer, the note is logged and dropped immediately.
- The original issue was silent data loss on oversize payloads. That is still present, just at a larger threshold.

Recommendation:
- Treat oversize payloads as a retryable/bufferable failure instead of returning immediately.
- Prefer `measureJson()` plus bounded allocation, or split the payload before serialization.

### 2. Medium - Previous-month hot-tier detection is still based on wall-clock equality, not actual retained data
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11093`

Why this still needs work:
- The month boundary filtering added to `handleHistoryCompare()` is correct once the hot-tier branch is entered.
- But `prevInHotTier` is still only true when the requested previous period equals the device's current month/year.
- That means a valid previous-month query can still skip hot-tier data even when the retention window actually contains it.

Recommendation:
- Determine hot-tier membership by scanning retained snapshot timestamps for the requested year/month instead of comparing only to the current calendar month.

### 3. Medium - Viewer parse/OOM retry fix can now head-of-line block the summary queue indefinitely
Evidence:
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:837`
- `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:846`

Why this still needs work:
- The new logic correctly stops deleting the note on parse/OOM failure, which fixes the data-loss bug.
- But a permanently bad summary note now remains at the head of the queue and is retried every cycle.
- If the Notecard returns notes in order, one corrupt note can block all newer viewer summaries indefinitely.

Recommendation:
- Add a retry counter, poison-note quarantine, or dead-letter path after repeated failures.
- Keep the current no-delete-on-first-failure behavior, but do not allow one bad note to starve the queue permanently.

### 4. Low - `warmTierAvailable` is still only checking configuration, not actual warm-tier data presence
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:11020`

Why this still needs work:
- The previous hardcoded `true` was removed, which is an improvement.
- But the new value is still derived only from `warmTierRetentionMonths > 0`.
- Fresh systems or systems with missing/corrupt summary files can still advertise flash historical data that is not actually available.

Recommendation:
- Base `warmTierAvailable` on real summary-file existence or successful warm-tier month reads, not only on retention configuration.

### 5. Low - Historical CSV export now HTML-escapes values instead of CSV-escaping them
Evidence:
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1578`

Why this needs work:
- Using `escapeHtml()` inside CSV generation avoids HTML injection, but CSV is not HTML.
- Labels such as `A&B` or `5 < 6` will export as `A&amp;B` and `5 &lt; 6`, which corrupts the exported data.
- This appears to be a regression introduced while hardening the historical page output.

Recommendation:
- Replace `escapeHtml()` in CSV export with a CSV-specific escaping helper that doubles quotes and preserves the original text content.
