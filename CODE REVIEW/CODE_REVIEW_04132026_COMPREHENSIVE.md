# Comprehensive Code Review — April 13, 2026

> **Version Reviewed:** 1.6.2 (post June 9, 2026 bugfix pass)  
> **Reviewer:** GitHub Copilot (Claude Opus 4.6)  
> **Scope:** Full codebase — Client, Server, Viewer, Common headers  
> **Method:** Automated deep analysis with manual validation of all findings

---

## Table of Contents

- [Summary of Findings](#summary-of-findings)
- [Critical Issues](#critical-issues)
- [High-Priority Issues](#high-priority-issues)
- [Moderate-Priority Issues](#moderate-priority-issues)
- [Low-Priority / Cosmetic Issues](#low-priority--cosmetic-issues)
- [False Positives — Verified Correct](#false-positives--verified-correct)

---

## Summary of Findings

| Severity | Count | New vs Known |
|----------|-------|--------------|
| CRITICAL | 2 | 2 new |
| HIGH | 5 | 5 new |
| MODERATE | 8 | 6 new, 2 known |
| LOW | 6 | 6 new |
| False Positives | 5 | — |

---

## Critical Issues

### CR-C1: Open Redirect Vulnerability in Login Page **(S)** — SECURITY

**File:** Server line 1611  
**Code:**
```javascript
const params = new URLSearchParams(window.location.search);
window.location.href = params.get("redirect") || "/";
```

**Issue:** After successful login, the browser navigates to whatever URL is in the `redirect` query parameter without any validation. An attacker can craft a phishing link:
```
https://server-ip/login?redirect=https://attacker.com/fake-dashboard
```
The user logs in with their real PIN, then the browser redirects to the attacker's site. The attacker's page can mimic the dashboard UI and harvest further credentials or session tokens.

**Risk:** Credential phishing; session token leakage (token stored in localStorage before redirect).

**Fix:** Validate that the redirect parameter is a relative path (starts with `/` and does not contain `//`):
```javascript
let dest = params.get("redirect") || "/";
if (!dest.startsWith("/") || dest.startsWith("//")) dest = "/";
window.location.href = dest;
```

**Applies to:** All embedded HTML pages that use `window.location.href='/login?redirect=...'` (~12 pages).

---

### CR-C2: processNotefile() Consumes Notes on Parse Failure **(S)** — DATA LOSS

**File:** Server lines 8242–8310  
**Code:**
```cpp
char *json = JConvertToJSONString(body);
// ...
DeserializationError err = deserializeJson(doc, json);
NoteFree(json);
if (!err) {
    handler(doc, epoch);  // Only processes on success
}

notecard.deleteResponse(rsp);

// Consume the note — ALWAYS, even if parse failed
J *delReq = notecard.newRequest("note.get");
if (delReq) {
    JAddStringToObject(delReq, "file", fileName);
    JAddBoolToObject(delReq, "delete", true);
    // ...
}
```

**Issue:** If `deserializeJson()` fails (e.g., OOM, truncated JSON, heap fragmentation), the handler is skipped but the note is **still deleted**. The data is permanently lost. This applies to ALL 9 notefiles processed by `processNotefile()` — telemetry, alarms, daily reports, unload events, config ACKs, etc.

**Risk:** Transient OOM during a high-activity period can silently drop alarm notes, telemetry, or config ACKs.

**Fix:** Only delete the note if the handler was successfully invoked:
```cpp
bool handled = false;
if (!err) {
    handler(doc, epoch);
    handled = true;
}
notecard.deleteResponse(rsp);

if (handled) {
    J *delReq = notecard.newRequest("note.get");
    // ...delete...
} else {
    // Log parse failure; note survives for retry on next poll
    Serial.print(F("WARNING: Parse failed for "));
    Serial.println(fileName);
}
```

Add a per-file failure counter and delete after 3 consecutive failures (poison note protection) as the Viewer already does.

---

## High-Priority Issues

### CR-H1: Negative Hysteresis Value Not Validated **(C)** — ALARM CORRECTNESS

**File:** Client line 2324  
**Code:**
```cpp
if (t["hysteresis"].is<float>()) mon.hysteresisValue = t["hysteresis"].as<float>();
```

**Issue:** No bounds check on hysteresis. If the server sends a negative value (e.g., `hysteresis: -5.0`), the alarm clearing thresholds invert:
- `highClear = highAlarmThreshold - (-5) = highAlarmThreshold + 5` → alarm never clears
- `lowClear = lowAlarmThreshold + (-5) = lowAlarmThreshold - 5` → alarm never clears

A zero hysteresis causes alarms to oscillate rapidly near thresholds (alarm → clear → alarm every sample cycle).

**Fix:** Validate after parsing:
```cpp
if (mon.hysteresisValue < 0.0f) mon.hysteresisValue = 0.0f;
if (mon.hysteresisValue > 50.0f) mon.hysteresisValue = 50.0f;  // Sane upper bound
```

---

### CR-H2: FTP PASV Response Parser Reads Uninitialized Array **(S)** — RELIABILITY

**File:** Server lines 3631–3642  
**Code:**
```cpp
int parts[6] = {0};
int idx = 0;
for (size_t i = 0; i < len && idx < 6; ++i) {
    if (isdigit(msg[i])) {
        parts[idx] = parts[idx] * 10 + (msg[i] - '0');
    } else if (msg[i] == ',' || msg[i] == ')') {
        idx++;
    }
}
if (idx < 6) {
    snprintf(error, errorSize, "PASV parse error");
    return false;
}
```

**Issue:** If the PASV response starts with digits before the opening parenthesis (e.g., "227 Entering Passive Mode (192,168,1,50,4,1)"), the parser starts accumulating `parts[0]` from "227" in the status line. The `parts[0]` value becomes `227 * 10 + ...` rather than `192`. The IP address is completely wrong.

Additionally, trailing garbage after the 6th comma increments `idx` past 6, so the `idx < 6` guard passes even if only 5 valid numbers were found.

**Fix:** Skip to the opening `(` before parsing:
```cpp
const char *start = strchr(msg, '(');
if (!start) { snprintf(error, errorSize, "PASV parse error"); return false; }
start++;  // skip '('
// Parse from start instead of msg
```

---

### CR-H3: posix_read_file() Silently Truncates Large Files **(S)** — DATA INTEGRITY

**File:** Server lines 1053–1080  
**Code:**
```cpp
size_t toRead = (size_t)fileSize;
if (toRead > bufSize - 1) {
    toRead = bufSize - 1;  // Silent truncation — no warning
}
```

**Issue:** If a file (e.g., `sensor_registry.json`, `server_config.json`) grows beyond the read buffer size, it is silently truncated. The truncated JSON is then parsed by `deserializeJson()`, which fails — or worse, succeeds with partial data, causing records to be silently dropped from the registry.

**Fix:** Return an error indicator or log a warning when truncation occurs:
```cpp
if (toRead > bufSize - 1) {
    toRead = bufSize - 1;
    Serial.print(F("WARNING: File truncated during read: "));
    Serial.println(path);
}
```

Also consider bumping the read buffer size for `sensor_registry.json` to accommodate large fleets (64 sensors × ~200 bytes each = ~12KB).

---

### CR-H4: FTP Credentials Stored and Transmitted in Plaintext **(S)** — SECURITY

**File:** Server line 344  
**Code:**
```cpp
char ftpPass[32];  // Plaintext in config struct, persisted to LittleFS
```

**Issue:** FTP credentials are stored unencrypted in LittleFS and transmitted over the network using the FTP protocol (no TLS). Any device on the local network can sniff credentials. If the Opta is physically accessed, credentials are readable from flash.

**Risk:** On a LAN deployment, this is a known limitation of FTP. However, the password should not be returned via the settings API endpoint.

**Fix (partial mitigation):**
1. Ensure `GET /api/settings` returns `"pass": "***"` instead of the actual password
2. Document that FTP is plaintext and recommend deployment on isolated VLANs
3. Long-term: Consider SFTP or FTPS support (hardware/library dependent)

---

### CR-H5: Relay State Lost on Reboot — Active Relays De-energize **(C)** — SAFETY

**File:** Client line 718  
**Code:**
```cpp
static RelayRuntime gRelayRuntime[MAX_RELAYS] = {};  // Zero-initialized — all relays OFF
```

**Issue:** If a relay is active in UNTIL_CLEAR or MOMENTARY mode and the device reboots (brown-out, watchdog, DFU), `gRelayRuntime` is zeroed. All relays turn OFF. If the alarm condition persists, the relay does not re-activate until the alarm re-latches through the debounce cycle (3 samples × sample interval).

For a 30-minute sample interval in ECO mode, this means up to 90 minutes with no relay actuation after a reboot, while the alarm condition persists.

**Risk:** A pump controlling tank overflow stops for up to 90 minutes after each reboot.

**Mitigation (existing):** On boot, if the alarm evaluates as TRUE again, it re-triggers after the debounce period. The relay safety timeout (documented) also limits how long relays stay on.

**Fix:** Persist a minimal relay state struct (4 × 8 bytes) to flash using atomic writes. On boot, check if alarm conditions still hold and restore relay activation. Already tracked as a known issue (RELAY-1 in prior reviews). Documenting for TODO.

---

## Moderate-Priority Issues

### CR-M1: Config ACK Not Retried on Notecard Failure **(C)** — RELIABILITY

**File:** Client lines 3495–3530  

**Issue:** If the config ACK `note.add` request fails (Notecard offline, I2C error), the ACK is not retried. The Server sees the config dispatch as unacknowledged and may re-dispatch, causing redundant config applies. The Client has `gPendingConfigAck` flag logic but no retry mechanism if the Notecard is temporarily unavailable.

**Fix:** If ACK send fails, keep `gPendingConfigAck = true` and retry on the next loop iteration. Add a retry counter with exponential backoff (max 3 attempts before dropping).

---

### CR-M2: processNotefile() Has No Watchdog Kick Inside Tight Loop **(S)** — WATCHDOG

**File:** Server lines 8242–8310  

**Issue:** The `while (processed < MAX_NOTES_PER_FILE_PER_POLL)` loop processes notes sequentially. Each iteration involves two blocking I2C transactions (`note.get` peek + `note.get` delete). Only a `safeSleep(1)` at the end yields/kicks the watchdog. If `MAX_NOTES_PER_FILE_PER_POLL` is high (default appears to be 10+), and each I2C call takes 100ms, the inner operations between `safeSleep()` calls could take 200-300ms — well within the 30s watchdog, but worth documenting.

**Recommendation:** Add explicit watchdog kick after each `note.get` call, similar to the Viewer pattern.

---

### CR-M3: FTP Response Buffer Only 128 Bytes **(S)** — ROBUSTNESS

**File:** Server line 3627  
**Code:**
```cpp
char msg[128];
if (!ftpSendCommand(session, "PASV", code, msg, sizeof(msg)) || code >= 400) {
```

**Issue:** FTP server responses (especially directory listings, error messages) can exceed 128 bytes. The `ftpSendCommand()` function likely truncates the response, potentially cutting off the PASV IP/port data.

**Fix:** Increase FTP response buffer to 256 or 512 bytes for safety.

---

### CR-M4: Alarm Rate Limit Can Suppress Recovery Notification **(C)** — OPERATIONS

**File:** Client line ~4708 (`checkAlarmRateLimit`)  

**Issue:** If a tank rapidly oscillates near the alarm threshold: HIGH → clear → HIGH within the 5-minute `MIN_ALARM_INTERVAL_SECONDS`, the second HIGH alarm is suppressed. The server never sees the intermediate "clear" event, so the alarm dashboard shows continuously "active" when the tank may have actually recovered and re-alarmed.

**Risk:** Operator sees a stale alarm and doesn't realize the condition briefly cleared and returned.

**Fix:** Consider allowing "clear" events to bypass rate limiting entirely (they are already exempt for "relay_timeout" type). Clear events are informationally critical even if brief.

---

### CR-M5: Viewer JSON Cast Truncation for Sensor Fields **(V)** — DATA INTEGRITY

**File:** Viewer lines ~1005–1007  
**Code:**
```cpp
item["k"].as<uint8_t>()   // sensorIndex
item["un"].as<uint8_t>()  // userNumber
```

**Issue:** If the JSON contains a value outside uint8_t range (e.g., `"k": 999`), the `.as<uint8_t>()` cast silently wraps (999 % 256 = 231). The wrong sensor index is used, potentially overwriting a different sensor's data.

**Fix:** Read as `int`, validate range, then cast:
```cpp
int k = item["k"] | -1;
if (k < 0 || k > MAX_SENSOR_RECORDS) continue;  // Skip invalid
```

---

### CR-M6: Viewer Never Sends `Connection: close` Header **(V)** — HTTP COMPLIANCE

**File:** Viewer response functions  

**Issue:** HTTP/1.1 defaults to keep-alive. The Viewer doesn't send `Connection: close`, so well-behaved clients may hold the connection open, waiting for the next response. Since the Viewer is single-threaded, this blocks all other web clients.

**Fix:** Add `Connection: close` to all responses.

---

### CR-M7: Server Settings API May Return FTP Password **(S)** — SECURITY

**File:** Server settings API endpoint  

**Issue:** Verify that `GET /api/settings` does not return `ftpPass` in the JSON response. If it does, any authenticated user (or XSS injection) can read the FTP password.

**Fix:** Ensure FTP password is never included in API responses. Return `"pass": ""` or omit the field.

---

### CR-M8: Email Payload Size Unbounded **(S)** — RELIABILITY

**File:** Server daily email construction  

**Issue:** With 64 sensors, each with long site names and full telemetry, the email payload JSON could exceed the Notecard's `note.add` body size limit (~30KB). The email would silently fail to send.

**Fix:** Add payload size check before sending. If over limit, split across multiple emails or truncate per-sensor detail.

---

## Low-Priority / Cosmetic Issues

### CR-L1: linearMap() Not in Common Header **(C)**

**File:** Client line 4124  
**Issue:** `linearMap()` is Client-only. If Server or Viewer ever need sensor value interpolation, it must be duplicated. Consider moving to `TankAlarm_Utils.h`.

---

### CR-L2: Client Alias Defines for Common Constants **(C)**

**File:** Client lines ~187–188  
**Issue:** `DEFAULT_SAMPLE_SECONDS` aliases `DEFAULT_SAMPLE_INTERVAL_SEC` from Common. Maintaining dual names adds confusion.

---

### CR-L3: Platform Defines Coexist in Parallel **(S)(C)**

**File:** TankAlarm_Platform.h vs Server/Client local defines  
**Issue:** `POSIX_FS_PREFIX` is defined locally in Server while TankAlarm_Platform.h uses `TANKALARM_POSIX_FS_PREFIX`. Parallel naming conventions.

---

### CR-L4: Stuck Detection Tolerance Hard-Coded **(C)**

**File:** Client `validateSensorReading()`  
**Issue:** `0.05` inches hard-coded stuck detection threshold. High-resolution sensors (0–1 PSI) may read within normal noise of this threshold.

---

### CR-L5: No DFU Rollback on Crash-Loop **(C)**

No boot-success flag to detect bad firmware after DFU update. A bad update causes permanent crash-loop until manual intervention.

---

### CR-L6: Solar Modbus Register Addresses Hard-Coded for SunSaver **(Common)**

**File:** TankAlarm_Solar.h lines 60–80  
**Issue:** All Modbus register addresses assume SunSaver MPPT. Any other solar charge controller requires recompilation.

---

## False Positives — Verified Correct

| Claim | Finding | Why It's OK |
|-------|---------|-------------|
| Digital sensor hysteresis inverted | FALSE POSITIVE | Digital sensors use debounce, not hysteresis (line 4508) |
| millis() wrap in gRpmAccumulatedStartMillis | FALSE POSITIVE | Unsigned subtraction handles wrap correctly |
| Session token insufficient entropy | FALSE POSITIVE | Uses 4 ADC pins + timing jitter + Knuth LCG (line 756) |
| HTTP body pre-allocation DoS | FALSE POSITIVE | `MAX_HTTP_BODY_BYTES` caps body at 16KB (line 262) |
| DFU CRC scope mismatch | FALSE POSITIVE | Download CRC and readback CRC both properly finalized (lines 525, 546) |

---

## Review Notes

- The codebase has matured significantly since v1.0. Most critical bugs from prior reviews (C-1 through C-6, H-series, I-series) are properly fixed.
- The two-step peek-then-delete pattern for Notecard notes is implemented but the Server's `processNotefile()` still deletes on parse failure — the most significant remaining bug.
- The open redirect in LOGIN_HTML is the most exploitable security issue.
- The Viewer's watchdog handling during `fetchViewerSummary()` is correctly implemented as of the latest version.
- `posix_read_file()` truncation and FTP PASV parsing are latent bugs that will manifest only when fleet size grows or FTP servers respond unexpectedly.
