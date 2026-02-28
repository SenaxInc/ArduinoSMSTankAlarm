# 🔍 TankAlarm Comprehensive Code Review Report (02/28/2026)

Based on a rigorous analysis of the source code in the `TankAlarm-112025-Client-BluesOpta.ino`, `TankAlarm-112025-Server-BluesOpta.ino`, `TankAlarm-112025-Viewer-BluesOpta.ino`, and the core shared library files, here is a comprehensive technical code review.

## 1. Architecture and Design
**Overview:** The system effectively delegates responsibilities across a three-node distributed embedded system, communicating asynchronously via Blues Notecard I2C modems and Notehub.

**✅ Strong Points:**
*   **Decoupling via Notehub:** Isolating hardware telemetry (Client) from dashboard hosting (Server) and readonly kiosks (Viewer) is a highly scalable approach. The Server processes `.qi` queries and the Client manages `.qo` notes flawlessly.
*   **Atomic Config Persistence:** Implementing Mbed OS LittleFileSystem (LittleFS) to save JSON configurations is done defensively. The use of `.tmp` writes followed by an OS-level atomic `rename()` prevents data corruption during unexpected power loss.
*   **Resilient State Machines:** The Edge Client leverages discrete state machines (`PowerState` and `PulseSamplerState`) to gracefully degrade functionality (shutting down non-vital polling) when detecting compromised solar/battery states.
*   **Single Source of Truth:** `TankAlarm_Common.h` heavily standardizes file definitions (e.g. `TELEMETRY_OUTBOX_FILE`) eliminating desync risk.

**⚠️ Areas for Improvement:**
*   **Monolithic Looping:** The Server's `loop()` orchestration lacks the use of Mbed OS hardware timers or RTOS Threads. It forces the system to sequentially handle Web HTTP clients, parse FTP, query the I2C bus, and check alarms manually. An RTOS architecture would drastically improve real-time determinism.

## 2. Security Vulnerabilities
**✅ Strong Points:**
*   **Timing Attack Mitigation:** In `TankAlarm-112025-Server-BluesOpta.ino`, `pinMatches()` evaluates the 4-digit access pin via a constant-time bitwise comparator (`volatile uint8_t diff`). This is an impressive defense against side-channel timing analysis. 
*   **Rate Limiting:** IP/Global lockout states (`gAuthFailureCount`) successfully punish repeated, unauthorized endpoint calls limiting Brute-force efficacy.

**⚠️ Vulnerabilities:**
*   **Cleartext Authentication:** Portenta Ethernet implements HTTP strictly over Port 80. Sending the administrative PIN back and forth across raw Ethernet leaves the intranet open to basic packet-sniffing/MITM interception. 
*   **Brute-Force Bypass on Reboot:** Because the brute-force lockout timers are stored in RAM (`gNextAllowedAuthTime`), merely cycling power to the server zeroes out the counters, theoretically allowing an attacker with physical access to bypass the rate limitation.
*   **Stored XSS Potential:** Configurations pulled from APIs (e.g., Client Names, Tank Types) and injected into the dynamic UI lack rigorous runtime sanitization. An attacker changing raw JSON device tags could execute malicious JavaScript within the browsers of operators querying the server dashboard.

## 3. Performance & Memory Optimizations
**✅ Strong Points:**
*   **Heap Fragmentation Avoidance:** `readHttpRequest()` operates natively inside a 514-byte static character buffer (`static char lineBuf[514]`) handling massive header parsing routines. Similarly, String operations aggressively use `body.reserve(contentLength);`. This is expert-level `String` management on constrained microcontrollers.
*   **Dynamic Document Scoping:** Using automatic stack-cleaned pointers (`std::unique_ptr<JsonDocument> docPtr(new JsonDocument());`) for heavy ArduinoJson structures elegantly avoids raw-pointer memory leaks if the routing encounters an abort condition mid-parse.
*   **Excellent `PROGMEM` Implementation:** Moving monolithic block GUIs (32KB HTML files) directly into `PROGMEM` and parsing them via chunked chunks buffer streaming (`pgm_read_byte_near()`) successfully shifts strain away from the MCU’s SRAM block.

**⚠️ Areas for Improvement:**
*   Continuing to bundle uncompressed raw HTML inside the `.ino` compilation consumes extensive ROM. Consider storing `STYLE_CSS` and standard `.html` dashboard payloads inside the hardware's onboard Mbed partition block (LittleFS) and stream via `EthernetClient.write()`.

## 4. Potential Bugs and Edge Cases
*   **Watchdog (WDT) Starvation in Socket Polling:** Looking at `readHttpRequest()`, there exists a `while (client.connected() && millis() - start < 5000UL)` loop for TCP payload evaluation. This block is highly susceptible to locking execution up to 5 seconds. If the configured `IWatchdog` hardware expectation is shorter than 5 seconds, it will trigger an unwarranted hard reset. Periodic `mbedWatchdog.kick()` commands should be forced inside while-loops parsing large payloads.
*   **TCP Socket Exhaustion Risk:** Half-open or badly terminated TCP requests from clients may hold memory blocks in the `PortentaEthernet` indefinitely if the HTTP transaction lacks aggressive timeouts, causing subsequent UI fetch requests by legitimate users to "Spin" forever.
*   **I2C Blockage on Wire Interruption:** If an externally coupled Notecard physically disconnects, standard Arduino Wire library requests (`Wire.endTransmission()`) can block endlessly. The `I2C_Utility` handles recovery routines, but careful software timeout monitoring and forced hardware bus resets ought to be implemented standard on any polling.

## 5. Code Style & Maintainability
**✅ Strong Points:**
*   Naming standards (`gConfig` for globals) and function clarity are pristine. 
*   Variables and structs rely appropriately on standard C/C++ primitives rather than proprietary abstractions where possible.

**⚠️ Areas for Improvement:**
*   **Over-Encapsulated Project Structure:** The `TankAlarm-112025-Server-BluesOpta.ino` tops 12,200 lines. Having the REST endpoint handlers, HTML view representations, hardware initialization scripts, JSON deserialization, and business logic occupying a single sequential source file borders on an anti-pattern.
*   **Refactor Recommendations:** It is strongly recommended to extract logic into explicit components:
  1. `Controllers/HttpController.cpp`
  2. `Services/NotehubSyncService.cpp`
  3. `Views/DashboardPages.cpp` (Or simply reading off the Flash system natively).
