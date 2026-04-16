# FTPS Library Integration Study — ArduinoOPTA-FTPS × TankAlarm Server

**Date:** April 15, 2026 (updated April 16, 2026)  
**Scope:** Research and feasibility assessment — **implementation started April 16**  
**Subject:** Evaluating integration of `dorkmo/ArduinoOPTA-FTPS` library into `TankAlarm-112025-Server-BluesOpta.ino`  
**Related Documents:**
- `FTPS_IMPLEMENTATION_04132026.md` — Original FTPS design note
- `FTPS_IMPLEMENTATION_CHECKLIST_04132026.md` — Phase-by-phase checklist
- `FTPS_REPOSITORY_REVIEW_04132026.md` — Repository creation plan
- `FTPS_SPIKE_PLAN_04142026.md` — Phase 0 TLSSocketWrapper feasibility spike

---

## Executive Summary

The `ArduinoOPTA-FTPS` library (https://github.com/dorkmo/ArduinoOPTA-FTPS) is a new, experimental Explicit FTPS client library purpose-built for Arduino Opta. It implements the exact transport layer that the TankAlarm FTPS implementation plan identified as the preferred path: Mbed `TLSSocketWrapper`-based Explicit FTPS with protected passive data channels.

**Compatibility verdict:** The two repositories are **highly compatible in intent and architecture**, but integration requires concrete work in both the library and the server. The library handles the hard TLS transport problem that the TankAlarm FTPS plan identified as the blocking risk. However, the library is experimental and unvalidated on real hardware, and the TankAlarm server's FTP surface is broader than the library's current API (backup/restore of 9+ files, client config manifests, monthly history archives, FTP archive retrieval).

**Recommendation:** Adopt the library as the FTPS transport layer. Plan server-side refactoring to bridge the gap between the library's buffer-based API and the server's multi-file workflow. Track a short list of library enhancements that would reduce integration friction.

---

## Status Update — April 16, 2026

Since this study was written, the following progress has been made:

| Item | Original Status | Current Status |
|------|----------------|----------------|
| Library hardware validation | Unvalidated | **Complete** — all 10 test steps pass on Opta against WD My Cloud PR4100 |
| Library version | Pre-release | **v0.1.0** (commit `73e63b9`, pushed to `main`) |
| `MKD` (mkdir) support | Gap — Section 3.2 | **Resolved** — `mkd()` added to library |
| `SIZE` support | Gap — Section 5.2 | **Resolved** — `size()` added to library |
| Watchdog callback hook | Gap — Section 5.2 | **Resolved** — `setTraceCallback()` provides phase-change hooks; server can kick watchdog in the callback |
| Phase 0 (library validation) | Blocking prerequisite | **Complete** |
| Phase 1 (compile verification) | Not started | **Complete** — server compiles with library present (junction-based) |
| Examples | 5 examples | **7 examples** — added `PyftpdlibLiveTest`, `WDMyCloudLiveTest` |
| Maturity score | 3/10 | **7/10** — hardware-validated, 7 examples, stable API |
| Feature completeness score | 7/10 | **9/10** — `mkd()` and `size()` close the two highest-priority gaps |

**Remaining gaps from Section 3.2:** Directory listing (`NLST`/`MLSD`) — still not available, but the server's manifest-based workaround is adequate. All other gaps are resolved.

**Updated recommendation:** Proceed directly to Phase 2 (Config Schema + Transport Bridge). The library's v0.1.0 API is stable and validated. Server-side integration is the remaining work.

---

## 1. ArduinoOPTA-FTPS Library — Current State Summary

### What it provides (v1 scope)
- Explicit FTPS client (`AUTH TLS` → `PBSZ 0` → `PROT P`)
- Passive mode only
- Binary transfer only
- `FtpsClient` class with `begin()`, `connect()`, `store()`, `retrieve()`, `quit()`, `lastError()`
- `FtpsServerConfig` with host, port, user, password, TLS server name, trust mode, fingerprint, root CA PEM
- SHA-256 fingerprint pinning trust mode
- Imported PEM certificate trust mode
- Fail-closed: no silent fallback to plaintext FTP
- Certificate validation required (`validateServerCert = true` enforced)
- `IFtpsTransport` abstraction with `MbedSecureSocketFtpsTransport` backend
- TLS session reuse hinting (control→data channel session caching)
- Peer certificate fingerprint extraction for enrollment workflows
- `FtpsError` enum with 17 specific error codes

### What it does NOT provide
- Implicit FTPS
- Active mode
- Directory listing (`NLST`, `MLSD`, `LIST`)
- File management (`DELE`, `RNFR`/`RNTO`, `MKD`, `RMD`)
- `FEAT` / capability discovery
- Stream-oriented transfers (only buffer-based `store()`/`retrieve()`)
- Persistent config storage
- Any UI or web interface
- Watchdog integration
- Any TankAlarm-specific logic

### Current status
- Experimental — no hardware validation yet
- Not published to Arduino Library Manager
- CC0-1.0 license (public domain dedication)
- Single contributor (`dorkmo`)
- First commit: April 15, 2026
- Examples: BasicUpload, BasicDownload, FileZillaLiveTest, WebHarnessLiveTest, FtpsSpikeTest

---

## 2. TankAlarm Server — Current FTP Surface Inventory

### Configuration (`ServerConfig` struct, ~line 337)
| Field | Type | Purpose |
|-------|------|---------|
| `ftpEnabled` | `bool` | Master FTP enable/disable |
| `ftpPassive` | `bool` | Passive mode toggle (always true in practice) |
| `ftpBackupOnChange` | `bool` | Auto-backup on config save |
| `ftpRestoreOnBoot` | `bool` | Restore from FTP at boot |
| `ftpPort` | `uint16_t` | FTP server port (default 21) |
| `ftpHost[64]` | `char[]` | FTP server hostname/IP |
| `ftpUser[32]` | `char[]` | FTP username |
| `ftpPass[32]` | `char[]` | FTP password |
| `ftpPath[64]` | `char[]` | Base remote path |

### Transport layer (`FtpSession`, ~line 913)
```cpp
struct FtpSession {
  EthernetClient ctrl;
};
```
- Plain TCP only — no TLS
- Single `EthernetClient` for control channel
- Data channel opened as new `EthernetClient` per transfer
- `ftpReadResponse()` — multi-line FTP response parser (~line 3667)
- `ftpSendCommand()` — send + read response (~line 3720)
- `ftpConnectAndLogin()` — plain connect, `USER`, `PASS`, `TYPE I` (~line 3727)
- `ftpEnterPassive()` — `PASV` with IPv4 tuple parsing (~line 3767)
- `ftpStoreBuffer()` — passive-mode upload (~line 3818)
- `ftpRetrieveBuffer()` — passive-mode download (~line 3855)
- `ftpQuit()` — send `QUIT` + stop (~line 3909)

### High-level FTP operations
| Operation | Function | Lines | Description |
|-----------|----------|-------|-------------|
| Full backup | `performFtpBackupDetailed()` | ~4391 | Iterates 9 `kBackupFiles[]`, uploads each, then uploads client config manifest + per-client configs |
| Full restore | `performFtpRestoreDetailed()` | ~4492 | Downloads 9 backup files, then downloads client manifest + per-client configs |
| Client configs backup | `ftpBackupClientConfigs()` | ~4231 | Builds manifest, uploads manifest + each client JSON |
| Client configs restore | `ftpRestoreClientConfigs()` | ~4280 | Downloads manifest, downloads each client JSON |
| Archive month to FTP | `archiveMonthToFtp()` | ~4804 | Builds monthly history JSON, uploads to history/ subfolder |
| Load archived month | `loadArchivedMonth()` | ~5014 | Downloads monthly history JSON from FTP (16KB static buffer) |
| Cached archive load | `loadFtpArchiveCached()` | ~5551 | Caches last loaded FTP archive to avoid repeated downloads |

### FTP call sites that require FTPS coverage
1. Manual backup via REST API (`POST /api/ftp-backup`)
2. Manual restore via REST API (`POST /api/ftp-restore`)
3. Auto-backup on config change (`gPendingFtpBackup`)
4. Boot-time restore (`ftpRestoreOnBoot`)
5. Monthly history archive upload
6. Archive download for cold-tier history display
7. Client config manifest backup/restore

### Web UI surface
- Server Settings page: FTP host/port/user/password/path inputs, enable/passive/auto-backup/restore-on-boot toggles
- Backup Now / Restore Now buttons
- JavaScript load/save through `/api/settings`

### Notecard sync surface
- FTP settings serialized to shortened keys (`en`, `pas`, `boc`, `rob`, `pt`, `hst`, `usr`, `pth`) for Notecard fleet management

---

## 3. Compatibility Analysis

### 3.1 — Strong alignment

| Aspect | TankAlarm needs | Library provides | Match |
|--------|----------------|------------------|-------|
| Target hardware | Arduino Opta | Arduino Opta only | **Exact** |
| TLS backend | Mbed `TLSSocketWrapper` | Mbed `TLSSocketWrapper` | **Exact** |
| FTPS mode | Explicit TLS (`AUTH TLS`) | Explicit TLS only | **Exact** |
| Transfer mode | Passive only | Passive only | **Exact** |
| Transfer type | Binary | Binary only | **Exact** |
| Control port | 21 default | 21 default | **Exact** |
| Networking | `PortentaEthernet` + `Ethernet` | Same | **Exact** |
| `Ethernet.getNetwork()` | Required for Mbed socket access | Used in `begin()` | **Exact** |
| Trust model — fingerprint | SHA-256 leaf cert pin | SHA-256 fingerprint pinning | **Exact** |
| Trust model — imported cert | PEM trust certificate | Imported PEM trust | **Exact** |
| Fail-closed policy | No fallback to plaintext | Enforced | **Exact** |
| Cert validation default | `true` | `true` (enforced in current build) | **Exact** |
| Upload primitive | `STOR` with buffer | `store()` with buffer | **Direct** |
| Download primitive | `RETR` into buffer | `retrieve()` with buffer | **Direct** |
| FTP command flow | `USER`→`PASS`→`TYPE I`→`PASV`→`STOR`/`RETR` | Same flow inside `connect()`+`store()`/`retrieve()` | **Direct** |

### 3.2 — Gaps and friction points

| Gap | Impact | Who needs to change | Severity |
|-----|--------|-------------------|----------|
| **No `MKD` (mkdir)** | Server needs to ensure remote directories exist before upload. Currently relies on the FTP server auto-creating dirs or pre-existing paths. | Library (v2 candidate) or server workaround | Medium |
| **No directory listing** | `ftpRestoreClientConfigs()` currently uploads a manifest file to enumerate clients. Without `NLST`/`MLSD`, the manifest workaround remains necessary. | Library (v2 candidate) | Low |
| **No multi-file session** | Library `store()`/`retrieve()` each do `PASV` + open data channel per call. Server does 10+ transfers per backup. Each will require a separate passive data channel + TLS handshake. | Neither — architecturally fine, but performance cost. | Low-Medium |
| **Buffer size limits** | Library `retrieve()` reads into a caller-provided buffer. Server's `loadArchivedMonth()` uses a 16KB static buffer; client configs are up to 4KB each. `FtpsClient` has `kMaxRootCaPemLen = 4097`. The data buffers are caller-managed, so size is not a library constraint. | Server manages buffer allocation | Low |
| **No `FtpSession` equivalent** | TankAlarm's `FtpSession` is a simple struct wrapping `EthernetClient`. The library uses `FtpsClient` as the session object. The mapping is straightforward. | Server refactor | Low |
| **Credential field sizes** | TankAlarm: `ftpUser[32]`, `ftpPass[32]`. Library: `kMaxUserLen = 96`, `kMaxPasswordLen = 128`. Library is more generous; no conflict. | None | None |
| **Host field size** | TankAlarm: `ftpHost[64]`. Library: `kMaxHostLen = 128`. No conflict. | None | None |
| **No `DELE` (delete)** | Server does not currently delete remote files, so this is not a gap for current use cases. | None for now | None |
| **Watchdog integration** | Server kicks watchdog before/during FTP operations. Library has no watchdog awareness. | Server (call watchdog around library calls) | Low |
| **Error model mismatch** | Server uses `bool` + `char error[]` pattern. Library uses `bool` + `char error[]` + `FtpsError` enum. The patterns are similar enough to bridge. | Server refactor (minor) | Low |
| **No insecure/plaintext mode** | Library enforces `validateServerCert = true`. Server currently supports plain FTP. During transition, server may want to keep plain FTP available. | Server must maintain parallel code paths during transition, or commit to FTPS-only | Medium |
| **Config schema gap** | Library needs `FtpsServerConfig` (host, port, user, password, tlsServerName, trustMode, fingerprint, rootCaPem, validateServerCert). Server `ServerConfig` has no FTPS fields yet. | Server (add fields per `FTPS_IMPLEMENTATION_CHECKLIST_04132026.md` Phase 1) | Medium |
| **No stream-oriented transfer** | Library `store()` requires the entire payload in a contiguous buffer. For the server's current `FTP_MAX_FILE_BYTES` (24KB) ceiling, this is fine. If files grow larger, a streaming API would be needed. | Library (v2 candidate) | Low |

### 3.3 — Dependency and build compatibility

| Aspect | Status |
|--------|--------|
| Board package | Both target `arduino:mbed_opta` ≥ 4.5.0 |
| Library dependencies | `PortentaEthernet`, `Ethernet` — already used by TankAlarm |
| Mbed headers | `netsocket/TCPSocket.h`, `netsocket/TLSSocketWrapper.h` — available in installed core |
| Architecture filter | `library.properties` says `architectures=mbed_opta` — matches TankAlarm target |
| License | CC0-1.0 — no restrictions on use, modification, or integration |
| Arduino Library Manager | Not published yet — must use local checkout or git submodule |
| Header conflicts | No overlapping header names with TankAlarm or Arduino_Opta_Blueprint |
| Symbol conflicts | Library uses namespaced enums (`FtpsError`, `FtpsTrustMode`, etc.) — no collision with TankAlarm's `FtpSession` or `FtpResult` |

---

## 4. What Changes on the TankAlarm Server

### 4.1 — New dependency
- Add `ArduinoOPTA-FTPS` as a library dependency (local checkout or submodule until published).
- Add `#include <FtpsClient.h>` and `#include <FtpsTypes.h>` to the server sketch.

### 4.2 — Config schema additions
Per the existing `FTPS_IMPLEMENTATION_CHECKLIST_04132026.md` Phase 1, `ServerConfig` needs:
```
ftpSecurityMode       // 0=plain, 1=explicit TLS
ftpTlsTrustMode       // 0=fingerprint, 1=imported-cert
ftpValidateServerCert // bool, default true
ftpTlsServerName[128] // TLS hostname for cert validation
ftpTlsFingerprint[65] // SHA-256 leaf-cert pin (64 hex + null)
ftpTlsCertPath[48]    // e.g., "/ftps/server_trust.pem" or ""
```
These map directly to `FtpsServerConfig` fields.

### 4.3 — Transport layer replacement

**Remove / deprecate:**
- `struct FtpSession { EthernetClient ctrl; };` (line ~913)
- `ftpReadResponse()` (~line 3667) — replaced by library internal
- `ftpSendCommand()` (~line 3720) — replaced by library internal
- `ftpConnectAndLogin()` (~line 3727) — replaced by `FtpsClient::begin()` + `connect()`
- `ftpEnterPassive()` (~line 3767) — replaced by library internal (PASV inside `store()`/`retrieve()`)
- `ftpStoreBuffer()` (~line 3818) — replaced by `FtpsClient::store()`
- `ftpRetrieveBuffer()` (~line 3855) — replaced by `FtpsClient::retrieve()`
- `ftpQuit()` (~line 3909) — replaced by `FtpsClient::quit()`

**Replace with a bridge function set:**
```cpp
// Conceptual bridge — exact implementation TBD
static FtpsClient gFtpsClient;

static bool ftpsConnectAndLogin(char *error, size_t errorSize) {
  if (!gFtpsClient.begin(Ethernet.getNetwork(), error, errorSize)) {
    return false;
  }
  FtpsServerConfig config;
  config.host = gConfig.ftpHost;
  config.port = gConfig.ftpPort;
  config.user = gConfig.ftpUser;
  config.password = gConfig.ftpPass;
  config.tlsServerName = gConfig.ftpTlsServerName;
  config.trustMode = (gConfig.ftpTlsTrustMode == 0)
    ? FtpsTrustMode::Fingerprint
    : FtpsTrustMode::ImportedCert;
  config.fingerprint = gConfig.ftpTlsFingerprint;
  config.rootCaPem = /* loaded PEM or nullptr */;
  config.validateServerCert = gConfig.ftpValidateServerCert;
  return gFtpsClient.connect(config, error, errorSize);
}

static bool ftpsStoreBuffer(const char *remoteFile,
                            const uint8_t *data, size_t len,
                            char *error, size_t errorSize) {
  return gFtpsClient.store(remoteFile, data, len, error, errorSize);
}

static bool ftpsRetrieveBuffer(const char *remoteFile,
                               uint8_t *out, size_t outMax, size_t &outLen,
                               char *error, size_t errorSize) {
  return gFtpsClient.retrieve(remoteFile, out, outMax, outLen, error, errorSize);
}

static void ftpsQuit() {
  gFtpsClient.quit();
}
```

### 4.4 — Plain FTP transition strategy

During the transition period, the server should support both plain FTP and FTPS:
- If `ftpSecurityMode == 0` (plain), use the existing `EthernetClient`-based code path.
- If `ftpSecurityMode == 1` (explicit TLS), use the `FtpsClient` code path.
- This avoids a flag-day migration and lets users adopt FTPS at their own pace.

Post-transition, the plain FTP path can be deprecated and removed.

### 4.5 — Higher-level function updates

`performFtpBackupDetailed()` and `performFtpRestoreDetailed()` need to:
1. Call `ftpsConnectAndLogin()` instead of `ftpConnectAndLogin()`.
2. Call `ftpsStoreBuffer()` / `ftpsRetrieveBuffer()` instead of `ftpStoreBuffer()` / `ftpRetrieveBuffer()`.
3. Call `ftpsQuit()` instead of `ftpQuit()`.
4. Continue to kick the watchdog between transfers (library does not do this).
5. The `buildRemotePath()` helper remains unchanged.
6. The `kBackupFiles[]` table remains unchanged.

The archive functions (`archiveMonthToFtp()`, `loadArchivedMonth()`, `loadFtpArchiveCached()`) and client config functions (`ftpBackupClientConfigs()`, `ftpRestoreClientConfigs()`) follow the same pattern.

### 4.6 — Web UI additions

Per `FTPS_IMPLEMENTATION_CHECKLIST_04132026.md` Phase 3:
- Security mode selector (plain / explicit TLS)
- Trust mode selector (fingerprint / imported cert)
- TLS server name input
- Certificate fingerprint input
- PEM certificate import control
- Certificate validation toggle
- Inline help text

### 4.7 — Notecard sync additions
- New shortened keys for FTPS fields in the outbound/inbound Notecard settings sync.

### 4.8 — Config persistence

New FTPS fields must be saved/loaded in `saveConfig()` / `loadConfig()`:
- `ftpSecurityMode` under `doc["ftp"]["security"]`
- `ftpTlsTrustMode` under `doc["ftp"]["trustMode"]`
- `ftpValidateServerCert` under `doc["ftp"]["validateCert"]`
- `ftpTlsServerName` under `doc["ftp"]["tlsServerName"]`
- `ftpTlsFingerprint` under `doc["ftp"]["fingerprint"]`
- PEM certificate stored separately at `/ftps/server_trust.pem`

Legacy configs without FTPS fields must still load cleanly with safe defaults (plain FTP, no TLS).

### 4.9 — Memory / resource impact estimate

| Resource | Current FTP | With FtpsClient | Delta |
|----------|------------|-----------------|-------|
| Flash (code) | ~3KB FTP helpers | ~8–12KB library estimate | +5–9KB |
| RAM (static) | `FtpSession` ≈ trivial | `FtpsClient` ≈ ~5.3KB (internal buffers) | +~5KB |
| RAM (heap) | `EthernetClient` per transfer | `TCPSocket` + `TLSSocketWrapper` per channel | TLS buffers ~16–33KB during handshake |
| Config RAM | 9 FTP fields | +6 FTPS fields (~300 bytes) | +~300B |

The TLS handshake heap cost is the biggest new resource pressure. The Opta has 8MB SDRAM, so this should be manageable, but heap fragmentation patterns should be monitored during testing.

---

## 5. Suggested Changes to ArduinoOPTA-FTPS Repository

These are enhancements that would reduce integration friction for TankAlarm (and other consumers). They are suggestions, not requirements — the server can work around most of these.

### 5.1 — High priority (would significantly ease integration)

| Suggestion | Rationale | Effort |
|------------|-----------|--------|
| **Add `MKD` support** | Server uploads to paths like `{base}/{serverUid}/clients/{uid}.json`. If the directory tree doesn't exist, `STOR` fails. Adding `mkd()` or `ensureDirectory()` lets the library create paths before upload. | Medium |
| **Add `connected()` or `isConnected()` accessor** | Server does multi-file sessions (10+ transfers per backup). Being able to check session health mid-workflow helps error handling. | Low |
| **Expose `FtpsClient::lastErrorMessage()`** | The library passes error messages through `char*` out-params. A convenience accessor for the last error string would simplify logging in multi-step workflows where the error buffer may be reused. | Low |
| **Document maximum concurrent `FtpsClient` instances** | TankAlarm would only use one, but documenting the constraint (heap cost per instance) helps integration planning. | Low (docs only) |

### 5.2 — Medium priority (nice to have for v1.x)

| Suggestion | Rationale | Effort |
|------------|-----------|--------|
| **Add `CWD` support** | Server backup/restore uses a fixed base path. `CWD` once after login would simplify all subsequent `STOR`/`RETR` paths. | Low |
| **Add `SIZE` support** | Before `RETR`, knowing the file size helps the server pre-allocate the right buffer and avoid truncation. | Low |
| **Allow optional `validateServerCert = false` in debug builds** | During TankAlarm development and testing, strict cert validation adds friction. A compile-time or explicit opt-in `FTPS_ALLOW_INSECURE_DEBUG` flag would help without weakening the default policy. | Low |
| **Add a watchdog callback hook** | Library I/O loops (`writeAll`, `ftpReadResponse`, `retrieve` data read loop) use `delay(5)` for would-block retries. Adding an optional `void (*onPoll)()` callback would let the server kick its watchdog inside library wait loops. | Medium |
| **Add `NOOP` or keepalive support** | For long multi-file sessions, some servers may time out the control channel. A `noop()` method would let the server send keepalives between transfers. | Low |

### 5.3 — Lower priority (v2 candidates)

| Suggestion | Rationale | Effort |
|------------|-----------|--------|
| **Stream-oriented `store()` variant** | If TankAlarm backup payloads grow beyond `FTP_MAX_FILE_BYTES` (24KB), a callback-based or chunked upload would avoid needing the entire file in RAM. | High |
| **`NLST` / `MLSD` directory listing** | Would eliminate the need for the server's `clients_manifest.txt` workaround. | Medium |
| **`DELE` support** | Useful for cleaning up old archive files on the NAS. | Low |
| **Connection pooling / reuse across `quit()`/`connect()` cycles** | If the server needs to reconnect mid-workflow (e.g., after a transient error), reusing the transport allocation would reduce heap churn. | Medium |

---

## 6. Integration Approach — Recommended Phasing

### Phase 0 — Library validation (before any server changes)
1. Run the `FtpsSpikeTest.ino` from the ArduinoOPTA-FTPS repo against the PR4100 on real Opta hardware.
2. Run the `FileZillaLiveTest.ino` against a FileZilla Server instance.
3. Confirm: control-channel TLS handshake, protected passive data channel, upload, download, session reuse behavior.
4. Document any server-specific issues (TLS session reuse enforcement, cipher preferences, certificate handling quirks).

### Phase 1 — Library checkout + compile verification
1. Add ArduinoOPTA-FTPS as a local library in the Arduino libraries folder or as a git submodule.
2. Add `#include <FtpsClient.h>` to the server sketch.
3. Confirm the server sketch still compiles cleanly with the library present.
4. Confirm no symbol conflicts between the library and the server's existing FTP code.

### Phase 2 — Config schema + UI (per existing checklist Phases 1–3)
1. Add FTPS fields to `ServerConfig`.
2. Add FTPS fields to config load/save.
3. Add FTPS fields to the settings API.
4. Add FTPS controls to the Server Settings web UI.
5. Add FTPS fields to Notecard settings sync.

### Phase 3 — Transport bridge (per existing checklist Phases 4–6)
1. Implement the `ftpsConnectAndLogin()` / `ftpsStoreBuffer()` / `ftpsRetrieveBuffer()` / `ftpsQuit()` bridge functions.
2. Wire the security-mode switch: plain FTP → existing code, explicit TLS → library code.
3. Ensure watchdog kicks happen around every library call.

### Phase 4 — Integration testing (per existing checklist Phases 7–10)
1. Test manual backup/restore via REST API with FTPS.
2. Test auto-backup on config change with FTPS.
3. Test boot-time restore with FTPS.
4. Test monthly archive upload with FTPS.
5. Test archived month download with FTPS.
6. Test client config manifest backup/restore with FTPS.
7. Regression-test plain FTP still works when security mode is set to plain.

### Phase 5 — Cleanup and docs
1. Update `FTPS_IMPLEMENTATION_04132026.md` to reference the library.
2. Update the Bill of Materials if the library adds flash/RAM cost.
3. Document the FTP→FTPS migration path for field-deployed units.

---

## 7. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Library fails Opta hardware validation | Medium | Blocking | Run Phase 0 spike before any server changes. The TankAlarm spike plan already covers this. |
| PR4100 requires TLS session reuse that the library can't satisfy | Medium | High | Library already implements best-effort session caching. If PR4100 enforces strict reuse, this is a library-level fix (transport layer). |
| TLS handshake heap exhaustion on Opta | Low | Medium | Opta has 8MB SDRAM. Monitor heap during multi-file backup sessions. |
| Library API changes before v1 release | Medium | Low | Library is CC0-licensed; TankAlarm can fork or pin to a specific commit. |
| Multi-file backup performance regression | Low-Medium | Low | Each `store()`/`retrieve()` does a separate PASV + TLS data handshake. May be slower than plain FTP. Acceptable for backup/restore use case. |
| Watchdog starvation during TLS handshake | Medium | Medium | Library's `delay(5)` polling loops don't kick the watchdog. Server must ensure watchdog is kicked before each library call. Consider requesting a poll callback in the library. |
| PEM certificate storage on LittleFS | Low | Low | Library expects PEM as a `const char*`. Server loads from `/ftps/server_trust.pem` and passes the string. Standard file I/O. |

---

## 8. Comparison: Library vs. Building In-House

| Factor | Use ArduinoOPTA-FTPS | Build from scratch in server .ino |
|--------|---------------------|----------------------------------|
| Transport layer effort | Done (TLSSocketWrapper wiring, PASV parsing, session caching) | Must write all of this from scratch |
| Trust model | Implemented (fingerprint + PEM + normalization + validation helpers) | Must implement |
| Error taxonomy | 17 specific error codes | Must design |
| Maintenance burden | Shared with library repo | All internal |
| Testability | Library has standalone examples and spike sketches | Must create test harnesses |
| Code size in server .ino | Removes ~300 lines of low-level FTP code, adds ~5 lines of `#include` + bridge | Adds ~500+ lines of TLS transport code to an already 12000+ line file |
| Reusability | Other Opta projects can use the same library | Locked into TankAlarm |
| Risk | Library is experimental / unvalidated | Same risk — TLS transport is unvalidated either way |

**Verdict:** Using the library is clearly preferable. The transport-layer work is the hardest and most specialized part of the FTPS migration, and it's exactly what the library provides.

---

## 9. Open Questions

1. **Should TankAlarm fork the library or depend on it?** Given CC0 licensing and single-contributor status, a fork gives more control. A dependency is cleaner if the library stabilizes.

2. **Should the library add a `poll()` or `onIdle()` callback for watchdog integration?** This would be the cleanest solution for the server's watchdog requirements during TLS handshakes and data transfers.

3. **Should the server's existing plain FTP code be kept permanently or sunset after FTPS validation?** Keeping both paths adds maintenance cost but provides a fallback.

4. **What is the realistic timeline for the library to complete Opta hardware validation?** This gates all server-side integration work.

5. **Should `MKD` be added to the library, or should the server pre-create directories via a separate mechanism?** For the PR4100, the directory structure likely needs to be created once and then persists.

---

## 10. Summary of Compatibility Scores

| Dimension | Score | Notes |
|-----------|-------|-------|
| Hardware target | 10/10 | Identical: Arduino Opta |
| Networking stack | 10/10 | Identical: PortentaEthernet + Mbed |
| FTPS protocol | 10/10 | Identical: Explicit TLS, passive, binary |
| Trust model | 10/10 | Identical: fingerprint + PEM |
| API style | 8/10 | Similar buffer-based approach; minor bridging needed |
| Feature completeness | 7/10 | No MKD, no directory listing, no stream transfers |
| Maturity | 3/10 | Experimental, no hardware validation, no releases |
| Server integration effort | 6/10 | Config schema, UI, bridge functions, testing all needed |
| **Overall** | **8/10** | **Highly compatible in design; integration work is mostly on the server side** |
