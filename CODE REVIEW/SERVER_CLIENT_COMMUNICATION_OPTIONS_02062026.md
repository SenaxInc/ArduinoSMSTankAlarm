# Server-Client Communication Architecture Options

**Date:** February 6, 2026  
**Context:** Evaluating alternatives to eliminate or minimize Notehub Routes for inter-device communication  
**Status:** Decision Pending — Document for Future Reference

---

## Table of Contents

1. [Background & Current Architecture](#1-background--current-architecture)
2. [The Fundamental Problem](#2-the-fundamental-problem)
3. [Option A: Arduino Opta Server + 2 Traditional Routes](#3-option-a-arduino-opta-server--2-traditional-routes)
4. [Option B: Arduino Opta Server + 1 Proxy Route (web.post/web.get)](#4-option-b-arduino-opta-server--1-proxy-route-webpostwebget)
5. [Option C: Arduino Opta Server + ArduinoBearSSL (Zero Routes)](#5-option-c-arduino-opta-server--arduinobearssl-zero-routes)
6. [Option D: Raspberry Pi Server (Zero Routes, Full Rewrite)](#6-option-d-raspberry-pi-server-zero-routes-full-rewrite)
7. [Option E: Hybrid — Arduino Opta + Raspberry Pi Bridge](#7-option-e-hybrid--arduino-opta--raspberry-pi-bridge)
8. [Comparison Matrix](#8-comparison-matrix)
9. [Notefile Inventory (Current State)](#9-notefile-inventory-current-state)
10. [Notehub API Reference Notes](#10-notehub-api-reference-notes)
11. [Notecard web.* API Reference Notes](#11-notecard-web-api-reference-notes)
12. [Data Usage & Rate Limit Analysis](#12-data-usage--rate-limit-analysis)
13. [Recommendations](#13-recommendations)

---

## 1. Background & Current Architecture

### 1.1 Hardware

The TankAlarm system consists of three types of Arduino Opta devices, all communicating via Blues Wireless Notecards through the Blues Notehub cloud service:

- **Client Optas** (field-deployed, 1 to many): Monitor tank levels via 4-20mA sensors, send telemetry to server, receive configuration and commands
- **Server Opta** (central, 1): Aggregates telemetry, serves web dashboard via Ethernet, dispatches SMS/email alerts, manages client configurations
- **Viewer Opta** (optional, 1 to many): Read-only dashboard displaying server summaries

### 1.2 Current Communication Flow

All inter-device communication currently works through Blues Notecard `.qo` (outbound) and `.qi` (inbound) notefiles. The Blues architecture requires that **something** bridges the gap between one device's `.qo` events and another device's `.qi` inbox. Currently, this bridging is expected to be done by **Notehub Routes**.

```
CLIENT OPTA                    NOTEHUB                         SERVER OPTA
──────────────                ─────────                       ───────────────
note.add("telemetry.qo") ──► Client's telemetry.qo      
                              events appear in Notehub   
                                        │
                              Route forwards events ──────► Server's .qi inbox
                                                            note.get("telemetry.qi")
```

### 1.3 Current Notefile Usage

#### Client → Server (Client sends, Server receives)
| Client Sends (.qo) | Server Reads (.qi) | Purpose |
|---|---|---|
| `telemetry.qo` | `telemetry.qi` | Periodic tank level readings |
| `alarm.qo` | `alarm.qi` | High/low alarm events |
| `daily.qo` | `daily.qi` | Daily summary reports |
| `unload.qo` | `unload.qi` | Tank unload detection events |
| `serial_log.qo` | `serial_log.qi` | Client serial log dumps (on request) |
| `serial_ack.qo` | `serial_ack.qi` | Acknowledgment that client received serial log request |
| `location_response.qo` | `location_response.qi` | GPS coordinates (on request) |

#### Server → Client (Server sends, Client receives)
| Server Sends (.qo) | Client Reads (.qi) | Purpose |
|---|---|---|
| `config_dispatch.qo` (with `_target`) | `config.qi` | Push configuration updates to specific client |
| `location_request.qo` (with `_target`) | `location_request.qi` | Request GPS location from specific client |
| `serial_request.qo` (with `_target`) | `serial_request.qi` | Request serial logs from specific client |
| `relay_command.qo` (with `_target`) | `relay.qi` | Remote relay on/off commands |

#### Server → External Services (NOT inter-device, stay as Notecard)
| Server Sends | Purpose | Requires Route? |
|---|---|---|
| `sms.qo` | SMS alerts via Twilio | Yes — Notehub Route to Twilio API |
| `email.qo` | Daily email summary | Yes — Notehub Route to SMTP/email service |

#### Server → Viewer
| Server Sends (.qo) | Viewer Reads (.qi) | Purpose |
|---|---|---|
| `viewer_summary.qo` | `viewer_summary.qi` | Periodic tank summary for display |

### 1.4 Key Technical Constraints Discovered

1. **The Arduino Opta's Ethernet does NOT support HTTPS.**  
   The server firmware explicitly uses HTTP port 80 for NWS API calls with the comment: `#define NWS_API_PORT 80  // Use HTTP for Arduino compatibility (HTTPS not easily supported)`. The `PortentaEthernet` library does not include a built-in SSL/TLS client.

2. **The Notehub REST API (`api.notefile.net`) requires HTTPS (port 443).**  
   There is no plain HTTP endpoint. All API calls require `Authorization: Bearer <token>` headers over TLS.

3. **Notecard `web.post`/`web.get` commands require a Proxy Route in Notehub.**  
   The `route` parameter is **required** (not optional) for all `web.*` requests. The Notecard cannot make arbitrary HTTP requests without a configured Proxy Route.

4. **Each Blues Notecard device has isolated notefiles.**  
   Client A's `telemetry.qo` and Client B's `telemetry.qo` are completely separate in Notehub. The server's Notecard can only access its own `.qi` files. There is no peer-to-peer Notecard communication.

5. **SMS and Email currently go through Notecard + Notehub Routes.**  
   `sms.qo` and `email.qo` notes are sent via the server's Notecard to Notehub, which forwards them via Routes to Twilio (SMS) and SMTP (email). These Routes would remain needed even if inter-device Routes are eliminated.

---

## 2. The Fundamental Problem

To move data between two Blues Notecard devices, **one of these must happen**:

1. **A Notehub Route** watches one device's `.qo` events and pushes them to another device's `.qi` inbox
2. **The Notehub REST API** is called (from somewhere with HTTPS access) to push notes to a device's `.qi` inbox or read events from devices
3. **Notehub Environment Variables** are used for shared state (limited, not message-based)

The question is: **where does the HTTPS call originate?**

| Source | Can do HTTPS? | Notes |
|---|---|---|
| Arduino Opta Ethernet | **NO** (natively) | Only plain HTTP; no SSL/TLS library included |
| Arduino Opta Ethernet + ArduinoBearSSL | **YES** (with library) | Adds ~30KB RAM, needs CA certs, time source |
| Notecard `web.*` via cellular | **YES** (with 1 Proxy Route) | 8KB max response, blocking, cellular data cost |
| Raspberry Pi / Computer | **YES** (natively) | Full HTTPS via Python/Node.js, no limitations |
| Cloud function / external script | **YES** | Runs outside the embedded system entirely |

---

## 3. Option A: Arduino Opta Server + 2 Traditional Routes

### Description
Keep the existing Arduino Opta server hardware and firmware. Configure **two Notehub Routes** to handle all inter-device communication:

1. **Client → Server Route**: Watches ALL `.qo` events from client devices and delivers them to the server device's `.qi` inbox
2. **Server → Client Route**: Watches server's `.qo` events (config_dispatch, location_request, serial_request, relay_command), reads the `_target` field from the note body, and delivers to the specified client device's `.qi` inbox

### How It Works

```
CLIENT → SERVER:
  Client note.add("telemetry.qo") ──► Notehub ──► Route 1 ──► Server's telemetry.qi
  Client note.add("alarm.qo")     ──► Notehub ──► Route 1 ──► Server's alarm.qi
  (etc. for all 7 client outbound notefiles)

SERVER → CLIENT:
  Server note.add("config_dispatch.qo", body: {_target: "dev:xxx"}) ──► Notehub ──► Route 2
    Route 2 reads _target, POSTs to Notehub API: /devices/dev:xxx/notes/config.qi
    ──► Client's config.qi

SERVER → VIEWER:
  Server note.add("viewer_summary.qo") ──► Notehub ──► Route 1 or separate ──► Viewer's viewer_summary.qi

SERVER → EXTERNAL:
  Server note.add("sms.qo")   ──► Notehub ──► SMS Route (Twilio) ──► Phone
  Server note.add("email.qo") ──► Notehub ──► Email Route (SMTP) ──► Inbox
```

### Notehub Route Configuration

**Route 1: Client-to-Server Data Delivery**
- Type: General HTTP Route or Notehub Data Transform
- Trigger: Events from client fleet devices (`.qo` notefiles: telemetry, alarm, daily, unload, serial_log, serial_ack, location_response)
- Action: Deliver as `.qi` note to the server device
- Could use JSONata to preserve notefile names (e.g., client's `telemetry.qo` → server's `telemetry.qi`)

**Route 2: Server-to-Client Command Delivery**
- Type: HTTP Route targeting Notehub's own API
- Trigger: Events from server device (`.qo` notefiles: config_dispatch, location_request, serial_request, relay_command)
- Action: Read `_target` field from event body, POST to `https://api.notefile.net/v1/projects/{pid}/devices/{_target}/notes/{notefile}.qi`
- Requires the Route to have a Notehub API Bearer token in its HTTP headers
- JSONata or JavaScript transform to extract `_target` and build the correct URL

### Firmware Changes Required

**Server:** Minimal — current code already uses `note.add` for outbound and `note.get` for inbound. The `_target` field in server→client notes is already implemented. No code changes needed.

**Client:** None — already sends `.qo` and reads `.qi` correctly.

**Viewer:** None — already reads `viewer_summary.qi`.

### Pros
- **Simplest firmware**: No code changes needed (current code works as-is)
- **Most efficient data usage**: Events piggyback on normal Notecard sync, zero extra cellular data for polling
- **No Notecard blocking**: `note.add` and `note.get` are fast local operations
- **No API rate limit concerns**: Routes don't count against API rate limits
- **Well-tested pattern**: Standard Blues architecture for device-to-device communication
- **Reliable**: Routes are managed by Notehub cloud infrastructure with retry logic

### Cons
- **2 Routes** must be configured and maintained in Notehub
- **Route 2 complexity**: The Server→Client route needs JSONata/JS to read `_target` and dynamically target different devices
- **Route debugging**: If routes malfunction, debugging is done in the Notehub UI (less visibility from firmware side)
- **SMS/Email still need routes**: 2 additional routes for Twilio SMS and SMTP email (total 4 Routes)

### Estimated Effort
- **Firmware changes**: 0 hours (current code works)
- **Notehub configuration**: 2-4 hours (set up and test routes)
- **Total**: 2-4 hours

---

## 4. Option B: Arduino Opta Server + 1 Proxy Route (web.post/web.get)

### Description
Keep the existing Arduino Opta server hardware. Configure **ONE Proxy Route** in Notehub that points to `https://api.notefile.net`. The server firmware uses the Notecard's `web.post` and `web.get` commands (which go through the Notecard's cellular connection → Notehub → Proxy Route → Notehub API) to make all Notehub REST API calls.

### How It Works

```
SERVER → CLIENT (via web.post):
  Server sends Notecard command:
    {"req":"web.post","route":"notehub-api",
     "name":"/v1/projects/{pid}/devices/{clientUID}/notes/config.qi",
     "body":{...config data...}}
  
  Flow: Arduino ─I2C─► Notecard ─cellular─► Notehub ─Proxy Route─► api.notefile.net
        ──► Target client's config.qi ──► Client reads via note.get

CLIENT → SERVER (via web.get polling):
  Server sends Notecard command:
    {"req":"web.get","route":"notehub-api",
     "name":"/v1/projects/{pid}/events?since={cursor}&files=telemetry.qo,alarm.qo,..."}
  
  Flow: Arduino ─I2C─► Notecard ─cellular─► Notehub ─Proxy Route─► api.notefile.net
        ──► Returns JSON array of recent client events ──► Server processes them

SERVER → VIEWER (via web.post):
  Same as Server→Client, targeting the Viewer's DeviceUID

SERVER → EXTERNAL:
  sms.qo and email.qo still go through normal note.add + dedicated SMS/Email Routes
```

### Notehub Route Configuration

**Single Proxy Route: "notehub-api"**
- Type: Proxy  
- URL: `https://api.notefile.net`
- HTTP Headers: `Authorization: Bearer <Personal_Access_Token>`
- This single route enables ALL Notehub API calls from the server firmware

### Firmware Changes Required

**Server (significant):**

1. **Add `notehubProjectUid` to ServerConfig** (or reuse `productUid`)
   - The Events API needs the project/product UID in the URL path

2. **Add helper function `notehubApiPost()`**
   ```cpp
   // Push a .qi note to a specific device via Notehub API (through Proxy Route)
   static bool notehubApiPost(const char *deviceUid, const char *notefile, J *bodyJson) {
     char namePath[256];
     snprintf(namePath, sizeof(namePath),
       "/v1/projects/%s/devices/%s/notes/%s",
       gConfig.productUid, deviceUid, notefile);
     
     J *req = notecard.newRequest("web.post");
     JAddStringToObject(req, "route", "notehub-api");
     JAddStringToObject(req, "name", namePath);
     JAddItemToObject(req, "body", bodyJson);
     
     J *rsp = notecard.requestAndResponse(req);
     // Check rsp["result"] for HTTP status code
     int httpStatus = JGetInt(rsp, "result");
     notecard.deleteResponse(rsp);
     return (httpStatus >= 200 && httpStatus < 300);
   }
   ```

3. **Rewrite 5 server→client send functions** to use `notehubApiPost()` instead of `note.add`:
   - `sendConfigViaNotecard()` → POST to `config.qi` on target client
   - `sendLocationRequest()` → POST to `location_request.qi` on target client
   - `requestClientSerialLogs()` → POST to `serial_request.qi` on target client
   - `sendRelayCommand()` → POST to `relay.qi` on target client
   - `sendRelayClearCommand()` → POST to `relay.qi` on target client
   - The `_target` field in the body becomes unnecessary (DeviceUID is in the URL path)

4. **Rewrite `publishViewerSummary()`** to use `notehubApiPost()` targeting the Viewer's DeviceUID

5. **Add helper function `notehubApiPollEvents()`**
   ```cpp
   // Poll Notehub Events API for recent client events (through Proxy Route)
   static bool notehubApiPollEvents(const char *sinceCursor) {
     char namePath[256];
     snprintf(namePath, sizeof(namePath),
       "/v1/projects/%s/events?since=%s&sortOrder=asc&pageSize=25",
       gConfig.productUid, sinceCursor);
     
     J *req = notecard.newRequest("web.get");
     JAddStringToObject(req, "route", "notehub-api");
     JAddStringToObject(req, "name", namePath);
     
     J *rsp = notecard.requestAndResponse(req);
     // Parse rsp["body"] which contains the Events API JSON response
     // Process events and update cursor
     // ...
   }
   ```

6. **Rewrite `pollNotecard()`** to use `notehubApiPollEvents()` instead of `note.get` on local `.qi` files:
   - Must track a `since` cursor (event timestamp/ID) to avoid re-processing
   - Must parse the Events API response format (array of event objects with `file`, `body`, `device`, `when` fields)
   - Must dispatch events to the correct handler based on `file` field (telemetry, alarm, daily, etc.)
   - Must handle pagination (response may contain `has_more: true`)
   - Must persist cursor to LittleFS to survive reboots

7. **Add Viewer DeviceUID to server configuration** (new field or discoverable)

8. **Add rate limiting logic** to stay within Notehub API limits (7 requests/minute free tier)

**Client:** None — no changes needed.

**Viewer:** None — no changes needed.

### Pros
- **Only 1 Proxy Route** in Notehub (plus SMS/Email routes = 3 total)
- **All routing logic in firmware**: Easier to understand and debug from the Arduino side
- **Bearer token stored in Notehub Route config**: More secure than embedding on device
- **Works without Ethernet internet**: Uses cellular connection through Notecard

### Cons
- **Significant firmware rewrite**: 7+ functions need rewriting, new polling/cursor logic
- **Higher cellular data usage**: Polling adds ~1-4 MB/day extra (see Section 12)
- **8KB response limit**: `web.*` responses capped at 8192 bytes; may need multiple calls for large event batches
- **Notecard blocking**: Each `web.post`/`web.get` blocks the Notecard for the duration (up to 90s timeout). During this time, no other Notecard operations (sync, `note.add`, etc.) can occur
- **API rate limits**: 7 requests/minute on free tier; must carefully budget between polling and commands
- **Latency**: Each request goes Arduino → Notecard → cellular → Notehub → API → back. Could be 2-10 seconds per call
- **Polling overhead**: Unlike routes (which push data on sync), this approach requires continuous polling which may miss events or introduce delays
- **SMS/Email routes still needed**: These still require dedicated Notehub Routes (total 3)
- **Cellular connection required**: `web.*` commands need an active Notehub connection; if cellular is down, all inter-device communication stops (with Routes, queued notes sync whenever connectivity resumes)

### Estimated Effort
- **Firmware changes**: 16-24 hours (rewrite sends, implement polling, cursor persistence, rate limiting)
- **Notehub configuration**: 30 minutes (1 Proxy Route)
- **Testing**: 8-12 hours (end-to-end testing of all communication paths)
- **Total**: 24-36 hours

---

## 5. Option C: Arduino Opta Server + ArduinoBearSSL (Zero Routes for Inter-Device)

### Description
Keep the existing Arduino Opta server hardware. Add the **ArduinoBearSSL** library to enable HTTPS/TLS connections from the Ethernet interface. The server makes direct HTTPS calls to `api.notefile.net` from Ethernet — zero Routes needed for inter-device communication.

### How It Works

```
SERVER → CLIENT (via Ethernet HTTPS):
  Arduino Opta ─Ethernet─► Internet ─HTTPS─► api.notefile.net
    POST /v1/projects/{pid}/devices/{clientUID}/notes/config.qi
    Authorization: Bearer <PAT>
    Body: {...config data...}
  ──► Target client's config.qi ──► Client reads via note.get

CLIENT → SERVER (via Ethernet HTTPS polling):
  Arduino Opta ─Ethernet─► Internet ─HTTPS─► api.notefile.net
    GET /v1/projects/{pid}/events?since={cursor}
    Authorization: Bearer <PAT>
  ──► Returns JSON array of recent client events ──► Server processes them

SERVER → EXTERNAL:
  SMS: Direct Twilio HTTPS API call from Ethernet (if Twilio supports needed auth)
  Email: Direct SMTP from Ethernet (using EthernetClient, port 25/587)
  OR: Continue using sms.qo/email.qo + Notehub Routes
```

### Dependencies

- **ArduinoBearSSL library** (official Arduino library, compatible with Arduino Opta/Portenta H7)
  - Wraps any Arduino `Client` (including `EthernetClient`) with BearSSL TLS
  - Usage: `BearSSLClient sslClient(ethernetClient);`
  
- **Root CA Certificate** for `api.notefile.net`
  - Must be embedded in firmware as a PEM string
  - Currently uses Let's Encrypt or DigiCert CA (need to verify and embed)
  - CA certificates expire and may need firmware updates when rotated
  
- **Time source** for certificate validation
  - ArduinoBearSSL requires `ArduinoBearSSL.onGetTime(callback)` 
  - Can use the Notecard's `card.time` API to get current epoch
  
- **Ethernet internet access**
  - The server's Ethernet port MUST have a route to the internet (not just LAN)
  - Requires DNS resolution for `api.notefile.net`
  - Current config: supports DHCP or static IP with gateway + DNS

### Firmware Changes Required

**Server (extensive):**

1. **Add ArduinoBearSSL include and setup**
   ```cpp
   #include <ArduinoBearSSL.h>
   
   // Root CA for api.notefile.net (PEM format)
   static const char ROOT_CA[] PROGMEM = R"CA(
   -----BEGIN CERTIFICATE-----
   ... DigiCert or Let's Encrypt root CA ...
   -----END CERTIFICATE-----
   )CA";
   
   // Time callback for BearSSL certificate validation
   unsigned long getTime() {
     // Get epoch from Notecard card.time
     J *req = notecard.newRequest("card.time");
     J *rsp = notecard.requestAndResponse(req);
     unsigned long t = (unsigned long)JGetNumber(rsp, "time");
     notecard.deleteResponse(rsp);
     return t;
   }
   
   void setup() {
     ArduinoBearSSL.onGetTime(getTime);
     // ...
   }
   ```

2. **Add `notehubToken` to ServerConfig**
   ```cpp
   struct ServerConfig {
     // ... existing fields ...
     char notehubToken[128];  // Personal Access Token for Notehub API
   };
   ```
   - Must be persisted to LittleFS with other config
   - Must be configurable via Server Settings web UI
   - Must be included in save/load config functions

3. **Implement HTTPS client helper**
   ```cpp
   static EthernetClient ethClient;
   static BearSSLClient sslClient(ethClient);
   
   // Make an HTTPS POST to Notehub API
   static bool notehubApiPost(const char *path, const char *jsonBody) {
     if (!sslClient.connect("api.notefile.net", 443)) {
       return false;
     }
     sslClient.print("POST ");
     sslClient.print(path);
     sslClient.println(" HTTP/1.1");
     sslClient.println("Host: api.notefile.net");
     sslClient.print("Authorization: Bearer ");
     sslClient.println(gConfig.notehubToken);
     sslClient.println("Content-Type: application/json");
     sslClient.print("Content-Length: ");
     sslClient.println(strlen(jsonBody));
     sslClient.println();
     sslClient.print(jsonBody);
     
     // Read response, parse status code...
     // Similar pattern to existing NWS API code
   }
   
   // Make an HTTPS GET to Notehub API
   static bool notehubApiGet(const char *path, char *responseBuffer, size_t bufSize) {
     // Similar HTTPS client pattern
   }
   ```

4. **Rewrite all 5 server→client send functions** (same as Option B but using Ethernet instead of Notecard `web.post`)

5. **Rewrite `pollNotecard()` for Events API polling** (same as Option B but using Ethernet HTTPS GET)

6. **Rewrite `publishViewerSummary()`** to use Ethernet HTTPS POST

7. **Optionally rewrite SMS/Email** to use direct API calls:
   - SMS: Twilio REST API (`https://api.twilio.com/2010-04-01/Accounts/{sid}/Messages.json`)
   - Email: Direct SMTP via `EthernetClient` on port 587 (STARTTLS) or port 25
   - This would eliminate ALL Notehub Routes

8. **Add rate limiting, cursor persistence, error handling** (same as Option B)

9. **Update Server Settings web UI** to include `notehubToken` field

### Pros
- **Zero Routes for inter-device communication** (truly route-free for device-to-device)
- **Potentially zero Routes total** if SMS/email are also converted to direct API calls
- **No Notecard blocking**: HTTPS calls happen on Ethernet, Notecard remains free for syncing
- **No 8KB response limit**: Ethernet can handle larger responses
- **Faster API calls**: Ethernet is typically faster than cellular for HTTPS requests
- **Bearer token on device**: Configurable via web UI, stored in LittleFS (could be a pro or con)

### Cons
- **Requires ArduinoBearSSL library**: Additional dependency, ~30KB RAM for TLS session state
- **Requires embedded CA certificate**: Must update firmware if CA cert rotates (Let's Encrypt certs rotate frequently)
- **Requires Ethernet internet access**: Server's Ethernet must route to the internet, not just LAN. This may not be the case in all deployment environments
- **Requires DNS**: Server must resolve `api.notefile.net` — needs properly configured DNS
- **TLS handshake overhead**: Each HTTPS connection requires a TLS handshake (~1-3 seconds on embedded hardware)
- **Memory pressure**: BearSSL adds ~30KB RAM usage for TLS state; the Opta has 1MB SRAM so this is manageable but not negligible
- **Bearer token stored on device**: If the device is physically compromised, the token is exposed. The token grants access to ALL devices in the Notehub project.
- **Certificate pinning challenges**: If Blues changes their TLS certificate provider, firmware updates are needed
- **Still needs firmware rewrite**: Same scope as Option B for the communication logic
- **Extensive firmware rewrite**: All send functions, polling, cursor management, rate limiting
- **Incompatible Ethernet usage**: The `BearSSLClient` shares the same `EthernetClient` pool with the web server. Need to be careful about concurrent connections (web server serving dashboard while also making HTTPS API calls)
- **Testing complexity**: Must test TLS handshake, certificate validation, connection timeouts, error recovery across various network conditions
- **API rate limits still apply**: 7 requests/minute on free tier

### Estimated Effort
- **Library integration**: 4-8 hours (ArduinoBearSSL setup, CA cert embedding, time callback, testing TLS handshake)
- **Firmware changes**: 20-30 hours (same as Option B plus HTTPS client implementation, SSL error handling)
- **Web UI changes**: 4-6 hours (add token to settings page)
- **Testing**: 12-16 hours (TLS testing across networks, connection recovery, concurrent usage)
- **Total**: 40-60 hours

---

## 6. Option D: Raspberry Pi Server (Zero Routes, Full Rewrite)

### Description
Replace the Arduino Opta server entirely with a **Raspberry Pi** (or similar single-board computer / any computer with internet). The Raspberry Pi runs a Python or Node.js application that communicates with all devices via the Notehub REST API over HTTPS. **Zero Routes needed.** The Raspberry Pi does NOT need a Blues Notecard — it uses standard internet connectivity.

### How It Works

```
CLIENT OPTAS (unchanged)          NOTEHUB                    RASPBERRY PI SERVER
──────────────────                ─────────                  ─────────────────────
note.add("telemetry.qo") ──────► Events stored            
                                                            polls GET /events
                                  ◄──────────────────────── Python requests.get()
                                                            processes telemetry

                                  POST /devices/{uid}/      
                                  notes/config.qi  ◄──────── Python requests.post()
note.get("config.qi") ◄────────── delivered to client       (push config to client)

                                                            Twilio API ──► SMS
                                                            SMTP ──► Email
                                                            Flask/Express ──► Web Dashboard

VIEWER OPTAS (unchanged)          NOTEHUB                    
──────────────────────            ─────────                  
                                  POST /devices/{uid}/      
                                  notes/viewer_summary.qi ◄─ Python requests.post()
note.get("viewer_summary.qi") ◄── delivered to viewer       (push summary to viewer)
```

### Architecture

```
┌─────────────────────────────────────────────────────┐
│ Raspberry Pi Server                                 │
│                                                     │
│ ┌─────────────────┐  ┌─────────────────────────┐   │
│ │ Web Dashboard   │  │ Notehub API Client       │   │
│ │ (Flask/Express) │  │ - Poll events            │   │
│ │ Port 80         │  │ - Push .qi notes          │   │
│ └────────┬────────┘  │ - Device management       │   │
│          │           └──────────┬──────────────┘   │
│          │                      │                   │
│ ┌────────┴────────┐  ┌─────────┴───────────────┐   │
│ │ SMS Service     │  │ Email Service            │   │
│ │ (Twilio API)    │  │ (SMTP / SendGrid)        │   │
│ └─────────────────┘  └─────────────────────────┘   │
│                                                     │
│ ┌─────────────────────────────────────────────┐     │
│ │ Data Storage                                │     │
│ │ SQLite / PostgreSQL / JSON files            │     │
│ └─────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────┘
          │
          │ HTTPS to api.notefile.net
          │ HTTPS to api.twilio.com
          │ SMTP to email server
          ▼
      Internet
```

### Technology Stack Options

**Python Stack (Recommended):**
- `Flask` or `FastAPI` for web dashboard
- `requests` library for Notehub API calls (HTTPS natively supported)
- `twilio` library for SMS
- `smtplib` (built-in) for email
- `sqlite3` (built-in) or `SQLAlchemy` for data storage
- `APScheduler` or `celery` for periodic polling
- Runs on any Linux system (Raspberry Pi, Docker container, cloud VM)

**Node.js Stack:**
- `Express` for web dashboard
- `axios` or `node-fetch` for Notehub API calls
- `twilio` npm package for SMS
- `nodemailer` for email
- `better-sqlite3` or `sequelize` for data storage
- Runs on any system with Node.js

### What Gets Rewritten

The entire 9,576-line Arduino server `.ino` file must be **reimplemented** in Python/Node.js:

| Current Arduino Feature | Python/Node.js Equivalent |
|---|---|
| EthernetServer web dashboard (9 HTML pages, REST API) | Flask/Express routes + templates |
| Notecard `note.get` / `note.add` | `requests.get/post` to Notehub API |
| `pollNotecard()` → process telemetry/alarm/daily/unload/serial/location | Background polling loop + event dispatching |
| `sendSmsAlert()` via `sms.qo` | `twilio.rest.Client.messages.create()` |
| `sendDailyEmail()` via `email.qo` | `smtplib.SMTP_SSL.sendmail()` |
| ServerConfig struct + LittleFS persistence | JSON/YAML config file or SQLite database |
| TankRecord array in RAM | SQLite table or in-memory dict |
| Contact management, calibration, serial logs | Equivalent data models + API routes |
| NWS weather API (HTTP) | `requests.get("https://api.weather.gov/...")` (HTTPS works natively) |

### What Stays the Same (Zero Changes)

- **Client Opta firmware**: Unchanged — continues using Notecard `note.add(.qo)` and `note.get(.qi)`
- **Viewer Opta firmware**: Unchanged — continues reading `viewer_summary.qi`
- **Blues Notecard hardware on clients/viewers**: Unchanged
- **Notehub project configuration**: Devices still connect to Notehub normally

### Pros
- **ZERO Notehub Routes needed** — not even 1
- **Direct Twilio SMS**: No Notehub Route for SMS
- **Direct SMTP Email**: No Notehub Route for email
- **Full HTTPS support**: Python/Node.js handle TLS natively with zero configuration
- **No 8KB limits**: Can process arbitrarily large API responses
- **Better data storage**: SQLite/PostgreSQL instead of RAM-only arrays
- **Better web dashboard**: Full web framework (Flask/Express) with templates, WebSockets, etc.
- **Easier debugging**: Full logging, debugger support, SSH access
- **More capable NWS integration**: HTTPS works natively (current Arduino code uses HTTP which may not work since NWS redirects to HTTPS)
- **Historical data**: Database enables long-term historical analysis
- **Automatic updates**: `apt-get upgrade` instead of OTA firmware updates
- **Multiple deployment options**: Raspberry Pi, Docker container, cloud VM (AWS/DigitalOcean), any Linux server

### Cons
- **COMPLETE SERVER REWRITE**: 9,576 lines of Arduino C++ → Python/Node.js. Major effort.
- **Different hardware**: Need a Raspberry Pi (or equivalent) — additional cost ($35-75 for Pi 4/5)
- **Different deployment model**: Pi needs power, network, enclosure; different from DIN-rail Opta
- **Higher power consumption**: Pi draws 3-5W vs Opta's <2W
- **No cellular backup**: Pi depends on Ethernet/WiFi for internet; if network goes down, all communication stops (Opta server has cellular backup via Notecard)
- **No Blues Notecard benefits**: The server loses DFU over Notecard, cellular fallback, Blues Notecard ecosystem integration
- **OS maintenance**: Raspbian needs updates, SD cards can fail, more moving parts
- **Learning curve**: If the developer is primarily Arduino-focused, the Pi paradigm may require adaptation
- **Web dashboard reimplementation**: All 9 HTML pages, JavaScript, CSS, REST API endpoints must be recreated (or ported)
- **Ongoing maintenance split**: Server is now a different technology stack from clients/viewer

### Estimated Effort
- **Core server logic** (polling, event processing, config management): 40-60 hours
- **Web dashboard** (9 pages + REST API + WebSocket): 60-80 hours
- **SMS/Email integration**: 4-8 hours
- **NWS weather integration**: 2-4 hours
- **Deployment setup** (Pi configuration, systemd services, auto-start): 4-8 hours
- **Testing**: 20-30 hours
- **Total**: 130-190 hours (3-5 weeks full-time)

---

## 7. Option E: Hybrid — Arduino Opta + Raspberry Pi Bridge

### Description
Keep the Arduino Opta server for the web dashboard and local processing. Add a **Raspberry Pi** (or any computer) as a "bridge" that handles ONLY the Notehub API communication. The Pi polls the Notehub Events API and pushes events to the Opta server via its local Ethernet API. The Pi also receives commands from the Opta server's API and forwards them to clients via the Notehub API.

### How It Works

```
CLIENT OPTAS                  NOTEHUB                 RASPBERRY PI             ARDUINO OPTA
──────────                    ─────────               ──────────────           ─────────────
note.add(.qo) ──────────────► Events stored           
                                                      polls GET /events ◄────── 
                                                      receives events
                              ◄──────────────────────── POST /events to Opta    
                                                                    ──────────► process & display

                                                      Opta API: "send config" ──────►
                              POST /devices/{uid}/    ◄────── POST to Notehub API
                              notes/config.qi
note.get("config.qi") ◄──────── delivered to client
```

### Pros
- **Zero Routes for inter-device** (Pi handles all API calls)
- **Minimal Opta firmware changes**: Just add REST API endpoints for the Pi to push/pull data
- **Keeps existing web dashboard**: All 9,576 lines of server code stay mostly intact
- **Separation of concerns**: Opta handles display/alerting, Pi handles Notehub API
- **SMS/Email**: Pi can handle Twilio + SMTP directly (zero routes for those too)

### Cons
- **Two pieces of server hardware**: Opta + Pi = more complexity, more failure points
- **Network dependency**: Pi and Opta must be on the same LAN
- **Still needs firmware changes**: Opta needs new API endpoints for the Pi bridge
- **Pi software development**: Need to write the bridge application (~500-1000 lines Python)
- **Deployment complexity**: Two devices to maintain, power, configure, monitor

### Estimated Effort
- **Pi bridge application**: 16-24 hours (Python polling + Notehub API + Opta API calls)
- **Opta firmware changes**: 8-16 hours (add REST API endpoints for receiving events and sending commands from the Pi)
- **Testing**: 8-12 hours
- **Total**: 32-52 hours

---

## 8. Comparison Matrix

| Criteria | A: 2 Routes | B: 1 Proxy | C: BearSSL | D: Pi Rewrite | E: Hybrid |
|---|---|---|---|---|---|
| **Notehub Routes** | 4 total (2 device + 2 SMS/email) | 3 total (1 proxy + 2 SMS/email) | 0-2 (just SMS/email, or 0 if direct API) | 0 | 0 |
| **Firmware Changes** | None | Major | Extensive | Complete rewrite | Moderate |
| **New Libraries** | None | None | ArduinoBearSSL | N/A (Python) | None on Opta |
| **New Hardware** | None | None | None | Raspberry Pi | Raspberry Pi |
| **Cellular Data Impact** | None (events sync normally) | +1-4 MB/day (polling) | None (uses Ethernet) | N/A | None |
| **Ethernet Internet Required** | No | No | **YES** | N/A (Pi has internet) | Yes (Pi needs internet) |
| **8KB Response Limit** | N/A | Yes (web.* limit) | No | No | No |
| **Notecard Blocking** | No | Yes (during web.*) | No | N/A | No |
| **API Rate Limits** | N/A | 7 req/min shared | 7 req/min | 7 req/min | 7 req/min |
| **SMS Route Needed** | Yes | Yes | Optional | No | No |
| **Email Route Needed** | Yes | Yes | Optional | No | No |
| **Client Changes** | None | None | None | None | None |
| **Viewer Changes** | None | None | None | None | None |
| **Estimated Effort** | 2-4 hrs | 24-36 hrs | 40-60 hrs | 130-190 hrs | 32-52 hrs |
| **Reliability** | Highest | Good | Good | Good | Good (2 devices) |
| **Maintainability** | Easiest | Moderate | Hard (TLS certs) | Different stack | Moderate (2 systems) |
| **Data Usage Cost** | Lowest | Highest | Low | N/A | Low |

---

## 9. Notefile Inventory (Current State)

### Complete list of all notefiles across all devices:

#### Client Device Notefiles
```
OUTBOUND (Client sends via note.add):
  telemetry.qo          - Periodic tank level readings
  alarm.qo              - High/low alarm events
  daily.qo              - Daily summary reports
  unload.qo             - Tank unload detection events
  serial_log.qo         - Client serial log dumps
  serial_ack.qo         - Acknowledgment of serial log request
  location_response.qo  - GPS coordinates response
  config.qo             - Client config acknowledgment
  relay_command.qo       - Client-to-client relay trigger (with _target)

INBOUND (Client reads via note.get):
  config.qi             - Configuration updates from server
  relay.qi              - Relay control commands from server
  serial_request.qi     - Request to send serial logs
  location_request.qi   - Request to send GPS location
```

#### Server Device Notefiles
```
OUTBOUND (Server sends via note.add):
  config_dispatch.qo    - Config updates for clients (with _target)
  location_request.qo   - GPS location requests (with _target)
  serial_request.qo     - Serial log requests (with _target)
  relay_command.qo       - Relay commands for clients (with _target)
  viewer_summary.qo     - Tank summary for viewer devices
  sms.qo                - SMS alerts (→ Twilio via Route)
  email.qo              - Daily email reports (→ SMTP via Route)

INBOUND (Server reads via note.get):
  telemetry.qi          - Tank level readings from clients
  alarm.qi              - Alarm events from clients
  daily.qi              - Daily reports from clients
  unload.qi             - Unload events from clients
  serial_log.qi         - Serial logs from clients
  serial_ack.qi         - Serial log acknowledgments
  location_response.qi  - GPS responses from clients
```

#### Viewer Device Notefiles
```
INBOUND (Viewer reads via note.get):
  viewer_summary.qi     - Tank summary from server
```

### Current Routing Metadata

Server→Client functions currently include a `_target` field in the note body containing the client's DeviceUID. This was added to support Route-based delivery (the Route reads `_target` and delivers to that device). In Options B/C/D/E, this field becomes unnecessary since the target DeviceUID goes directly in the API URL path.

---

## 10. Notehub API Reference Notes

### Key Endpoints

**Push QI Note to Device:**
```
POST https://api.notefile.net/v1/projects/{projectUID}/devices/{deviceUID}/notes/{notefileID}
Authorization: Bearer <Personal_Access_Token>
Content-Type: application/json

{"body": {"key": "value", ...}}
```
- `notefileID` must end in `.qi` or `.qis`
- `projectUID` = the Product UID (e.g., `com.company.project:product`)
- `deviceUID` = Blues DeviceUID (e.g., `dev:1234567890abcdef`)

**Get Events (read client telemetry):**
```
GET https://api.notefile.net/v1/projects/{projectUID}/events
Authorization: Bearer <Personal_Access_Token>
```
Query parameters:
- `since` — cursor for pagination (returns events after this point)
- `sortOrder` — `asc` or `desc`
- `pageSize` — number of events per page (default 50)
- `files` — filter by notefile name (comma-separated)
- `deviceUID` — filter by specific device

Response format:
```json
{
  "events": [
    {
      "uid": "event-uid",
      "device_uid": "dev:xxx",
      "file": "telemetry.qo",
      "body": {"c": "dev:xxx", "k": 1, "l": 48.5, ...},
      "when": 1706745600,
      "best_id": "..."
    }
  ],
  "through": "cursor-string",
  "has_more": false
}
```

**Get Device Latest Events:**
```
GET https://api.notefile.net/v1/projects/{projectUID}/devices/{deviceUID}/latest
Authorization: Bearer <Personal_Access_Token>
```

### Authentication

**Personal Access Token (PAT):**
- Created in Notehub UI → Account Settings → Access Tokens
- Long-lived (no expiration unless revoked)
- Grants access to ALL projects/devices the user owns
- Used as: `Authorization: Bearer <token>`
- ⚠️ If compromised, attacker has full API access to all devices

**OAuth Client Credentials (Alternative):**
- Created per-project in Notehub
- Token expires every 30 minutes (must refresh)
- Scoped to a specific project
- More secure for production, but requires token refresh logic

### Rate Limits
- **Free tier**: 7 API requests per minute, 10,200 per day per billing account
- **Paid tiers**: Higher limits available
- Rate limit applies to ALL API calls (polling + commands + management)
- Budget carefully: if polling every 30s = 2 req/min, leaving 5 req/min for commands

---

## 11. Notecard web.* API Reference Notes

### web.post
```json
{
  "req": "web.post",
  "route": "notehub-api",
  "name": "/v1/projects/{pid}/devices/{uid}/notes/config.qi",
  "body": {"key": "value"}
}
```
- `route` (REQUIRED): Alias for a Proxy Route configured in Notehub
- `name`: URL path relative to the Proxy Route's base URL
- `body`: JSON body to send
- `seconds`: Timeout override (default 90s)
- `async`: If true, returns immediately without waiting for response
- **Response limit**: 8192 bytes max
- **Blocking**: The Notecard is blocked during the request

### web.get
```json
{
  "req": "web.get",
  "route": "notehub-api",
  "name": "/v1/projects/{pid}/events?since=cursor&pageSize=10"
}
```
- Same constraints as web.post
- Returns response in `body` field (JSON) or `payload` (base64 binary)
- **Response limit**: 8192 bytes max

### Proxy Route Configuration in Notehub
1. Go to Notehub → Project → Routes → Create Route
2. Type: "Proxy"
3. Route Alias: `notehub-api` (or any name)
4. URL: `https://api.notefile.net`
5. HTTP Headers: `Authorization: Bearer <PAT_TOKEN>`
6. This single route enables ALL Notehub API calls through the Notecard

---

## 12. Data Usage & Rate Limit Analysis

### Option A (2 Routes): Baseline
- **Extra cellular data for inter-device comm**: 0 bytes (events sync naturally)
- **API requests**: 0 (routes handle everything server-side)

### Option B (1 Proxy Route): Polling Overhead
- Each `web.get` poll: ~300B request + ~500-2000B response (depending on events)
- Each `web.post` command: ~300B request + ~100B response

| Poll Interval | Polls/Day | Data/Day (polling only) | Budget Used |
|---|---|---|---|
| Every 15s | 5,760 | ~5-10 MB | 4 req/min → leaves 3/min for commands |
| Every 30s | 2,880 | ~2.5-5 MB | 2 req/min → leaves 5/min for commands |
| Every 60s | 1,440 | ~1-2.5 MB | 1 req/min → leaves 6/min for commands |
| Every 5 min | 288 | ~200-500 KB | 0.2 req/min → leaves 6.8/min for commands |

**Recommendation for Option B**: Poll every 60 seconds. This uses ~1-2.5 MB/day and leaves 6 API requests/minute for commands.

### Option C (BearSSL over Ethernet): Same Polling but via Ethernet
- Ethernet data is typically unmetered (LAN)
- Only concern is Notehub API rate limits (same as Option B)
- Can poll more aggressively since there's no cellular data cost

### Option D/E (Raspberry Pi): Same API Rate Limits
- Internet bandwidth is typically unmetered
- Same 7 req/min API rate limit applies
- Can use WebSockets or long-polling in the future if Blues adds support

---

## 13. Recommendations

### For Immediate Deployment (Fastest Path)
**Option A: 2 Traditional Routes**
- Zero firmware changes
- 2-4 hours of Notehub configuration
- Most reliable and data-efficient
- SMS/Email routes are needed regardless (total 4)
- This is the standard Blues architecture for multi-device systems

### For Minimal Route Footprint (Arduino Opta)
**Option B: 1 Proxy Route**
- Reduces inter-device routes from 2 to 1 (total 3 with SMS/Email)
- Significant firmware work (24-36 hours)
- Consider carefully whether 1 fewer Route justifies 30+ hours of work

### For Zero Routes (Long-Term / Next Major Version)
**Option D: Raspberry Pi Server** or **Option E: Hybrid**
- Only practical way to achieve truly zero Routes
- Pi server eliminates ALL Routes (including SMS/Email)
- Major effort (130-190 hours for full rewrite, or 32-52 for hybrid)
- Better overall architecture (database, full HTTPS, standard web framework)
- Consider when planning v2.0 of the system

### Not Recommended
**Option C: ArduinoBearSSL** — The complexity of managing TLS certificates, library integration, and potential Ethernet/TLS conflicts on embedded hardware is not justified. If you need zero routes, the Raspberry Pi (Option D) is a cleaner solution with fewer edge cases.

---

## Appendix: Key Code References

### Server Notecard Functions That Would Change

| Function | File | Line | Current Behavior | Would Change In |
|---|---|---|---|---|
| `sendConfigViaNotecard()` | Server .ino | ~5945 | `note.add("config_dispatch.qo")` | Options B, C, D, E |
| `sendLocationRequest()` | Server .ino | ~1941 | `note.add("location_request.qo")` | Options B, C, D, E |
| `requestClientSerialLogs()` | Server .ino | ~1993 | `note.add("serial_request.qo")` | Options B, C, D, E |
| `sendRelayCommand()` | Server .ino | ~5714 | `note.add("relay_command.qo")` | Options B, C, D, E |
| `sendRelayClearCommand()` | Server .ino | ~5757 | `note.add("relay_command.qo")` | Options B, C, D, E |
| `publishViewerSummary()` | Server .ino | ~6902 | `note.add("viewer_summary.qo")` | Options B, C, D, E |
| `pollNotecard()` | Server .ino | ~6036 | `note.get` on 7 `.qi` files | Options B, C, D, E |
| `processNotefile()` | Server .ino | ~6046 | Generic `note.get` + handler | Options B, C, D, E |
| `sendSmsAlert()` | Server .ino | ~6730 | `note.add("sms.qo")` | Options C, D, E only |
| `sendDailyEmail()` | Server .ino | ~6802 | `note.add("email.qo")` | Options C, D, E only |

### Functions That Do NOT Change (Any Option)

| Function | Reason |
|---|---|
| All Client .ino functions | Clients always use Notecard `note.add` / `note.get` |
| All Viewer .ino functions | Viewer always reads via `note.get("viewer_summary.qi")` |
| Server web dashboard (HTML serving) | Dashboard serves via Ethernet regardless |
| Server config persistence | LittleFS load/save stays the same |
| Server `initializeNotecard()` | hub.set still needed for Notecard sync |

---

*Document generated during architecture review session, February 2026.*  
*To be revisited when making the final communication architecture decision.*
