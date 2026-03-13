# Code Review: Server, Client, and Viewer System

Date: 2026-03-12

Scope reviewed:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino
- TankAlarm-112025-Common/src/*

This review is based on static analysis of the current source. I did not run hardware-in-the-loop tests.

## Findings

### 1. High: Large client configs can be dispatched, but the cached snapshot and retry state stay stale

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:848
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:7654
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:10075
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9061

Why this matters:
- The server can serialize and send a client config up to the 8192-byte dispatch buffer in `dispatchClientConfig()`, but `ClientConfigSnapshot.payload` is only 1536 bytes.
- `cacheClientConfigFromBuffer()` rejects payloads `>= sizeof(payload)` and returns early, but `dispatchClientConfig()` continues, looks up the existing snapshot anyway, and marks it pending with the hash of the new payload.
- If the client already had an older cached snapshot, retries will resend that old payload instead of the newly submitted one. If the first send succeeds, the server can still serve stale config JSON back through `handleClientConfigGet()`, so the console no longer reflects what was actually sent.

Impact:
- Failed sends for larger site configs are not recoverable by the advertised retry path.
- ACK/version tracking can be associated with a stale cached payload.
- Operators can see an outdated config in the server console after successfully pushing a newer one.

### 2. High: Config retry cadence is one hour, but the API tells operators it retries every 60 seconds

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:2661
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:6954

Why this matters:
- The HTTP response for `ConfigDispatchStatus::CachedOnly` tells the user the config will auto-retry every 60 seconds.
- The actual loop gate is `3600000UL`, and the surrounding comment says `every 60 minutes`.

Impact:
- After a temporary Notecard outage or weak-signal failure, operators can reasonably expect the config to go out within a minute, but the server does not retry for up to an hour.
- That delay is large enough to look like a stuck rollout, especially for field fixes that are time-sensitive.

### 3. Medium: Viewer drops measurement metadata and renders every device as a tank level in feet/inches

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:8968
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:159
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:631
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:772

Why this matters:
- The server publishes `ot`, `st`, and `mu` in `publishViewerSummary()`, so the summary stream already distinguishes tanks from engines, pumps, gas, and flow monitors.
- The viewer throws that metadata away in `handleViewerSummary()`, does not expose it from `sendTankJson()`, and the browser always formats `l` with `formatFeetInches()` under the fixed column heading `Level (ft/in)`.

Impact:
- Any non-tank sensor pushed into the viewer summary will be misrepresented as a tank depth.
- RPM, PSI, GPM, and similar readings become misleading operator data instead of merely incomplete data.

### 4. Medium: Viewer summary includes 24-hour change, but the viewer never stores or renders it

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:9000
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:159
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:631
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:772

Why this matters:
- The server publishes `d` for 24-hour delta in the viewer summary payload.
- The viewer does not read that field into its internal record, does not include it in `/api/tanks`, and the browser hard-codes the `24hr Change` column to `--`.

Impact:
- The UI advertises a trend column that can never contain data.
- This removes one of the key pieces of situational context the server is already computing and sending.

### 5. Medium: Session validation leaks the live session token into URLs on every page check

Files:
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5739
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1544

Why this matters:
- `handleSessionCheck()` expects the token in the query string: `/api/session/check?token=...`.
- The embedded page scripts call that endpoint on a timer and on visibility changes, so the active session token is repeatedly placed into request URLs instead of staying in a header or body.

Impact:
- Session tokens can end up in browser history, proxy logs, access logs, and other URL-capture points on the management network.
- Because the same token is accepted later through `X-Session`, any logged token is directly reusable for authenticated API access until logout or re-login invalidates it.

## Open Questions

1. Is the viewer intended to support only liquid tank monitors, or should it reflect all object types already present in the server summary payload?
2. What is the intended maximum supported client config size for production sites? The current dispatch path and cache path enforce different limits.

## Residual Risk

- I did not compile or run these sketches, so there may be hardware-specific behavior not visible from static review.
- The server sketch is large enough that there may be additional edge cases in other API and persistence paths outside the specific flows reviewed here.# Code Review - Server, Client, Viewer System

Date: 2026-03-12  
Reviewer: GitHub Copilot (GPT-5.3-Codex)  
Scope: 
- TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino
- TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino
- TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino

## Findings (ordered by severity)

### 1) High - Session token exposed in URL query string during session checks
- Location:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5740-5748
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1363
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1386
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1398
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1473
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1503
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1518
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1538
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1546
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1601
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1675
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1698
- What happens:
  - Browser code performs session validation with `GET /api/session/check?token=...`.
  - Server extracts token from query (`token=`).
- Risk:
  - Session token can leak through browser history, proxies, router logs, and referrer propagation.
  - This weakens session confidentiality even though API requests also use `X-Session`.
- Recommendation:
  - Remove query-token flow entirely.
  - Validate sessions only from `X-Session` header (or secure cookie).
  - Keep `/api/session/check` as header-based GET with no token in URL.

### 2) High - Raw admin PIN is persisted in browser localStorage
- Location:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1544
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:1363
- What happens:
  - On login success, UI stores the typed PIN in `localStorage` (`tankalarm_token`) and later reuses it as `state.pin` for privileged operations.
- Risk:
  - Any script execution in origin context (XSS, injected dependency, browser extension compromise) can read the cleartext PIN.
  - PIN has longer-lived blast radius than session token because it is reusable secret material.
- Recommendation:
  - Never store raw PIN client-side.
  - Use session token only for authorization after login.
  - For high-risk actions requiring step-up auth, prompt for PIN in-memory per action and do not persist.

### 3) Medium - Fresh-install login permits authenticated session without valid PIN setup
- Location:
  - TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino:5781-5790
- What happens:
  - If configured PIN is invalid/unset, login marks request as valid even when provided PIN is blank or malformed (non-4-digit), then issues a valid session.
- Risk:
  - During bootstrap windows, any LAN user can obtain a session without proving knowledge of a PIN.
  - This is especially risky if commissioning occurs on shared or untrusted LANs.
- Recommendation:
  - Require explicit first-run enrollment flow that sets a valid 4-digit PIN before granting general API session.
  - Return setup-required state until PIN is successfully established.

### 4) Medium - Client executes inbound relay commands without verifying intended target identity
- Location:
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6583-6621
  - TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino:6623-6689
- What happens:
  - `pollForRelayCommands()` pulls from `relay.qi` and `processRelayCommand()` executes based on `relay/state` fields only.
  - No check that payload target UID equals this device UID.
- Risk:
  - Safety currently relies completely on Notehub route correctness.
  - Any route misconfiguration or accidental fan-out could actuate relays on unintended clients.
- Recommendation:
  - Include and enforce a `target` (or equivalent) UID in relay payloads.
  - Drop command if target does not match `gDeviceUID`.
  - Log and ack rejected commands for traceability.

### 5) Low - Viewer API is intentionally unauthenticated and exposes fleet metadata on LAN
- Location:
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:444-473
  - TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino:631-671
- What happens:
  - Any LAN host can read `GET /api/tanks` with server UID, viewer UID, site labels, alarm flags, and update timestamps.
- Risk:
  - Acceptable for kiosk-only trusted LAN deployments, but this is an information disclosure risk on mixed networks.
- Recommendation:
  - If deployment context is not strictly trusted LAN, add optional read token or network ACL guard.
  - At minimum, document this clearly in viewer README and deployment checklist.

## Positive notes
- Server HTTP parsing has strong line-length caps and body-size enforcement, reducing request-memory abuse risk.
- Constant-time compares are used for both session token and PIN checks.
- Client and viewer include watchdog-aware loops and bounded processing in key Notecard polling paths.

## Residual testing gaps
- No runtime test execution was performed in this review (static review only).
- Recommend adding targeted integration tests for:
  - Session lifecycle (`/api/login`, `/api/session/check`, `/api/logout`) with header-only tokens.
  - First-run PIN enrollment behavior.
  - Relay command rejection when target UID mismatches.
