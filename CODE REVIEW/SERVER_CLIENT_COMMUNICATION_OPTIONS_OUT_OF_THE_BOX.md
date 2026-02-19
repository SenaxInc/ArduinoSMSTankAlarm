# Server-Client Communication: "Out of the Box" Options

**Date:** February 19, 2026  
**Context:** Exploring unconventional and advanced Blues Wireless Notecard features to optimize the Tank Alarm architecture.  
**Status:** Proposal for "Next Gen" Architecture

---

## 1. Executive Summary: The "Universal Proxy" Approach

The previous review focused on traditional "Device-to-Database-to-Device" routing. This review proposes a radical shift: **Treating the Notehub API as the primary backend**, rather than just a message broker.

By creating a single "Universal Proxy" route on Notehub, the Server Opta can directly interact with the entire Notehub API. This unlocks capabilities previously thought impossible without an external server (like a Raspberry Pi) or complex SSL libraries.

### Key Benefits
- **Zero Hardcoded UIDs:** Server dynamically discovers clients via API.
- **True Device Management:** Server can rename devices, set environment variables, and check connectivity status.
- **Instant Commands:** Use the `signal` API for low-latency control (bypassing the Note database).
- **No Custom SSL Certificates:** The Notecard handles all TLS/SSL termination.

---

## 2. The "Universal Proxy" Concept

Instead of creating one route for SMS, one for Email, and one for every device interaction, we create **one single route** that points to the Notehub API itself.

### 2.1 Configuration
- **Route Name:** `NotehubAPI` (or `UniversalProxy`)
- **Route URL:** `https://api.notefile.net/`
- **Headers:** `Authorization: Bearer <access_token>` (Stored securely in Notehub environment variables, hidden from device firmware if desired, or passed by device).

### 2.2 How it Works
The Server Opta uses the Notecard's `web.post` (or `web.get`) command to hit any endpoint on the Notehub API.

```json
// Example: Server Opta listing all devices in the 'client-fleet'
{
  "req": "web.get",
  "route": "NotehubAPI",
  "name": "v1/projects/app:1234-5678/fleets/fleet:client-fleet/devices"
}
```

**Why this is "Outside the Box":** 
Normally, `web.post` is used to send data *out* to a third party. Here, we are looping it *back* to Notehub to manage the project itself from within the embedded device.

---

## 3. Dynamic Fleet Management (Fleets & Products)

The user asked: *"What different ways can fleets, Device UID and product UID information be used?"*

### 3.1 Eliminating Hardcoded Client Lists
**Current State:** The Server Opta firmware likely has an array of `ClientDeviceUIDs`.
**New State:** 
1. Assign all Client Notecards to a specific Fleet (e.g., `tankalarm-clients`).
2. Server Opta sends a `web.get` request via the Universal Proxy to list all devices in `tankalarm-clients`.
3. Server parses the JSON response to build its internal list of active clients.
4. **Result:** To add a new tank, you simply configure its Notecard with the correct Product UID and Fleet. The Server automatically "adopts" it on the next scan.

### 3.2 Automated Provisioning via Environment Variables
We can move configuration (poll rates, alarm setpoints) out of the firmware code and into **Fleet Environment Variables**.

1. Server Opta (or Admin) sets a variable `PollRate` = `15` on the `tankalarm-clients` Fleet via API.
2. Notehub automatically syncs this variable to *every* device in that fleet.
3. Client devices read `_poll_rate` from their local environment on startup.
4. **Result:** Global configuration changes without firmware updates or complex individual messaging.

### 3.3 Product UID as a Multi-Tenant Key
If managing multiple independent Tank Alarm deployments (e.g., different customers), the **Product UID** segregates them entirely. The Server Opta can be programmed to only "see" devices within its own Product, ensuring data isolation even if they share the same physical network or backend code.

---

## 4. High-Speed Command & Control ("Signals")

**The Problem:** Normal Notes (`note.add`) are stored in a database. They are reliable but can have latency (sync times, database processing).
**The Solution:** Bit-brained **Signals**.

The Notehub API has a special endpoint: `POST /v1/projects/{app}/devices/{uid}/signal`.
- **Bypasses the Database:** Signals are ephemeral and routed immediately.
- **Low Latency:** designed for "real-time" interactions (turning on a pump, acknowledging an alarm).
- **Transient:** If the device is offline, the signal is dropped (unlike a Note which is queued). This is perfect for "live" controls where you want immediate feedback or failure.

**Implementation:**
Server Opta uses the Universal Proxy to send a `signal` to a specific Client UID to "Turn Relay ON".

---

## 5. Security & Certificates

**Requirement:** *"Avoid custom security certificates if possible."*

The **Universal Proxy** approach is the ultimate solution for this.
1. **No Certs on Opta:** The Arduino Opta communicates with the Notecard over I2C/Serial. No network stack is involved.
2. **Notecard Handles HTTPS:** The Notecard has built-in, managed root certificates for standard Web PKI. It validates the Notehub API certificate automatically.
3. **Secure Tokens:** The Authentication Token for the Notehub API can be injected into the Route headers on the Notehub side. This means the Opta firmware *never even needs to know the API secret*. It just sends a request to "Route: NotehubAPI", and Notehub appends the credentials before looping back to itself. This is **extremely secure**.

---

## 6. Architecture Comparison

| Feature | Option A (Old) | Option B (Universal Proxy) |
| :--- | :--- | :--- |
| **Route Count** | 1 per function (SMS, Email, Relay, etc.) | **1 Total** (The "Universal Proxy") |
| **Client List** | Hardcoded in Firmware | **Dynamic** (API Discovery) |
| **Configuration** | Custom `config.qo` notes | **Native Environment Variables** |
| **Latency** | Standard (Database polling) | **Low** (Using API Signals) |
| **Firmware Changes** | Minimal | **Moderate** (Need to implement JSON parsing for API responses) |
| **Security** | Good | **Excellent** (Secret scanning, token isolation) |

---

## 7. Viewer Integration

The **Viewer** can also utilize this pattern.
1. Viewer joins the `tankalarm-viewer` fleet.
2. Viewer uses Universal Proxy to query `latest` events for specific tank devices.
3. Instead of the Server "pushing" summaries to the Viewer, the Viewer "pulls" data directly from the Notehub Cloud via the Application API.
4. **Benefit:** The Server Opta doesn't need to know the Viewer exists. The Viewer is just another authorized client consumption point.

---

## 8. Conclusion: The Recommended "Hybrid" Architecture

After analyzing feasibility and data costs, the definitive recommendation is a **Hybrid Architecture**:
1.  **Use Standard Routing (`.qo` -> `.qi`)** for high-volume *Telemetry* (Sensor Data).
2.  **Use Universal Proxy (`web.post`)** for low-volume *Management* (Discovery, Config, Instant Control).

This provides the "Best of Both Worlds": the cost-efficiency of batched notes for logging, and the power/speed of the API for fleet orchestration.

### 8.1 Required Hardware & Setup

| Component | Hardware Requirement | Software / Config Requirement |
| :--- | :--- | :--- |
| **Server Opta** | Standard Arduino Opta RS485/WiFi | • Firmware Library: `ArduinoJson` (v7+) for API parsing.<br>• Allocate 4KB+ RAM for JSON buffers.<br>• **NO** Ethernet SSL client needed (Notecard handles TLS). |
| **Client Optas** | Standard Arduino Opta | • Firmware: Supports `Environment Variable` callbacks.<br>• Fleet: Assigned to e.g., `tankalarm-clients` fleet. |
| **Viewer Opta** | Standard Arduino Opta + Display | • Firmware: Polling logic via Universal Proxy.<br>• Fleet: Assigned to `tankalarm-viewer` fleet. |
| **Blues Notecard** | Notecard Cellular (NB-IoT/Cat-M) | • Firmware: v3.5.1+ recommended.<br>• Product UID: Single UID for entire project. |

### 8.2 Detailed Software Implementation Steps

#### A. Notehub Configuration (The "Universal Proxy")
1.  **Create Proxy Route:** "NotehubAPI" pointing to `https://api.notefile.net`.
2.  **Authentication:** Add Header `Authorization: Bearer <access_token>` (Generate a Programmatic Access Token in Notehub Settings).
3.  **Environment Variables:** Define standard vars at the **Fleet Level**:
    *   `_poll_rate`: 15 (minutes)
    *   `_alarm_high`: 90 (percent)
    *   `_alarm_low`: 10 (percent)

#### B. Server Firmware Logic
1.  **On Startup:** Run `web.get` to `NotehubAPI/v1/projects/{id}/fleets/{id}/devices`. Parse JSON to build the `ClientList`.
2.  **Periodic (15m):** Run `hub.sync`. Read inbound `telemetry.qi` notes (routed normally) to update tank levels.
3.  **On User Command:** Run `web.post` to `NotehubAPI/.../signal` to toggle relays instantly on clients.

#### C. Client Firmware Logic
1.  **On Startup:** Read environment variables (`env.get`) to set local alarm thresholds.
2.  **On Loop:** Read sensors. If change > 5%, `note.add`. If stable, buffer.
3.  **On Signal:** Handle `hub.signal` interrupt for immediate relay switching.

### 8.3 Pros & Cons of the Hybrid Approach

| Category | Pros | Cons |
| :--- | :--- | :--- |
| **Scalability** | **Excellent.** Zero hardcoded UIDs. Add a tank by simply turning it on. | Server RAM usage increases with fleet size (storing list of clients). |
| **Cost** | **Optimized.** Bulk data uses cheap batching. API calls are reserved for high-value events. | Universal Proxy usage *can* be expensive if bugs cause loop-polling. |
| **Maintenance** | **Low.** Change global settings (poll rates) from the web dashboard instantly. | Requires managing an API Token in Notehub Route settings. |
| **Complexity** | **Medium.** Eliminates custom SSL code on Arduino, but requires robust JSON parsing. | Debugging API errors requires checking Notehub "Route Log" events. |
| **Reliability** | **High.** Telemetry is durable (store-and-forward). Signals are fast (fire-and-forget). | API availability depends on Notehub cloud uptime (99.9%). |

### 8.4 Final Verdict
This architecture removes the most brittle parts of the old design (hardcoded lists, custom `config.qo` parsing) while respecting the constraints of embedded data plans. It makes the Arduino Opta act like a modern "Cloud-Native" edge controller.

---

## 9. Implementation Checklist

Use this checklist to migrate your system to the new "Hybrid Architecture".

### Phase 1: Notehub Setup (Cloud)
- [ ] **Create Proxy Route**
    - [ ] Name: `UniversalProxy`
    - [ ] URL: `https://api.notefile.net/`
    - [ ] HTTP Method: `POST` (Handles both GET/POST requests from device)
    - [ ] Header: `Authorization: Bearer <your-programmatic-access-token>` (Generate in Notehub Settings -> Developer)
- [ ] **Configure Fleets**
    - [ ] Create Fleet: `tankalarm-clients` (Assign all Client Optas here)
    - [ ] Create Fleet: `tankalarm-server` (Assign Server Opta here)
    - [ ] Create Fleet: `tankalarm-viewer` (Assign Viewer Optas here)
- [ ] **Set Fleet Environment Variables** (in `tankalarm-clients` Fleet Settings)
    - [ ] `_poll_rate`: `15` (minutes)
    - [ ] `_alarm_high`: `90` (percent)
    - [ ] `_alarm_low`: `10` (percent)

### Phase 2: Client Firmware (Sensor Nodes)
- [ ] **Remove Legacy Config**
    - [ ] Delete `config.qi` handling logic.
    - [ ] Delete `config_dispatch.qo` handlers.
- [ ] **Add Environment Support**
    - [ ] Implement `env.modified` check on startup and in main loop.
    - [ ] Function: `updateSettingsFromEnv()` reading `_poll_rate`, `_alarm_high`.
- [ ] **Add Signal Support**
    - [ ] Implement `hub.set` with `mode:continuous` if real-time control is needed (or short sync interval).
    - [ ] Function: Handle interrupted/polled signals for Relay control (`{"body":{"relay":true}}`).

### Phase 3: Server Firmware (Aggregator)
- [ ] **Add Libraries**
    - [ ] Include `ArduinoJson` (v6 or v7).
- [ ] **Implement Discovery**
    - [ ] New Function: `fetchClientList()`
    - [ ] Logic: `web.post` -> Route `UniversalProxy` -> Path `v1/projects/{app}/fleets/{fleet}/devices`.
    - [ ] Parse JSON response -> Populate local `ClientNode` array.
- [ ] **Implement Control**
    - [ ] New Function: `sendRelayCommand(targetUID, state)`
    - [ ] Logic: `web.post` -> Route `UniversalProxy` -> Path `v1/projects/{app}/devices/{uid}/signal`.

### Phase 4: Viewer Firmware (Reference)
- [ ] **Implement Pull Logic**
    - [ ] New Function: `fetchTankSummary(targetUID)`
    - [ ] Logic: `web.post` -> Route `UniversalProxy` -> Path `v1/projects/{app}/events/latest` (filter by file `telemetry.qo`).
    - [ ] Display result on screen.

### Phase 5: Verification
- [ ] **Connectivity Test:** Server retrieves list of clients (Success/Fail).
- [ ] **Config Propogation:** Changed `_poll_rate` in Notehub -> Clients update within 1 sync cycle.
- [ ] **Latency Test:** Server sends Relay Signal -> Client actuates relay (< 5 seconds).
