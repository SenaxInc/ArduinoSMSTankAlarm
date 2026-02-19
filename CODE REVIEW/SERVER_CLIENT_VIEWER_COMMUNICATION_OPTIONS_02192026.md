# Server-Client-Viewer Communication Architecture Options (Expanded Review)

**Date:** February 19, 2026  
**Context:** Full re-review of communication patterns across Server Opta, Client Optas, and Viewer Optas, with deep Blues Notecard/Notehub API coverage  
**Primary Goal:** Minimize operational complexity and avoid custom security certificates where possible  
**Status:** Architecture Decision Support (v2 analysis)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [What Changed Since the 02/06/2026 Review](#2-what-changed-since-the-02062026-review)
3. [Current Observed Implementation Pattern](#3-current-observed-implementation-pattern)
4. [Option A: Notecard-Native Direct Addressing (Recommended Baseline)](#4-option-a-notecard-native-direct-addressing-recommended-baseline)
5. [Option B: Notecard-Native + DB State Replication](#5-option-b-notecard-native--db-state-replication)
6. [Option C: Event-Driven Host Loop (ATTN + Change Trackers)](#6-option-c-event-driven-host-loop-attn--change-trackers)
7. [Option D: Minimal Cloud Bridge (No Routes, OAuth)](#7-option-d-minimal-cloud-bridge-no-routes-oauth)
8. [Option E: Notecard web.* + Proxy Route Control Plane](#8-option-e-notecard-web--proxy-route-control-plane)
9. [Option F: Direct HTTPS from Opta (BearSSL)](#9-option-f-direct-https-from-opta-bearssl)
10. [Option G: Hybrid Split-Plane (Notecard Data + API Control)](#10-option-g-hybrid-split-plane-notecard-data--api-control)
11. [Option H: Multi-Server / Multi-Viewer Fleet Topologies](#11-option-h-multi-server--multi-viewer-fleet-topologies)
12. [Potentially Missed Blues API Features (Important)](#12-potentially-missed-blues-api-features-important)
13. [Fleet, Device UID, Product UID Strategy Patterns](#13-fleet-device-uid-product-uid-strategy-patterns)
14. [Security and Certificate Requirements Matrix](#14-security-and-certificate-requirements-matrix)
15. [Comparison Matrix](#15-comparison-matrix)
16. [Data Usage Analysis by Option](#16-data-usage-analysis-by-option)
17. [Recommended Roadmap](#17-recommended-roadmap)
18. [Conclusion: Best Communication Architecture](#18-conclusion-best-communication-architecture)
19. [Implementation Checklist (One-Page)](#19-implementation-checklist-one-page)

---

## 1. Executive Summary

The strongest finding from this re-review is that your codebase is already moving toward a **route-minimized, Notecard-native device messaging model**, especially with:

- `device:<uid>:*.qi` targeting for server/client commands
- Fleet-targeted publishes for client→server telemetry (`fleet:<name>:*.qi` pattern in current client code)

This means the old “must use Notehub Routes for inter-device delivery” assumption is no longer the right baseline for this repository.

### High-Level Recommendation

If your priority is **avoid custom certificates**, keep communication primarily in native Notecard/Notehub flows and improve host logic with:

1. **Option A** (direct addressing baseline)
2. Add **Option C** mechanisms (`card.attn`, `file.changes`, `note.changes` trackers) to reduce poll load and latency
3. Add selected **Option B** DB files for shared state and acknowledgments

Use BearSSL/direct HTTPS only if/when you truly need API control that Notecard-native patterns cannot provide.

---

## 2. What Changed Since the 02/06/2026 Review

### Key Delta #1: Inter-device delivery is already route-light

Observed code paths show direct target addressing (no custom route transform required for many cases):

- Server sends to `device:<clientUid>:config.qi`, `device:<clientUid>:relay.qi`, `device:<clientUid>:location_request.qi`, etc.
- Client publishes to fleet-style targets for server consumption (configured by `serverFleet`)

### Key Delta #2: New opportunity is not “how to call API” but “how to run event loop better”

The bigger improvement now is host-side orchestration:

- Move from repeated per-file `note.get` polling loops
- Toward `file.changes` + tracker + `note.changes` pattern
- Optionally trigger wake/processing using `card.attn` file-change events

### Key Delta #3: Viewer path can be made cleaner

Viewer summary currently appears as a distinct channel and can be improved with:

- Direct device targeting for one viewer
- Dedicated viewer fleet fanout for many viewers
- Optional DB snapshot model for “latest state” semantics

---

## 3. Current Observed Implementation Pattern

### 3.1 Server → Client

- Direct target files using device UID (already implemented):
	- `device:<uid>:config.qi`
	- `device:<uid>:location_request.qi`
	- `device:<uid>:serial_request.qi`
	- `device:<uid>:relay.qi`

### 3.2 Client → Server

- Fleet-routed target naming via `serverFleet` in client config
- Multiple outbound event types (`telemetry`, `alarm`, `daily`, `serial_log`, `location_response`, etc.)

### 3.3 Server inbound processing

- Server currently processes per notefile with repeated `note.get(..., delete:true)` loops
- Works, but can be improved for efficiency and responsiveness

---

## 4. Option A: Notecard-Native Direct Addressing (Recommended Baseline)

### Description

Use native Notecard notefile addressing only:

- **Client → Server:** `fleet:<server-fleet>:<file>.qi`
- **Server → Client:** `device:<client-uid>:<file>.qi`
- **Server → Viewer:** `device:<viewer-uid>:viewer_summary.qi` or viewer fleet target

No custom cert handling on Opta. No BearSSL. Minimal Notehub API dependence.

### Pros

- No custom TLS stack on Opta
- No custom certificate rotation burden
- Uses Blues-managed transport/session security end-to-end
- Keeps firmware simple and consistent with existing code

### Cons

- Still need good host logic for queue processing and dedupe
- Less flexible than direct API for global fleet analytics and bulk operations

### Best use case

Default architecture for current production system.

---

## 5. Option B: Notecard-Native + DB State Replication

### Description

Add `.db` / `.dbs` notefiles for replicated state where queue semantics are awkward.

Example uses:

- `client_state.db` per client (last applied config revision, relay state, calibration version)
- `viewer_snapshot.db` for latest summarized state instead of only queue updates
- `server_policy.db` for server-published shared controls

### Why this matters

Queues (`.qi/.qo`) are excellent for events, but state is often cleaner in DB files:

- update in place (`note.update`)
- read current value deterministically (`note.get` with note ID)

### Pros

- Stronger idempotency model for config/state
- Cleaner recovery after reboot/offline periods
- Fewer “stale command replay” edge cases

### Cons

- Requires schema discipline (note IDs, versioning)
- Slightly more logic than pure queue model

### Certificate impact

None beyond Blues standard transport.

---

## 6. Option C: Event-Driven Host Loop (ATTN + Change Trackers)

### Description

Keep Notecard-native addressing, but replace heavy poll loops with:

1. `card.attn` in file-watch mode (watch incoming files)
2. `file.changes` with tracker for which files changed
3. `note.changes` with tracker for incremental retrieval

This is a major operational upgrade without changing your fundamental topology.

### Missed capability this unlocks

- Avoid scanning 7+ files every loop when nothing changed
- Faster reaction on inbound notes
- Lower host CPU churn

### Pros

- Better responsiveness and efficiency
- Minimal architectural disruption
- No custom cert requirements

### Cons

- Requires careful tracker lifecycle management (`start/reset/stop`)
- Need robust re-arm logic for ATTN

---

## 7. Option D: Minimal Cloud Bridge (No Routes, OAuth)

### Description

Run a tiny cloud worker (or on-prem service) that uses Notehub API with OAuth client credentials:

- Reads events via cursor endpoint (`/events-cursor`)
- Sends targeted inbound notes via Device API (`/devices/{uid}/notes/{file}`)
- Can send `signal` to specific devices for low-latency wake logic

### Why this is interesting

Gives strong orchestration while keeping Opta firmware simple and avoiding custom cert management on MCU.

### Pros

- No custom certificates on Opta
- OAuth token lifecycle better than long-lived PATs
- Powerful fleet-level observability and controls

### Cons

- Introduces external service to deploy/monitor
- More moving parts than pure Notecard-native model

### Certificate impact

TLS handled by cloud runtime (not custom cert store in Opta firmware).

---

## 8. Option E: Notecard web.* + Proxy Route Control Plane

### Description

Opta calls Notehub API through Notecard `web.get/web.post` and a Notehub Proxy Route alias.

### Strengths

- No BearSSL on Opta
- Route alias centralizes API auth header in Notehub

### Important constraints (easy to underestimate)

- Notecard must be connected during web transaction
- ~8192-byte response limit
- Call-level timeout behavior (default 90s)
- Blocks Notecard transaction path during request lifecycle

### Certificate impact

No custom cert management on Opta.

---

## 9. Option F: Direct HTTPS from Opta (BearSSL)

### Description

Opta Ethernet client talks directly to `api.notefile.net` using on-device TLS.

### Why this is often painful on MCU deployments

- CA chain management/rotation
- time validation dependencies
- memory/connection handling overhead
- higher complexity under intermittent network conditions

### When justified

- You must eliminate intermediary services/routes
- You need features only practical through direct API from device

### Certificate impact

**Requires certificate handling in firmware** (the path you wanted to avoid).

---

## 10. Option G: Hybrid Split-Plane (Notecard Data + API Control)

### Description

Use two planes intentionally:

- **Data plane:** Notecard-native queue/DB messaging for normal telemetry and commands
- **Control plane:** Notehub API for rare administrative operations (fleet bulk, audits, provision workflows)

This avoids overusing API polling for regular traffic while preserving high-power admin tools.

### Pros

- Operationally balanced
- Keeps day-to-day traffic route/cert-light
- Enables deep management when needed

### Cons

- Requires clear ownership boundaries between planes

---

## 11. Option H: Multi-Server / Multi-Viewer Fleet Topologies

Think outside the box for resilience and scaling:

### H1: Active/Passive servers via dual fleet publish

Clients publish to both:

- `fleet:<primary-server-fleet>:telemetry.qi`
- `fleet:<backup-server-fleet>:telemetry.qi`

Use dedupe keys (`deviceUID + sequence + timestamp`) on servers.

### H2: Role-based viewer fleets

- `viewer-ops`
- `viewer-management`
- `viewer-readonly-public`

Server emits tailored summaries by fleet rather than one universal payload.

### H3: Tenant segmentation by Product UID + fleet layering

- Product UID as hard tenant boundary
- Fleet for operational segments within tenant

---

## 12. Potentially Missed Blues API Features (Important)

These are the highest-value capabilities that appear underused or not fully leveraged in architecture decisions:

1. **`card.attn` file watch modes** for event-driven host wakeup
2. **`file.changes` trackers** for cheap “what changed?” routing
3. **`note.changes` trackers** for incremental retrieval vs brute `note.get` loops
4. **`hub.signal` + Device API `/signal`** for low-latency cloud→device nudges
5. **DB Notefiles (`.db/.dbs`)** for shared state, versioned config, deterministic reads
6. **Local DB (`.dbx`)** for async transaction caching/host side staging
7. **Environment hierarchy API** (device/fleet/project precedence inspection)
8. **Events cursor APIs** (`/events-cursor`, `/fleets/{fleetUID}/events-cursor`) for scalable ingestion
9. **`selectFields` on Event API** to reduce payload and parsing cost
10. **OAuth token endpoint** (`/oauth2/token`) to avoid long-lived PAT in automation
11. **Device public key APIs** for note-level crypto options without custom TLS cert stores on Opta
12. **`card.usage.get` and `card.usage.test`** to drive data-budget policy from observed usage

---

## 13. Fleet, Device UID, Product UID Strategy Patterns

## 13.1 Product UID (Hard Boundary)

Use Product UID as the **security/tenant/environment boundary**:

- `prod` vs `staging`
- customer A vs customer B
- regulatory boundary separation

Avoid sharing Product UID across unrelated tenants.

## 13.2 Device UID (Precision Routing + Identity)

Use Device UID for:

- deterministic command targeting (`device:<uid>:config.qi`)
- command acknowledgment correlation
- per-device policy overrides (environment variables)
- quarantine actions (disable/enable device API)

## 13.3 Fleet (Operational Grouping)

Use fleet for dynamic grouping:

- server groups (`server-primary`, `server-backup`)
- client cohorts (`region-west`, `pilot`, `high-priority`)
- viewer audiences

## 13.4 Combined Strategy (Recommended)

- Product UID = tenant/env boundary
- Fleet = operational broadcast domain
- Device UID = final command/identity granularity

---

## 14. Security and Certificate Requirements Matrix

| Pattern | Custom security certificates on Opta? | Notes |
|---|---|---|
| Notecard-native queue/DB messaging | **No** | Uses Blues-managed secure transport/session model |
| Notecard `web.*` via Proxy Route | **No** | Route alias + Notehub handles endpoint TLS path |
| Cloud bridge with Notehub API (OAuth) | **No** (on Opta) | TLS/certs handled in cloud runtime |
| Direct HTTPS from Opta via BearSSL | **Yes** | Requires cert chain/time validation lifecycle management |
| Route-based delivery only | **No** | But can add Notehub route config complexity |

---

## 15. Comparison Matrix

| Option | Complexity | Cert burden on Opta | Route dependency | Scalability | Recommendation |
|---|---|---|---|---|---|
| A Notecard-native direct addressing | Low | None | Low | Good | **Strong Yes** |
| B + DB state replication | Medium | None | Low | Very good | **Yes** |
| C Event-driven host loop | Medium | None | Low | Very good | **Yes (high value)** |
| D Minimal cloud bridge | Medium | None | None | Excellent | Conditional |
| E Notecard web.* control plane | Medium | None | Proxy route | Moderate | Conditional |
| F Direct HTTPS BearSSL | High | High | None | Good | Usually No |
| G Hybrid split-plane | Medium | None | Low/None | Excellent | **Yes (mature phase)** |
| H Multi-server/viewer fleets | Medium | None | Low | Excellent | Yes for larger deployments |

---

## 16. Data Usage Analysis by Option

### 16.1 Goal and method

To compare options fairly, separate data usage into:

1. **Business payload usage** (telemetry, alarms, daily reports, config commands)
2. **Architecture overhead** (polling, keepalive behavior, duplicate fanout, control-plane calls)

Business payload is mostly required regardless of architecture. The key optimization target is architecture overhead.

### 16.2 Assumptions used for estimates

Based on Blues Notecard low-bandwidth guidance and prior repo analysis:

- Continuous-mode idle overhead is typically on the order of ~30-60 KB/day depending `sync` behavior and session keepalive activity.
- Non-API note syncs are generally efficient compared to frequent request/response API polling patterns.
- Notecard `web.*` responses are capped (~8192 bytes), and frequent polls can add MB/day overhead.

These are **planning estimates** for architecture comparison, not billing-precision numbers.

### 16.3 Estimated architecture overhead by option (daily)

| Option | Estimated architecture overhead | Why |
|---|---|---|
| A Notecard-native direct addressing | **Lowest: ~0 to +0.1 MB/day** | No API polling loop; normal note sync behavior only |
| B + DB state replication | **Lowest to very low: ~0 to +0.1 MB/day** (often slightly lower effective total traffic than A) | Replaces repeated state chatter with in-place DB updates |
| C Event-driven host loop | **Lowest: ~0 to +0.05 MB/day** | Tracker/ATTN logic is mostly host-side optimization, not extra cellular traffic |
| D Minimal cloud bridge | **Very low on-device: ~0 to +0.1 MB/day** | Device cellular pattern stays mostly native; cloud does API work over internet |
| E Notecard `web.*` control plane | **High: ~1 to +10 MB/day** depending poll interval and payload sizes | Repeated request/response polling over cellular adds significant overhead |
| F Direct HTTPS from Opta (Ethernet) | **Very low cellular overhead on server side; fleet total near A/C** | API traffic moves to Ethernet; client/viewer Notecard traffic remains |
| G Hybrid split-plane | **Low: ~0.05 to +0.3 MB/day** | Core data native; occasional API/admin control activity |
| H Multi-server/viewer fanout | **Variable; can be High** (often +50% to +100% on duplicated upstream publishes) | Same event may be intentionally sent to multiple fleets/consumers |

### 16.4 Most important minimization insights

1. **Avoid frequent Notecard `web.get` polling** (largest avoidable overhead).
2. Prefer **native note routing patterns (fleet/device targeting)** for daily operation.
3. Use **DB notefiles for state** to avoid repetitive command/state churn.
4. Keep **fanout deliberate** (H options can multiply bytes quickly).
5. Use **cursor-based cloud ingestion** for analytics/admin instead of device-side polling.

### 16.5 Ranked best options for minimum data usage

From lowest to highest architecture overhead (typical deployments):

1. **C** Event-driven host loop (with A baseline)
2. **A** Notecard-native direct addressing
3. **B** Notecard-native + DB state replication
4. **D** Minimal cloud bridge
5. **G** Hybrid split-plane
6. **F** Direct HTTPS from Opta (cellular remains low; operational complexity high)
7. **H** Multi-fanout topologies (can be expensive if duplicated flows are broad)
8. **E** Notecard `web.*` polling control plane

### 16.6 Practical recommendation if data usage is the top priority

Use **A + C + selective B**:

- A for core routing
- C for efficient inbound processing (trackers + optional ATTN)
- B for state/ack channels to prevent repeat traffic

Then measure with:

- `card.usage.get` (device side)
- Notehub usage views per device/fleet

and tune:

- outbound/inbound intervals (especially solar/power modes)
- alarm burst behavior
- viewer summary frequency

---

## 17. Recommended Roadmap

### Phase 1 (immediate, low risk)

1. Standardize on Option A as explicit baseline architecture document
2. Add Option C event loop improvements:
	 - `file.changes` / `note.changes` trackers
	 - optional `card.attn` watch for changed files
3. Normalize naming + schema versioning across all command and telemetry bodies

### Phase 2 (stability + operability)

1. Introduce selected Option B DB files for state and ack paths
2. Add command IDs + idempotency checks for relay/config/location requests
3. Add usage-driven sync tuning (`card.usage.get/test` + measured policy)

### Phase 3 (scale/admin)

1. Add Option G split-plane controls for admin/audit/bulk operations
2. Use OAuth client credentials for API automation
3. Add fleet cursor ingestion for analytics and health dashboards

---

### Final Recommendation (Short Form)

If you want to avoid custom security certificates, the best path is:

- Stay **Notecard-native** for primary messaging
- Upgrade host processing with **change trackers + ATTN**
- Add selective **DB state replication** where queue semantics are weak
- Use Notehub API externally (OAuth) only for advanced operations

This gives you a robust architecture without the maintenance burden of MCU TLS certificate management.

---

## 18. Conclusion: Best Communication Architecture

### Final decision recommendation

The best overall communication architecture for this project is:

## **Option A + Option C + Selective Option B**

- **Option A (core transport):** Notecard-native direct addressing (`fleet:<server-fleet>:*.qi` and `device:<uid>:*.qi`)
- **Option C (processing model):** Event-driven host processing using `file.changes` / `note.changes` trackers, with optional `card.attn`
- **Selective Option B (state channels):** DB notefiles (`.db/.dbs`) only where state/idempotency matters (config revision, command acks, latest viewer snapshot)

This combination gives the best balance of:

- Lowest practical cellular data usage
- No custom certificate lifecycle on Opta
- High reliability and recoverability
- Minimal architecture complexity increase from your current codebase

### Hardware requirements

### Required (production baseline)

1. **Client nodes**
	- Arduino Opta (or chosen Client MCU in current platform)
	- Blues Notecard (Cell or Cell+WiFi model per deployment)
	- Sensor wiring/power as currently defined

2. **Server node**
	- Arduino Opta server
	- Blues Notecard
	- Ethernet network connectivity for dashboard/API endpoints

3. **Viewer nodes (if used)**
	- Arduino Opta viewer
	- Blues Notecard

4. **Cloud services**
	- Blues Notehub project
	- Fleet definitions (server, clients, and optional viewer fleets)

### Optional (not required for recommended architecture)

- Raspberry Pi / cloud bridge host (only for Option D/G advanced control plane)
- Additional backup server hardware (only for multi-server Option H)

### Explicitly not required for recommended architecture

- No custom TLS certificate store on Opta firmware
- No BearSSL integration for direct Notehub API calls from MCU

### Software setup requirements

### Required firmware behaviors

1. **Addressing and file conventions**
	- Client publishes telemetry/events to server fleet-targeted `.qi` files
	- Server sends per-device commands via `device:<uid>:<file>.qi`
	- Viewer summary delivery uses either per-device or viewer fleet convention

2. **Host inbound processing**
	- Use `file.changes` tracker to identify changed files
	- Use `note.changes` tracker for incremental note retrieval
	- Retain `note.get(... delete:true)` only as fallback path where needed

3. **State and idempotency (selective DB usage)**
	- Add DB notes for command/version/ack state where replay ambiguity exists
	- Include command IDs, revision numbers, and timestamps in control payloads

4. **Sync and power tuning**
	- Keep current power-mode strategy (solar vs continuous behavior)
	- Tune inbound/outbound intervals using observed `card.usage.get` results

5. **Operational controls**
	- Define fleet membership policy and naming conventions
	- Define Product UID policy for environment/tenant boundaries

### Notehub setup requirements

1. Create/maintain fleets (at minimum):
	- `tankalarm-server` (or equivalent)
	- `tankalarm-clients`
	- optional `tankalarm-viewers`

2. Ensure each device is assigned correctly to fleet(s)
3. Keep Product UID aligned across devices in same deployment boundary
4. Use environment variables for policy flags where practical

### Security setup requirements

- Use standard Blues transport security path (no custom cert provisioning on Opta)
- If external automation is introduced later, use OAuth client credentials for Notehub API
- Avoid long-lived PAT usage in embedded/device firmware

### Pros of the recommended architecture

1. **Lowest data overhead among practical options**
	- Avoids heavy `web.*` polling cost
	- Preserves efficient Notecard sync semantics

2. **No custom certificate management on MCU**
	- Eliminates cert rotation burden and TLS stack complexity on Opta

3. **High reliability with low operational risk**
	- Uses established Notecard queue/DB patterns
	- Strong offline recovery behavior

4. **Incremental adoption from current codebase**
	- Builds directly on current device/fleet-targeted implementation
	- No full server rewrite required

5. **Scales cleanly**
	- Device UID for precision commands
	- Fleet for operational fanout
	- Product UID for hard environment/tenant boundaries

6. **Clear growth path**
	- Can later add split-plane cloud control (Option G) without redesigning data plane

### Cons of the recommended architecture

1. **Still requires disciplined message/schema governance**
	- Need stable payload schema versioning and command IDs

2. **Tracker lifecycle complexity**
	- `file.changes` / `note.changes` trackers must be managed correctly (`start/reset/stop`)

3. **Some implementation work still needed**
	- Event-driven processing and selective DB channels are not “zero work”

4. **Less API-level central control than full cloud bridge**
	- Fleet-wide analytics/admin workflows are possible but less centralized unless Option G/D is added

5. **Fanout design must be intentional**
	- Multi-server/viewer duplication can raise data usage if not tightly scoped

### Bottom line

For your stated priorities—**minimize data usage** and **avoid custom security certificates**—the right choice is to standardize on:

- **Notecard-native communication (A)**
- **Event-driven host processing (C)**
- **Selective DB state channels (B)**

This is the most efficient and lowest-risk architecture for the current TankAlarm platform.

---

## 19. Implementation Checklist (One-Page)

Use this as the direct execution checklist for the selected architecture: **Option A + Option C + selective Option B**.

### A) Firmware tasks (Server, Client, Viewer)

### 1. Message/addressing standardization

- [ ] Confirm and document canonical file map:
	- Client → Server: `fleet:<serverFleet>:telemetry.qi`, `alarm.qi`, `daily.qi`, `unload.qi`, `serial_log.qi`, `location_response.qi`
	- Server → Client: `device:<uid>:config.qi`, `relay.qi`, `serial_request.qi`, `location_request.qi`
	- Server → Viewer: `device:<viewer-uid>:viewer_summary.qi` or viewer fleet equivalent
- [ ] Remove/retire any legacy route-dependent `_target`-only assumptions where no longer needed

### 2. Server inbound processing upgrade (Option C)

- [ ] Add `file.changes` tracker for server inbound files
- [ ] Add `note.changes` tracker per active inbound file
- [ ] Process only changed files/notes each loop iteration
- [ ] Keep existing `note.get(...delete:true)` as fallback path for recovery/troubleshooting
- [ ] Add tracker reset/start behavior on boot/recovery

### 3. Optional ATTN integration (Option C)

- [ ] Configure `card.attn` file-watch mode for relevant inbound files
- [ ] Rearm/disarm logic implemented safely after each processing cycle
- [ ] Validate behavior for no-change, single-change, and burst-change scenarios

### 4. State/idempotency channels (Selective Option B)

- [ ] Add DB notefiles for control-state only where needed (do not overuse):
	- Example: `client_state.db` (revision + last applied command ID)
	- Example: `viewer_snapshot.db` (latest summary state)
- [ ] Add command ID + revision + timestamp fields to command payloads
- [ ] Enforce duplicate/replay suppression on command apply path

### 5. Data usage controls

- [ ] Audit all `sync:true` call sites and keep only where low-latency is required
- [ ] Keep/tune solar vs continuous mode settings for clients
- [ ] Add periodic `card.usage.get` capture to diagnostics output/logging

### 6. Validation tests (firmware)

- [ ] Client telemetry delivery test (normal cadence)
- [ ] Server command delivery test (config, relay, serial request, location request)
- [ ] Offline buffering/retry test (Notecard unavailable then restored)
- [ ] Reboot recovery test (trackers + DB state + pending commands)
- [ ] Viewer summary delivery test (single viewer and optional fleet)

### B) Notehub setup tasks

### 1. Product/fleet structure

- [ ] Verify correct Product UID for target deployment environment
- [ ] Create/verify fleets:
	- `tankalarm-server`
	- `tankalarm-clients`
	- optional `tankalarm-viewers`
- [ ] Assign all devices to correct fleet(s)

### 2. Environment variable governance

- [ ] Define project-level defaults (safe baseline)
- [ ] Define fleet-level overrides (operational segmentation)
- [ ] Use device-level overrides only when required
- [ ] Validate effective hierarchy for sample devices

### 3. Route posture

- [ ] Keep only essential external routes (if still needed for SMS/email)
- [ ] Confirm no legacy inter-device routes are still required by active firmware paths

### 4. Observability and operations

- [ ] Create dashboard filters for key files (`telemetry`, `alarm`, `daily`, `config`, `relay`)
- [ ] Verify event throughput for representative busy period
- [ ] Set alerting/troubleshooting procedure for missing telemetry and failed command acks

### C) Security and access tasks

- [ ] Confirm no custom Opta TLS certificate store is required for chosen architecture
- [ ] Keep API automation credentials out of firmware (if later using cloud control plane)
- [ ] Prefer OAuth client credentials over long-lived PAT for external automation

### D) Deployment gate (go/no-go)

Mark production-ready only when all are true:

- [ ] End-to-end comms pass (client/server/viewer)
- [ ] Data usage stays within expected budget over pilot window
- [ ] Offline/recovery behavior verified
- [ ] Fleet assignments and Product UID controls validated
- [ ] Operations team has runbook for tracker reset/recovery and Notehub checks

---

*Document generated as an expanded architecture review, February 19, 2026.*
