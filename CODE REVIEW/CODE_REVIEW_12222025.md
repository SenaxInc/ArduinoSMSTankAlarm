# Code Review: Tank Alarm 112025 (Opta + Blues)
**Date:** December 22, 2025
**Reviewer:** GitHub Copilot

## Overview
This review covers the "112025" version of the Tank Alarm system, specifically the Server, Client, and Viewer components designed for the Arduino Opta and Blues Wireless Notecard.

**Scope:**
- `TankAlarm-112025-Server-BluesOpta/`
- `TankAlarm-112025-Client-BluesOpta/`
- `TankAlarm-112025-Viewer-BluesOpta/`

## Summary
The codebase is well-structured, modular, and demonstrates a high level of maturity for an embedded system. It effectively leverages the capabilities of the Arduino Opta (STM32H7) and the Blues Notecard. The code handles complex tasks such as telemetry aggregation, remote configuration, and reliable communication with robust error handling.

## Key Strengths
1.  **Architecture**: Clear separation of concerns between Server, Client, and Viewer roles. The Server acts as the central hub, aggregating data and managing configuration, while Clients are focused on sensing and reporting.
2.  **Hardware Abstraction**: The code uses conditional compilation (`#ifdef`) effectively to support different hardware platforms (Opta/Mbed vs. STM32duino), ensuring portability.
3.  **Robustness**:
    -   **Watchdog Timer**: Integrated watchdog support (`IWatchdog` or Mbed `Watchdog`) prevents system hangs.
    -   **Error Handling**: Notecard requests and JSON parsing are checked for errors.
    -   **Memory Management**: Heap allocation for large JSON documents is handled correctly with explicit `delete` calls.
4.  **Configuration**: Extensive use of `#define` macros for compile-time configuration and `struct`s for runtime configuration.
5.  **Features**:
    -   **Remote Configuration**: The system supports pushing configuration updates from the Server to Clients via the Notecard.
    -   **Calibration**: A learning-based calibration system is implemented.
    -   **Web Interface**: The Server hosts a comprehensive web dashboard and configuration tools.

## Findings & Recommendations

### 1. Network Configuration (High Priority)
**Observation:** IP addresses and MAC addresses are hardcoded in the firmware.
-   `gStaticIp`, `gStaticGateway`, etc. are defined as `static` global variables.
-   `gMacAddress` is hardcoded.

**Risk:** This limits deployment flexibility. Multiple devices on the same network would require recompilation to avoid IP/MAC conflicts.

**Recommendation:**
-   **DHCP**: Enable DHCP by default, with a fallback to a static IP if DHCP fails.
-   **Configuration File**: Load network settings (IP, Gateway, Subnet) from a configuration file on the filesystem (e.g., `network.json`) or from the Notecard's environment variables.
-   **Unique MAC**: While the locally administered bit is used, ensure a mechanism to generate a unique MAC address (e.g., based on the MCU's unique ID) to avoid collisions.

### 2. String Usage (Medium Priority)
**Observation:** The `String` class is used in several places (e.g., `performFtpBackup`, HTTP handling).

**Risk:** On embedded systems, excessive use of `String` can lead to heap fragmentation over time, potentially causing instability.

**Recommendation:**
-   While the Opta has ample RAM (1MB), it is best practice to minimize `String` usage. Prefer `char` arrays and `snprintf` for string manipulation where possible, especially in long-running loops.

### 3. Security (Medium Priority)
**Observation:**
-   The system uses a PIN (`configPin`) for protecting sensitive web actions.
-   `SERVER_PRODUCT_UID` and `VIEWER_PRODUCT_UID` are hardcoded.

**Recommendation:**
-   Ensure the PIN is strong and changed from any default.
-   Consider loading Product UIDs from a secure configuration or Notecard environment variables to allow for easier fleet management without recompilation.

### 4. Code Duplication (Low Priority)
**Observation:** There is some code duplication between the Server, Client, and Viewer sketches (e.g., `initializeNotecard`, `ensureTimeSync`, Watchdog setup).

**Recommendation:**
-   If the project grows, consider creating a shared library (e.g., `TankAlarmCommon`) to encapsulate common functionality. This would reduce maintenance effort and ensure consistency.

### 5. Memory Safety (Medium Priority)
**Observation:** `DynamicJsonDocument` is allocated on the heap in multiple locations across Server, Client, and Viewer.

**Findings:**
-   ✅ **Server `sendClientDataJson`** (line 2668): Uses `std::unique_ptr` correctly - no leak possible.
-   ✅ **Server HTTP body parsing** (line 2811): Uses `std::unique_ptr` correctly.
-   ✅ **Viewer `sendTankJson`** (line 528): Uses `std::unique_ptr` correctly.
-   ✅ **Viewer `fetchViewerSummary`** (line 591): Uses `std::unique_ptr` and properly calls `NoteFree(json)` in both success and OOM paths.
-   ✅ **Client config loading** (lines 939, 958, 1154): Uses `std::unique_ptr` throughout.
-   ⚠️ **Server `handleContactsGet`** (line 4279): Uses raw `new` with manual `delete` - see issue below.

**Issue in `handleContactsGet`:**
```cpp
DynamicJsonDocument *docPtr = new DynamicJsonDocument(CONTACTS_JSON_CAPACITY);
// ... processing ...
delete docPtr;  // Only called at end of function
```
If `serializeJson` throws or memory operations fail between allocation and delete, memory will leak.

**Recommendations:**
-   **Convert `handleContactsGet`** to use `std::unique_ptr<DynamicJsonDocument>` for consistency with other functions.
-   The Client code properly guards malloc with null checks and frees in error paths (line 929-946) - this pattern is correct.

### 6. FTP Implementation Review (Medium Priority)
**Observation:** The FTP backup/restore implementation is functional but has several areas for improvement.

**Findings:**

#### A. Connection Management ✅
-   `ftpQuit()` properly sends QUIT command and closes the control connection.
-   `ftpConnectAndLogin()` validates host presence and connection success.
-   `FtpSession` struct correctly encapsulates the control connection.

#### B. Error Handling ⚠️
-   **Partial Failure Recovery**: When `performFtpBackup` fails mid-backup (e.g., after 2 of 5 files), there's no rollback or indication of which files succeeded.
-   **Silent Failures**: In `ftpBackupClientConfigs`, individual file failures are logged but don't stop the backup or report to caller.

**Recommendation:** Consider returning a more detailed result structure:
```cpp
struct FtpResult {
  bool success;
  uint8_t filesProcessed;
  uint8_t filesFailed;
  char lastError[128];
};
```

#### C. Buffer Size Concerns ⚠️
-   **`ftpBackupClientConfigs`** uses a 2KB manifest buffer (line 1867) - adequate for ~40 clients with typical UID/site lengths.
-   **`ftpRestoreClientConfigs`** uses a 1KB config buffer (line 1962) per client - may truncate large configs.
-   **`FTP_MAX_FILE_BYTES` = 24KB** - enforced correctly in `writeBufferToFile` but not in all retrieve paths.

**Recommendation:** The 1KB client config buffer should be increased to match `FTP_MAX_FILE_BYTES` or at least 2KB to prevent silent truncation of larger configurations.

#### D. Timeout Handling ✅
-   `FTP_TIMEOUT_MS` (8 seconds) is applied consistently in `ftpReadResponse` and `ftpRetrieveBuffer`.
-   The implementation correctly handles slow/stalled connections.

#### E. Data Connection Cleanup ⚠️
-   In `ftpStoreBuffer` and `ftpRetrieveBuffer`, the data connection (`dataClient`) is properly stopped before checking transfer completion.
-   However, if `ftpEnterPassive` succeeds but `dataClient.connect` fails, subsequent operations may leave the server in an unexpected state.

**Recommendation:** After a data connection failure, consider sending `ABOR` to reset server state before the next operation.

#### F. Path Traversal Security ✅
-   `buildRemotePath` constructs paths safely using `snprintf` with bounded output.
-   No user-controlled input flows directly into path construction.

### 7. Client Memory Management (Low Priority)
**Observation:** The Client code (lines 929-946) uses `malloc`/`free` for file reading with proper null checks.

**Findings:**
-   ✅ Null check after `malloc` (line 930 equivalent).
-   ✅ `free(buffer)` called in both success (line 946) and error (line 941) paths.
-   ✅ File size validated before allocation (8KB limit, line 924-927).
-   ✅ `std::unique_ptr` used for `DynamicJsonDocument` in same function.

**Recommendation:** None - this pattern is correct and robust.

## Conclusion
The "112025" code release is in excellent shape. The logic is sound, and the implementation is robust. Addressing the network configuration rigidity is the most significant improvement to be made for scalable deployment.

**Status:** **APPROVED** (with recommendations)
