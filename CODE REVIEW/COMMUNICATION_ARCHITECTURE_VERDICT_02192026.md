# Communication Architecture Verdict & Recommendation

**Date:** February 19, 2026  
**Reviewed Documents:**
- `SERVER_CLIENT_COMMUNICATION_OPTIONS_OUT_OF_THE_BOX.md` (Feb 19, 2026)
- `SERVER_CLIENT_VIEWER_COMMUNICATION_OPTIONS_02192026.md` (Feb 19, 2026)

**User Constraints:**
1. Avoid high data usage
2. Avoid SSL certificates
3. Routes are OK, but should be well documented and streamlined

---

## CRITICAL FINDING: Current Code Has a Known Bug

Before comparing the two documents, a critical issue must be addressed. **Both documents assume the `device:<uid>:file.qi` and `fleet:<name>:file.qi` patterns work as Notecard `note.add` file parameters. They do not.**

### Evidence

1. **Blues `note.add` API documentation** explicitly states the `file` parameter on the Notecard must end in one of: `.qo`, `.qos`, `.db`, `.dbs`, `.dbx`. The `.qi` extension is NOT valid for `note.add`. Inbound queue files (`.qi`) can only be read via `note.get`, not created via `note.add` on the Notecard.

2. **Colons in notefile names are rejected** by Notecard firmware. The earlier debugging session (Phase 4) produced the error: `"notefile names may not include a : character"`. This is a firmware-level validation error.

3. **The Notehub API** is the official mechanism for adding `.qi` notes to a specific device: `POST /v1/projects/{projectUID}/devices/{deviceUID}/notes/{notefileID}`. This is a cloud-side API call, not a Notecard-side call.

4. **The code has been reverted** to use these broken patterns:
   - Server: `snprintf(targetFile, ..., "device:%s:config.qi", clientUid)` then `note.add`
   - Client: `snprintf(targetFile, ..., "fleet:%s:%s", fleetName, fileName)` then `note.add`
   
   These calls will fail at the Notecard level.

### What This Means for the Two Documents

| Document | Core Assumption | Validity |
|---|---|---|
| 02192026 (Option A) | `device:<uid>:file.qi` works natively in `note.add` | **Invalid** — produces Notecard error |
| Out-of-the-Box | Universal Proxy via `web.post` through Proxy Route | **Valid** — documented Blues feature |

The 02192026 document's Option A recommendation is built on an invalid assumption. However, its Option C (change trackers), Option B (DB state), and general architectural principles are excellent and fully valid.

The Out-of-the-Box document's Universal Proxy concept is technically valid and addresses the inter-device communication gap, but at a higher cellular data cost than necessary for routine operations.

---

## Correct Blues Inter-Device Communication Patterns

Since Notecards cannot directly address each other, all inter-device communication must go through Notehub. The valid mechanisms are:

### 1. Notehub Route Relay (Standard Pattern)
- Device A sends `note.add` to `outbound_file.qo` with target info in the body
- A Notehub Route catches the event and uses JSONata/transform to add a `.qi` note to Device B
- Device B reads from `inbound_file.qi` via `note.get`
- **Data cost:** Near zero overhead (normal sync traffic)
- **Latency:** Dependent on sync intervals
- **Certificates:** None on device

### 2. Notecard `web.post` via Proxy Route (Universal Proxy)
- Device A sends `web.post` through a Proxy Route to `api.notefile.net`
- The Notecard handles TLS; the Route injects the auth header
- Device A calls `POST /v1/projects/{app}/devices/{targetUID}/notes/config.qi` directly
- **Data cost:** ~0.5–2 KB per API call (cellular)
- **Latency:** Immediate (device must be connected)
- **Certificates:** None on device

### 3. Environment Variables (for Configuration)
- Set variables at fleet/device level via Notehub UI or API
- Devices read via `env.get` or `env.modified` on next sync
- **Data cost:** Near zero (synced as part of normal session)
- **Latency:** Next sync cycle
- **Certificates:** None on device

### 4. Signals API via Proxy Route (for Urgent Commands)
- Device A sends `web.post` via Proxy Route to `/v1/projects/{app}/devices/{targetUID}/signal`
- Ephemeral, low-latency, no storage
- **Data cost:** ~0.5 KB per signal (cellular)
- **Latency:** Immediate if target is connected; dropped if offline
- **Certificates:** None on device

---

## Document Comparison

### Out-of-the-Box Document

**Strengths:**
- Correctly identifies the Universal Proxy as a valid mechanism for inter-device communication
- Excellent ideas about dynamic fleet discovery (eliminating hardcoded client lists)
- Environment Variables for configuration is a significant simplification
- Signals API for low-latency relay control is well-conceived
- One Route to rule them all — elegant and streamlined

**Weaknesses:**
- Every management operation costs cellular data (`web.post` round trips)
- 8 KB response limit on `web.get` — fleet device listings can exceed this with >5 devices
- `web.post`/`web.get` block the Notecard during execution (~seconds per call)
- Using Proxy for routine telemetry would be expensive
- Viewer "pull" model via Proxy adds per-viewer polling cost
- Does not address host-side processing optimization (no mention of change trackers)
- Somewhat dismisses standard routing as "old" when it's actually the lowest-cost option

**Data impact:** Medium. API calls for fleet discovery, signals, and config add ~0.1–1 MB/day depending on poll frequency and fleet size.

### 02192026 Document

**Strengths:**
- Excellent identification of `file.changes`, `note.changes` trackers, and `card.attn` as high-value improvements (Option C)
- DB notefiles for state replication (Option B) is the right answer for config/ack/version tracking
- Data usage analysis is thorough and accurate
- Phased implementation roadmap is practical
- Correctly identifies that the biggest improvement is host-side processing, not transport changes
- Security matrix is comprehensive and correct

**Weaknesses:**
- **Critical flaw:** Option A assumes `device:<uid>:file.qi` works natively in `note.add` on the Notecard — it does not
- Does not propose a working mechanism to replace the broken addressing patterns
- The "observed implementation pattern" section describes code that will produce Notecard errors
- Does not mention the Universal Proxy or `web.post` as valid alternatives for device targeting

**Data impact:** If the addressing issue is fixed with routes, the A+C+B recommendation achieves the absolute lowest data usage of any approach.

---

## The Verdict: Hybrid of Both Documents

Neither document is complete on its own. The best architecture combines the 02192026 document's host-side optimizations with the Out-of-the-Box document's working inter-device mechanisms.

### Recommended Architecture: Route Relay (Primary) + Universal Proxy (Management) + Trackers + DB State

This translates to: **02192026's Option A+C+B, but with the broken `device:uid:` addressing replaced by Notehub Route relays for routine traffic, and the Out-of-the-Box Universal Proxy for occasional management operations.**

---

## Recommended Architecture Detail

### Tier 1: Routine Data Flow (Route Relay — Lowest Data Cost)

#### Client → Server (Telemetry, Alarms, Daily Reports)

```
Client Opta                    Notehub                         Server Opta
    |                              |                                |
    |-- note.add                   |                                |
    |   file: "telemetry.qo"      |                                |
    |   body: {tank:85, ...}      |                                |
    |   _target not needed         |                                |
    |-- syncs to Notehub -------->|                                |
    |                              |-- Route: "Client-to-Server" --|
    |                              |   Watches: telemetry.qo,      |
    |                              |   alarm.qo, daily.qo, etc.    |
    |                              |   Transform: Add to server's   |
    |                              |   telemetry.qi via Device API  |
    |                              |-------------------------------->|
    |                              |                                |-- note.get
    |                              |                                |   file: "telemetry.qi"
    |                              |                                |   delete: true
```

**Routes needed:** 1 Route — "Client-to-Server Relay"  
- Watches: `telemetry.qo`, `alarm.qo`, `daily.qo`, `unload.qo`, `serial_log.qo`, `location_response.qo`
- Action: For each event, adds a `.qi` note to the server device
- The Route can use JSONata to transform the event body and inject the source device UID

#### Server → Client (Config, Relay, Location Request, Serial Request)

```
Server Opta                    Notehub                         Client Opta
    |                              |                                |
    |-- note.add                   |                                |
    |   file: "command.qo"        |                                |
    |   body: {                    |                                |
    |     _target: "dev:12345",    |                                |
    |     type: "config",          |                                |
    |     payload: {...}           |                                |
    |   }                          |                                |
    |-- syncs to Notehub -------->|                                |
    |                              |-- Route: "Server-to-Client" --|
    |                              |   Watches: command.qo          |
    |                              |   Transform: Read _target,     |
    |                              |   add to target's config.qi    |
    |                              |   (or relay.qi, etc.)          |
    |                              |-------------------------------->|
    |                              |                                |-- note.get
    |                              |                                |   file: "config.qi"
```

**Routes needed:** 1 Route — "Server-to-Client Relay"  
- Watches: `command.qo` (single consolidated outbound file from server)
- Action: Reads `_target` and `type` from body, adds appropriate `.qi` note to the target device
- JSONata transform extracts target UID and routes to correct `.qi` file

#### Server → Viewer

Same pattern as Server → Client. Server sends `viewer_summary.qo` with `_target` = viewer device UID. Route adds `viewer_summary.qi` to the viewer.

**Total Routes for Tier 1:** 2 Routes (Client-to-Server, Server-to-Client)  
**Plus existing:** SMS Route, Email Route (if using Blues `sms.qo`/`email.qo` — these are system-level and separate)

### Tier 2: Management Operations (Universal Proxy — Occasional Use)

For operations that don't fit the route relay model, add the Universal Proxy from the Out-of-the-Box document:

**1 additional Route:** "UniversalProxy"  
- URL: `https://api.notefile.net/`  
- Header: `Authorization: Bearer <access_token>`

Use cases:
- **Fleet discovery:** Server periodically (e.g., once per hour) calls `web.get` to list devices in the client fleet. Eliminates hardcoded client UIDs.
- **Device health check:** Query device connectivity status before sending commands
- **Urgent relay commands:** Use Signals API (`/signal` endpoint) for immediate relay control when the standard route relay latency is too slow
- **Environment variable updates:** Push config changes to fleet/device level

**Data cost:** Minimal if limited to management operations. Budget ~0.05–0.2 MB/day.

### Tier 3: Host-Side Optimization (Change Trackers + ATTN)

This is the 02192026 document's Option C — the highest-value improvement with zero additional cellular cost:

**Server inbound processing upgrade:**
```cpp
// BEFORE (current): Brute-force polling 7+ files every loop
for (int i = 0; i < NUM_NOTEFILES; i++) {
    while (note.get(files[i], delete:true)) { processNote(); }
}

// AFTER: Event-driven with trackers
// 1. Check what changed
J *req = notecard.newRequest("file.changes");
JAddStringToObject(req, "tracker", "server_inbound");
// Returns: {"changes":2, "info":{"telemetry.qi":{"changes":2}}}
// 2. Only process changed files
// 3. Use note.changes with tracker for incremental retrieval
```

**Optional ATTN integration:**
```cpp
// Arm file-watch for inbound files
J *req = notecard.newRequest("card.attn");
JAddStringToObject(req, "mode", "arm,files");
// Watch for changes to any .qi file
```

### Tier 4: State Channels (Selective DB Notefiles)

This is the 02192026 document's Option B — for state that needs in-place update semantics:

| DB Notefile | Purpose | Updated By |
|---|---|---|
| `client_state.db` | Last config revision, relay state, firmware version | Client (note.add with note ID per field) |
| `viewer_snapshot.db` | Latest tank levels for dashboard | Server |
| `command_ack.db` | Command acknowledgment state | Client after executing command |

**Benefits:** Eliminates "stale command replay" after reboot. Server can check `client_state.db` to know if a config was applied without waiting for a confirmation note.

---

## Configuration: Replace Config Notes with Environment Variables

The Out-of-the-Box document's environment variable proposal is excellent for configuration that changes infrequently:

| Variable | Level | Purpose |
|---|---|---|
| `_poll_rate` | Fleet: tankalarm-clients | Telemetry interval in minutes |
| `_alarm_high` | Fleet: tankalarm-clients | High-level alarm threshold (%) |
| `_alarm_low` | Fleet: tankalarm-clients | Low-level alarm threshold (%) |
| `_tank_capacity` | Device-level per client | Individual tank capacity |
| `_relay_mode` | Device-level per client | Relay control policy |

**Client firmware reads these on sync:**
```cpp
J *req = notecard.newRequest("env.get");
JAddStringToObject(req, "name", "_poll_rate");
J *rsp = notecard.requestAndResponse(req);
int pollRate = JGetInt(JGetObjectItem(rsp, "text"));
```

**Benefit:** Change config for ALL clients by updating one fleet variable in the Notehub UI. No firmware upload, no notes, no routes. Zero data cost beyond normal sync.

---

## Route Documentation (Streamlined)

### Route 1: Client-to-Server Relay

| Setting | Value |
|---|---|
| **Name** | `ClientToServerRelay` |
| **Type** | General HTTP/HTTPS |
| **URL** | `https://api.notefile.net/v1/projects/{projectUID}/devices/{serverDeviceUID}/notes/` |
| **Append** | The source notefile name (e.g., `telemetry.qi`) using JSONata |
| **Method** | POST |
| **Headers** | `Authorization: Bearer <PAT>` |
| **Filter** | Notefiles: `telemetry.qo`, `alarm.qo`, `daily.qo`, `unload.qo`, `serial_log.qo`, `location_response.qo` |
| **Filter** | Fleets: `tankalarm-clients` only |
| **Transform** | JSONata: Include source device UID in body (`_source` field) |
| **Body** | Original note body + `_source` field |

**Purpose:** Every outbound note from a client automatically appears as an inbound note on the server's Notecard.

### Route 2: Server-to-Client Relay

| Setting | Value |
|---|---|
| **Name** | `ServerToClientRelay` |
| **Type** | General HTTP/HTTPS |
| **URL** | Dynamic: `https://api.notefile.net/v1/projects/{projectUID}/devices/` + `body._target` + `/notes/` + `body._file` |
| **Method** | POST |
| **Headers** | `Authorization: Bearer <PAT>` |
| **Filter** | Notefiles: `command.qo` |
| **Filter** | Fleets: `tankalarm-server` only |
| **Transform** | JSONata: Extract `_target` (target device UID) and `_file` (target .qi filename) from body. Send remaining body fields as the .qi note body. |
| **Body** | Extracted command payload (without `_target` and `_file` fields) |

**Purpose:** Server sends a single consolidated `command.qo` note with target routing info. The Route unpacks it and delivers to the correct client's inbound queue.

### Route 3: Universal Proxy (Management)

| Setting | Value |
|---|---|
| **Name** | `UniversalProxy` |
| **Type** | Proxy for `web.post`/`web.get` |
| **URL** | `https://api.notefile.net/` |
| **Headers** | `Authorization: Bearer <PAT>` |

**Purpose:** Enables `web.post`/`web.get` calls from any device to the Notehub API for management operations (fleet discovery, signals, health checks).

### Routes 4-5: External Services (Existing)

| Route | Purpose |
|---|---|
| SMS Route | Blues `sms.qo` → Twilio/carrier SMS delivery |
| Email Route | Blues `email.qo` → Email service delivery |

**Total Routes: 3 new + 2 existing = 5 total (well-documented, streamlined)**

---

## Data Usage Estimate

| Traffic Type | Daily Estimate | Notes |
|---|---|---|
| Client telemetry (normal) | ~50-200 KB/device | Based on current note sizes and intervals |
| Server commands (config/relay) | ~5-20 KB/device | Infrequent, small payloads |
| Route relay overhead | ~0 additional | Routes transform events server-side at no cellular cost |
| Change tracker processing | ~0 additional | Host-side optimization only |
| Universal Proxy management | ~10-50 KB total | Fleet discovery 1x/hour + occasional signals |
| Notecard sync sessions | ~30-60 KB/device | Standard keepalive and session overhead |
| **Daily total per device** | **~100-350 KB** | Well within 500 MB/year Blues plan |

---

## Implementation Roadmap

### Phase 1: Fix the Broken Addressing (Critical — Do First)

**Goal:** Make inter-device communication work correctly.

1. **Server firmware:**
   - Replace `device:<uid>:config.qi` pattern with `command.qo` + `_target` body field
   - Consolidate all server→client sends into a single `command.qo` file with `_target`, `_file`, and payload
   - Keep `note.get(...delete:true)` for reading inbound `.qi` files (this is correct)

2. **Client firmware:**
   - Replace `fleet:<name>:telemetry.qi` pattern with standard `telemetry.qo` (no prefix)
   - All outbound notes use plain `.qo` extensions
   - Keep `note.get(...delete:true)` for reading inbound `.qi` files

3. **Notehub setup:**
   - Create Route 1 (Client-to-Server Relay)
   - Create Route 2 (Server-to-Client Relay)
   - Create fleet assignments (tankalarm-server, tankalarm-clients, tankalarm-viewers)
   - Test end-to-end message delivery

### Phase 2: Host Optimization (High Value — Low Risk)

**Goal:** Reduce CPU churn and improve responsiveness.

1. Add `file.changes` tracker for server inbound processing
2. Add `note.changes` tracker per active inbound file
3. Replace brute-force polling loops with tracker-driven processing
4. Optionally add `card.attn` file-watch mode

### Phase 3: Configuration Modernization (Medium Value)

**Goal:** Simplify config management.

1. Create Route 3 (Universal Proxy)
2. Move static config parameters to fleet Environment Variables
3. Add `env.get`/`env.modified` handling to client firmware
4. Add fleet discovery via Universal Proxy to server firmware (optional)

### Phase 4: State Channels (Stability)

**Goal:** Improve reliability and idempotency.

1. Add selective `.db` notefiles for state tracking
2. Add command IDs and revision numbers to command payloads
3. Add duplicate/replay suppression
4. Add `card.usage.get` monitoring

### Phase 5: Advanced Features (Scale)

**Goal:** Production hardening.

1. Signals API for urgent relay commands
2. OAuth client credentials for external automation
3. Viewer snapshot DB for deterministic state reads
4. Multi-viewer fleet support

---

## Final Summary

| Criterion | Recommendation |
|---|---|
| **Primary transport** | Route Relay (2 routes) for all routine inter-device communication |
| **Management operations** | Universal Proxy (1 route) for fleet discovery, signals, health checks |
| **Configuration** | Environment Variables (no routes, no notes) |
| **Host processing** | Change trackers + optional ATTN (zero data cost) |
| **State management** | Selective DB notefiles for config/ack/version tracking |
| **External services** | Existing SMS + Email routes |
| **Total routes** | 5 (3 new + 2 existing) |
| **SSL certificates on Opta** | **None required** |
| **Data usage overhead** | **Minimal** (~50 KB/day management overhead beyond business data) |

### What to Take from Each Document

| From 02192026 Document | From Out-of-the-Box Document |
|---|---|
| Option C: Change trackers + ATTN (high value) | Universal Proxy concept (valid, use for management) |
| Option B: DB state replication (selective use) | Environment Variables for configuration (excellent) |
| Phased roadmap approach | Dynamic fleet discovery (eliminates hardcoded UIDs) |
| Data usage analysis methodology | Signals API for urgent commands |
| Security matrix (correct) | Single-route auth token isolation |

### What to Discard

| From 02192026 Document | From Out-of-the-Box Document |
|---|---|
| Option A's assumption that `device:uid:file.qi` works in `note.add` | Using Proxy for routine telemetry (too expensive) |
| "Observed implementation pattern" section (describes broken code) | Viewer pull model via Proxy (unnecessary polling cost) |
| | "Zero routes" framing (routes are actually cheap and correct) |

---

## Hardware Requirements

This section documents all hardware, firmware, software, and cloud service requirements for the recommended architecture.

### Per-Node Hardware

#### Client Node (Remote Tank Monitoring — 1 per tank site)

| Qty | Component | Part Number | Est. Cost | Purpose |
|-----|-----------|-------------|-----------|---------|
| 1 | Arduino Opta Lite | AFX00003 | $151 | Industrial controller (STM32H747XI dual-core Cortex-M7/M4, 2 MB Flash, 1 MB RAM) |
| 1 | Blues Wireless for Opta | — | $170 | Notecard carrier board — plugs into Opta expansion slot, provides I2C bridge to Notecard |
| 1 | Blues Notecard Cellular (NA) | NOTE-NBGL-500 | $45–49 | LTE-M / NB-IoT cellular modem with embedded SIM, TLS engine, and Notehub connectivity |
| 1 | 24V DC Power Supply | — | $30–50 | DIN rail mount, industrial-rated; powers Opta + Notecard carrier |
| 1 | Arduino Opta Ext A0602 (optional) | AFX00007 | $229 | 4-20mA / 0-10V analog expansion for pressure transmitter input |

**Client subtotal: ~$396–$649** (depending on analog expansion)

##### Optional Solar Power System (per remote site without grid power)

| Qty | Component | Est. Cost | Purpose |
|-----|-----------|-----------|---------|
| 1 | Solar Panel (50–100W, 12V nominal) | $80–150 | Primary power source |
| 1 | Solar Charge Controller (SunKeeper-6 or equiv.) | ~$60 | MPPT/PWM charge regulation; Modbus-readable via RS485 |
| 1 | Deep Cycle Battery (Optima D31T or equiv.) | ~$250 | Energy storage for overnight / cloudy periods |
| 1 | Solar Mounting Hardware | ~$50 | Ground or pole mount |

**Solar add-on subtotal: ~$440–510**

#### Server Node (Office / Headquarters — 1 per deployment)

| Qty | Component | Part Number | Est. Cost | Purpose |
|-----|-----------|-------------|-----------|---------|
| 1 | Arduino Opta Lite | AFX00003 | $151 | Industrial controller — runs aggregation, web dashboard, command dispatch |
| 1 | Blues Wireless for Opta | — | $170 | Notecard carrier board |
| 1 | Blues Notecard Cellular (NA) | NOTE-NBGL-500 | $45–49 | Cellular connectivity to Notehub for inter-device communication |
| 1 | Ethernet Cable (Cat5e/Cat6) | — | $5–10 | RJ45 connection to local network for web dashboard serving |
| 1 | 24V DC Power Supply | — | $30–50 | DIN rail mount, industrial-rated |

**Server subtotal: ~$401–430**

**Server network requirements:**
- Ethernet connection to local LAN (for web dashboard access from browsers)
- Cellular coverage for Blues Notecard (LTE-M or NB-IoT band support)
- No internet access required on the Ethernet side — the web dashboard is served locally; all cloud communication goes through the Notecard's cellular link

#### Viewer Node (Optional — Remote Display, 1 per viewing location)

| Qty | Component | Part Number | Est. Cost | Purpose |
|-----|-----------|-------------|-----------|---------|
| 1 | Arduino Opta Lite | AFX00003 | $151 | Industrial controller — receives summary data, serves local web dashboard |
| 1 | Blues Wireless for Opta | — | $170 | Notecard carrier board |
| 1 | Blues Notecard Cellular (NA) | NOTE-NBGL-500 | $45–49 | Cellular connectivity to Notehub for receiving viewer summary notes |
| 1 | Ethernet Cable (Cat5e/Cat6) | — | $5–10 | RJ45 connection to local network for web dashboard serving |
| 1 | 24V DC Power Supply | — | $30–50 | DIN rail mount, industrial-rated |

**Viewer subtotal: ~$401–430**

### Sensors & Field Wiring (per tank)

| Qty | Component | Est. Cost | Purpose |
|-----|-----------|-----------|---------|
| 1+ | Pressure Transmitter 4-20mA (0–5 PSI) | $150–250 | Bottom-mounted tank level measurement (Dwyer 626 series or equiv.) |
| 1+ | Float Switch (N.C.) | $50–100 | High/low level digital detection (Dayton 3BY75 or equiv.) |
| 2+ | M12 Connectors (panel mount + plug) | ~$50 | Weatherproof field wiring connections |
| 1 | Terminal Block (GTB-406 or equiv.) | ~$10 | Field wire termination |

**Sensors subtotal: ~$260–410 per tank**

### Compatible Arduino Opta Models

| Model | Part Number | WiFi | RS485 | Notes |
|-------|-------------|------|-------|-------|
| **Opta Lite** | AFX00003 | No | No | **Recommended** — lowest cost, all features used |
| Opta WiFi | AFX00001 | Yes | No | Works but WiFi not used (Notecard handles wireless) |
| Opta RS485 | AFX00002 | No | Yes | Useful if Modbus sensor integration needed |

### Compatible Blues Notecard Models

| Model | SKU | Coverage | Notes |
|-------|-----|----------|-------|
| **Notecard Cellular (NA)** | NOTE-NBGL-500 | North America LTE-M/NB-IoT | **Recommended** — lowest cost for NA deployments |
| Notecard Cellular (Global) | NOTE-WBEX-500 | Global LTE-M/NB-IoT | For international deployments |
| Notecard Cell (Legacy) | NOTE-CELL-500 | Global 2G/3G fallback | Only if LTE-M/NB-IoT coverage unavailable |

### Sensor Compatibility

The system supports multiple sensor types via the Opta's I/O and optional expansion:

| Sensor Type | Connection | Requires Expansion | Notes |
|---|---|---|---|
| 4-20mA Current Loop | Analog input via Opta Ext A0602 | Yes | Pressure transmitters, level sensors |
| 0-10V Analog | Analog input via Opta Ext A0602 | Yes | General purpose analog sensors |
| Digital On/Off | Opta digital inputs (D0–D7) | No | Float switches, limit switches |
| Pulse/Frequency | Opta digital inputs | No | Flow meters, pulse counters |

---

### Firmware & Software Requirements

#### Firmware Stack (all nodes)

| Component | Version | Purpose |
|-----------|---------|---------|
| Arduino IDE / CLI | 2.x+ | Compilation and upload |
| Arduino Mbed OS Opta Boards | Latest | Board support package for Opta hardware |
| `note-arduino` library | Latest | Blues Notecard C/C++ API — provides `Notecard` class, `J*` JSON helpers |
| `ArduinoJson` library | v7.x | JSON serialization/deserialization for web dashboard, config parsing |
| `Wire` library | Built-in | I2C communication between Opta host and Notecard at address `0x17` |
| `Ethernet` / `PortentaEthernet` | Built-in | Web dashboard HTTP server on Server and Viewer nodes |
| `LittleFS` library | Built-in | Persistent storage on STM32 internal flash (Server node) |
| `IWatchdog` library | Built-in | Hardware watchdog timer (30-second timeout) |

**Firmware version:** `1.0.0` (defined in `TankAlarm_Common.h`)

#### Notecard Firmware

| Requirement | Value |
|---|---|
| Minimum Notecard firmware | v3.5.1+ recommended |
| Communication interface | I2C at `0x17`, 400 kHz (`NOTECARD_I2C_FREQUENCY`) |
| Connection mode | Continuous (Server), Periodic (Client — tunable interval) |

#### Notecard Configuration (per device, set via `hub.set`)

| Parameter | Server | Client | Viewer |
|---|---|---|---|
| `mode` | `continuous` | `periodic` (tunable) | `periodic` |
| `product` | `product:com.your-company:tankalarm` | Same | Same |
| `sn` | User-defined serial number | User-defined | User-defined |
| `sync` | `true` | `false` (sync on demand/interval) | `false` |

---

### Cloud Service Requirements

#### Blues Notehub (Required)

| Item | Details |
|---|---|
| **Notehub Account** | Free tier at [notehub.io](https://notehub.io) |
| **Project** | 1 Notehub project for entire TankAlarm deployment |
| **Product UID** | 1 Product UID shared across all devices in the deployment |
| **Cellular Data Plan** | Blues prepaid — 500 MB included with Notecard purchase; additional via pay-as-you-go |
| **Estimated data usage** | ~100–350 KB/day per device (see Data Usage Estimate section above) |

#### Notehub Fleet Configuration (Required)

| Fleet Name | Devices | Purpose |
|---|---|---|
| `tankalarm-server` | Server Opta(s) | Route filter: only server receives client telemetry relay |
| `tankalarm-clients` | All Client Optas | Route filter: only clients receive server commands; environment variable target |
| `tankalarm-viewers` (optional) | Viewer Opta(s) | Route filter: viewers receive summary data |

#### Notehub Routes (Required — 5 Total)

| # | Route Name | Type | Purpose |
|---|---|---|---|
| 1 | `ClientToServerRelay` | General HTTP/HTTPS → Notehub Device API | Relays client `.qo` events to server's `.qi` inbound queue |
| 2 | `ServerToClientRelay` | General HTTP/HTTPS → Notehub Device API | Relays server `command.qo` to targeted client's `.qi` files |
| 3 | `UniversalProxy` | Proxy (for `web.post`/`web.get`) | Management: fleet discovery, signals, health checks |
| 4 | SMS Route | Blues system route | `sms.qo` → Twilio/carrier for alarm SMS delivery |
| 5 | Email Route | Blues system route | `email.qo` → Email service for daily reports |

#### Notehub Environment Variables (Recommended)

Set at fleet level (`tankalarm-clients`) for global client configuration:

| Variable | Default | Purpose |
|---|---|---|
| `_poll_rate` | `15` | Telemetry reporting interval in minutes |
| `_alarm_high` | `90` | High-level alarm threshold (%) |
| `_alarm_low` | `10` | Low-level alarm threshold (%) |

Device-level overrides for per-tank settings:

| Variable | Purpose |
|---|---|
| `_tank_capacity` | Individual tank capacity (gallons/liters) |
| `_relay_mode` | Relay control policy for this specific client |

#### Authentication

| Token Type | Where Stored | Where Used |
|---|---|---|
| Notehub Programmatic Access Token (PAT) | Notehub Route headers (Routes 1, 2, 3) | Authenticates Route API calls to `api.notefile.net` |
| — | **NOT in device firmware** | The Opta firmware never handles API tokens |

---

### Enclosures & Installation

| Component | Specification | Notes |
|---|---|---|
| DIN Rail Enclosure | NEMA 4X rated | Required for outdoor client installations |
| DIN Rail (35mm) | Standard | For mounting Opta controllers inside enclosure |
| Cable Glands | IP67 rated | Weatherproof cable entry for power, sensor, antenna |
| Antenna (external, optional) | SMA, LTE-M/NB-IoT band | If Notecard cellular is inside a metal enclosure |

---

### Deployment Cost Summary

| Deployment | Components | Est. Cost |
|---|---|---|
| **Minimum viable (1 server + 1 client)** | 2× Opta + 2× Carrier + 2× Notecard + power + sensors | **~$900–1,100** |
| **Typical (1 server + 3 clients)** | 4× Opta + 4× Carrier + 4× Notecard + power + sensors | **~$2,400–3,200** |
| **With viewer** | Add 1× Opta + Carrier + Notecard + power | **+$401–430** |
| **With solar per client** | Add solar panel + controller + battery + mount | **+$440–510 per site** |
| **Blues Notehub service** | Included 500 MB with each Notecard; pay-as-you-go beyond | **$0–$10/month typical** |

---

*This verdict recommends the architecture that best satisfies all three constraints: low data usage (Route Relay is cheapest), no SSL certificates (Notecard handles all TLS), and well-documented streamlined routes (5 total, each with a clear purpose).*
