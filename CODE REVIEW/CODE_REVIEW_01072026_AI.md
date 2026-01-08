# Code Review Report

**Date:** January 7, 2026
**Reviewer:** GitHub Copilot
**Subject:** Tank Alarm System - Client & Server Implementation (Blues Opta)
**Version:** 1.0.0 (11/2025)

## 1. Executive Summary

The codebase for the Tank Alarm Client and Server (Arduino Opta + Blues Notecard) is mature, well-structured, and demonstrates good embedded software engineering practices. The architecture effectively leverages the Blues Notecard for robust connectivity and the Arduino Opta's processing power.

Key strengths include:
*   **Robust Connectivity**: Excellent use of Notecard's store-and-forward architecture.
*   **Modularity**: Clear separation between Client, Server, and Common logic.
*   **Reliability**: Implementation of Watchdog timers and file system persistence.
*   **Safety**: Explicit checks for string lengths and buffer sizes.

However, there are opportunities for improvement regarding security credential management, production deployment configuration (MAC addresses), and potentially blocking network operations.

## 2. Detailed Findings

### 2.1. Code Architecture & Organization
*   **Strengths**:
    *   The `TankAlarm_Common.h` file effectively shares constants and configurations, ensuring consistency between Client and Server.
    *   The use of standard JSON configuration files (`client_config.json`, `server_config.json`) allows for flexible remote updates and persistence.
    *   Hardware abstraction is decent, with specific checks for the Opta platform.

*   **Observations**:
    *   **POSIX Compatibility**: The effort to shim POSIX file I/O calls (`fopen`, `fread`) on top of MbedFS/LittleFS is interesting. While it improves portability, it adds a small abstraction layer. Ensure `TankAlarm_Platform.h` handles this consistenly across all future targets.

### 2.2. Reliability & Safety
*   **Buffer Safety**:
    *   The code makes extensive use of `strlcpy` and checks input lengths (e.g., `if (strlen(clientUid) >= 48)`). This is excellent practice for preventing buffer overflows.
    *   `snprintf` is correctly used for formatting strings.
*   **Watchdog Integration**:
    *   Both Client and Server initialize standard watchdogs (`IWatchdog` or `MbedWatchdog`). This is critical for remote unmanned devices.
    *   **Risk**: The `performFtpBackup` and `performFtpRestore` functions in the Server likely involve network operations. If these operations block for longer than `WATCHDOG_TIMEOUT_SECONDS` (30s), the device may reset during a backup.
        *   **Recommendation**: Ensure these long-running tasks explicitly "kick" the watchdog periodically or are run in sections.
*   **Memory Management**:
    *   The configuration loader limits file size to 8KB (`fileSize > 8192`) before `malloc`. This is a safe limit for the Opta's RAM.
    *   Usage of `std::unique_ptr` for `DynamicJsonDocument` ensures automatic memory cleanup, preventing leaks.

### 2.3. Security
*   **Hardcoded Credentials**:
    *   **Product UIDs**: `DEFAULT_PRODUCT_UID` is hardcoded. While not strictly secret, it ties the firmware to a specific Notecard project.
    *   **FTP Credentials**: `ftpPass` is stored in the `ServerConfig` struct and serialized to plain text JSON in the internal filesystem.
        *   **Recommendation**: If high security is required, consider encrypting sensitive fields in the JSON or omitting them from the file (requiring re-entry on setup), though for this application level, the current approach is likely an acceptable trade-off for usability.
*   **Network Identity**:
    *   **MAC Address**: The server code explicitly sets a static MAC address: `static byte gMacAddress[6] = { 0x02, 0x00, 0x01, 0x12, 0x20, 0x25 };`.
        *   **Critical**: If multiple Server units are deployed on the same Layer 2 network, this will cause IP conflicts.
        *   **Recommendation**: Use the hardware MAC address provided by the Opta's Ethernet controller, or generate a unique MAC based on the MCU unique ID / Notecard Device ID.

### 2.4. Logic & Functionality
*   **Client Configuration**:
    *   The client successfully supports many sensor types (Analogue, Current Loop, Pulse/RPM).
    *   The `printHardwareRequirements` function is a nice utility for verifying deployment needs.
*   **Server Data Aggregation**:
    *   The ring buffer implementation for Serial Logs (`ServerSerialBuffer`, `ClientSerialBuffer`) is efficient and well-bounded.
    *   **Concurrency**: The main loop handles many tasks (Web requests, Notecard polling, Time sync). Ensure that `handleWebRequests` does not starve the `pollNotecard` logic if the server is under heavy HTTP load.

## 3. Recommendations

### Immediate Actions
1.  **MAC Address Fix**: Remove the hardcoded MAC address in the Server or add logic to only use it if the hardware MAC is invalid. The Opta should have a factory-assigned MAC.
2.  **Watchdog during FTP**: Verify that `performFtpBackup` kicks the watchdog. Code review suggests it might be a single blocking call.

### Future Improvements
1.  **Secret Management**: Move FTP passwords and high-value keys to a separate, potentially encrypted, storage area or a separate config file with stricter permissions if the OS supported it (though on bare metal Mbed, file separation is enough).
2.  **Async Network Operations**: If the web server load increases, consider moving the blocking Notecard polling or FTP operations to a separate thread (Mbed OS supports threads), though this introduces complexity with sharing `Wire` (I2C) resources.

## 4. Conclusion
The codebase is in a very healthy state for a V1.0 release. It prioritizes reliability and safety, which is appropriate for a monitoring system. Addressing the MAC address collision issue is the only "Blocking" finding for a multi-device deployment.

---
**Review Status**: **APPROVED** (pending MAC address fix)
