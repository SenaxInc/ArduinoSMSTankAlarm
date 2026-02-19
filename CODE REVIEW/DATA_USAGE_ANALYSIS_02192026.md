# Data Usage & Cost Analysis: Standard vs. "Universal Proxy"

**Date:** February 19, 2026  
**Context:** Evaluating the operational costs (Consumption Credits & Data Bytes) of the proposed architectures.

---

## 1. Cost Model Basics

Blues Wireless charges based on **Consumption Credits (CCs)**.
- **1 CC** = 1 Ingress Event (Data entering Notehub).
- **Egess (Data leaving Notehub)** = Generally included (up to fair use limits).
- **Data Bytes** = Cellular data consumption. Important for battery life and avoiding overage on legacy plans, but less critical on standard "Pro" plans.

| Operation | Consumption Credits (CC) | TLS Overhead (Bytes) |
| :--- | :--- | :--- |
| **`hub.sync` (Batch of Notes)** | 1 CC (covers up to ~100 notes in one sync) | **Low** (1 Handshake per batch) |
| **`web.post` / `web.get`** | 1 CC (per request) | **High** (1 Handshake per request) |
| **Signal (received)** | 0 CC (usually) | **Very Low** (Keep-alive packet) |

---

## 2. Scenario Analysis

### Scenario A: Standard Routing (Current Architecture)
*Clients push data to Notehub; Server syncs to receive routed notes.*

**Daily Volume (Per Client):**
- **Telemetry:** 15-minute intervals = 96 readings/day.
- **Batching:** Client syncs every 1 hour (4 readings/sync).
- **Server:** Syncs every 15 minutes to pick up data.

**Cost Calculation:**
1.  **Client Ingress:** 24 Syncs/day = **24 CCs/day**.
2.  **Notehub Processing:** Routing logic = **0 CCs**.
3.  **Server Egress:** 96 Syncs/day (to check for mail) = **0-96 CCs** (Empty syncs might be free or low cost depending on specific plan, but let's assume worst case of periodic syncing).
    *   *Optimization:* Server can use "Sync on Interrupt" (hub.set `mode:continuous` or `mode:periodic` with `inbound:15`) to only burn data when necessary.

**Total Estimate (10 Clients + 1 Server):**
- Clients: 10 * 24 = 240 CCs
- Server: ~96 CCs
- **Total:** ~336 CCs/day (~10,000 CCs/month)

---

### Scenario B: Universal Proxy (API Polling)
*Clients push data to Notehub; Server uses `web.get` to read API instead of syncing files.*

**Critical Flaw:** `web.*` commands are **not batched**. Every request is a full cellular transaction.

**Cost Calculation:**
1.  **Client Ingress:** Same as above (Client still sends data). **240 CCs/day**.
2.  **Server Polling:**
    -   If Server polls the API every 15 minutes for *each* client:
    -   10 Clients * 96 Polls = **960 requests**.
    -   **960 CCs/day** just for the Server.
    -   **Huge Data Overhead:** 960 TLS Handshakes * 5KB = **~4.8 MB/day** of unnecessary protocol overhead.

**Verdict:** Using "Universal Proxy" for *telemetry retrieval* is **prohibitively expensive**.

---

### Scenario C: Hybrid "Management Only" Proxy
*Use Standard Routing for high-frequency Telemetry. Use Universal Proxy for low-frequency Management.*

**Architecture:**
- **Telemetry:** Clients `note.add` -> Route -> Server `.qi` (Standard). Efficient batching.
- **Configuration (Poll Rates):** Server `web.post` -> Environment Variables (Rare).
- **Discovery (Client List):** Server `web.get` -> Fleet List (Once per day or on reboot).
- **Relay Control:** Server `web.post` (Signal) -> Client (On Demand).

**Cost Calculation (10 Clients + 1 Server):**
1.  **Telemetry (Standard):** ~336 CCs/day. (Same as Scenario A).
2.  **Discovery:** 1 `web.get` / day = **1 CC**.
3.  **Config Change:** 1 `web.post` / week = Negligible.
4.  **Relay Event:** 2 `web.post` (On/Off) per event.

**Total Estimate:**
- **Same operational cost as Scenario A**, but with the added features of Dynamic Discovery and Instant Control.
- **Added Cost:** Negligible (~10-20 CCs/month extra for management features).

---

## 3. Data Optimization Strategies

To minimize data usage in **any** scenario, apply these configuration changes:

1.  **Increase Batch Sizes / Decrease Sync Frequency**
    -   *Current:* Sync every 15 mins?
    -   *Proposed:* Sample every 15 mins, Sync every **4 hours**.
    -   *Impact:* Reduces Client Ingress from 96 syncs to 6 syncs per day.
    -   *Savings:* **93% reduction in CCs**.
    -   *Trade-off:* Server data is 4 hours old. (Is real-time tank level critical? If not, do this).

2.  **"On Change" Syncing**
    -   Only trigger a sync if tank level changes by >5%.
    -   Use `hub.set` with `sync: true` only for Alarms.
    -   Regular readings just queue up (`sync: false`).

3.  **Response Truncation for "Universal Proxy"**
    -   When querying the API (e.g., `web.get` for device list), use the `?page_size=100` and `?select=uid,last_seen` parameters to reduce the JSON response size.
    -   This saves cellular bytes (battery), though CC cost is the same.

---

## 4. Final Recommendation

**Adopt Scenario C (Hybrid).**

1.  **Do NOT replace standard routing for Telemetry.** The overhead of `web.get` API polling is too high for continuous data. Keep the `.qo` -> `.qi` Note file routing for bulk data.
2.  **Use Universal Proxy ONLY for:**
    -   **Discovery:** Fetching the list of active device UIDs on startup.
    -   **Global Config:** Setting Fleet Environment Variables.
    -   **Emergency Control:** Sending "Signals" (Relay On/Off) where latency matters more than cost.
3.  **Optimize Sync Schedule:**
    -   Set `outbound` sync to **60 minutes** or longer.
    -   Allow Alarms to trigger immediate syncs.
    -   This provides the best balance of cost vs. responsiveness.
