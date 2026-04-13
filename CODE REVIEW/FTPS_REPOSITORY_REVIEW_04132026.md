# Future Repository Review - Arduino Opta FTPS

**Date:** April 13, 2026  
**Scope:** Planning note for a future standalone FTPS repository derived from TankAlarm FTPS work  
**Status:** Design and extraction guidance only; do not publish a standalone repo until the TankAlarm FTPS path is proven on-device

---

## Executive Summary

A separate FTPS repository may be worth creating later, but only if the work is extracted as a small, reusable FTPS transport/client layer rather than as copied TankAlarm application code.

The GitHub landscape appears to have:

- many Arduino FTP repositories
- several Arduino TLS building blocks
- no obvious, well-known, generic Explicit FTPS client library aimed at Arduino Opta / Mbed Ethernet

That means there may be a real gap worth filling. It does **not** mean the project should publish immediately.

The correct sequence is:

1. prove Explicit FTPS against the PR4100 inside TankAlarm
2. refactor the FTPS logic behind a clean internal boundary
3. extract only the generic FTPS pieces into a new repository
4. publish only after the code no longer depends on TankAlarm settings UI, config schema, archive rules, or Notecard behavior

---

## Key Findings

### 1. Do not publish the first working TankAlarm FTPS code as a library

The first implementation will still be shaped by TankAlarm-specific concerns:

- embedded settings UI
- `ServerConfig` schema
- filesystem persistence details
- backup and archive naming rules
- product-specific error logging and watchdog behavior

If those concerns leak into a public repo, the result will be harder to maintain and less useful to other users.

### 2. The future repo should be transport-oriented, not product-oriented

The reusable part is the FTPS control/data-channel layer, not the TankAlarm backup workflow.

The public code should solve:

- Explicit TLS control-channel upgrade via `AUTH TLS`
- `PBSZ 0`
- `PROT P`
- passive-mode protected data sockets
- upload/download primitives
- certificate trust configuration

The public code should **not** include:

- web UI pages
- JSON settings handlers
- filesystem path policy
- backup scheduling
- archive business logic
- NAS-specific application flows beyond examples

### 3. The best technical base for a future repo is still the Mbed secure-socket path

Based on the installed Opta core research, the strongest future-repo foundation is:

- `Ethernet.getNetwork()`
- `NetworkInterface`
- `TCPSocket`
- `TLSSocketWrapper`

This remains the preferred direction because Explicit FTPS needs a STARTTLS-style control-channel upgrade after `AUTH TLS`, and `TLSSocketWrapper` is the cleanest match for that behavior.

### 4. The future repo should start with a narrow promise

The first public release should be intentionally small:

- Arduino Opta / Mbed first
- Explicit TLS only
- passive mode only
- binary transfers only
- certificate validation required unless the caller explicitly opts out for bring-up
- upload/download only

It should **not** promise generic support for every Arduino board on day one.

---

## GitHub Landscape Snapshot

Current GitHub results suggest the following:

- There are many Arduino FTP projects and FTP examples.
- There are reusable TLS libraries such as `SSLClient` and `ArduinoBearSSL`.
- Search results for `Arduino FTPS` and `FTPS client Arduino` are noisy and largely dominated by plain FTP, ESP32-specific projects, servers, or modem-specific code.
- There does not appear to be a clear, mature, Opta-oriented, generic Explicit FTPS client repository that makes this work redundant.

That is a reasonable argument for a future public repo **after** the TankAlarm implementation stabilizes.

---

## Recommended Future Repository Scope

If a new repo is created, the v1 scope should be:

- Explicit FTPS only
- passive mode only
- `USER`, `PASS`, `TYPE I`, `PASV`, `STOR`, `RETR`, `QUIT`
- `AUTH TLS`, `PBSZ 0`, `PROT P`
- fingerprint pinning
- imported PEM certificate trust
- Mbed Opta transport backend
- examples for PR4100-style NAS usage

Out of scope for v1:

- Implicit FTPS
- active mode
- full directory listing/parsing API
- recursive sync features
- generic web UI
- configuration persistence
- backup/restore product workflows

---

## Specific Code Recommendations

## 1. Create an internal extraction boundary inside TankAlarm first

Before any new repo exists, split the FTPS code inside TankAlarm into library-shaped units.

Recommended internal file split:

```text
TankAlarm-112025-Server-BluesOpta/
  FtpsTypes.h
  FtpsErrors.h
  FtpsTransport.h
  FtpsTransport_MbedSecureSocket.cpp
  FtpsClient.h
  FtpsClient.cpp
  FtpsTrust.h
```

This should happen before extraction so the project learns where the real boundaries are while the code is still close to the product that uses it.

## 2. Keep the transport abstraction from the current FTPS design note

The existing research already points to the correct shape: keep the protocol logic separated from the socket implementation.

Recommended public transport interface:

```cpp
class IFtpsTransport {
public:
  virtual ~IFtpsTransport() {}

  virtual bool connectControl(const FtpEndpoint &endpoint,
                              const FtpTlsConfig &tls,
                              char *error,
                              size_t errorSize) = 0;

  virtual bool upgradeControlToTls(const FtpTlsConfig &tls,
                                   char *error,
                                   size_t errorSize) = 0;

  virtual bool openProtectedDataChannel(const IPAddress &host,
                                        uint16_t port,
                                        const FtpTlsConfig &tls,
                                        char *error,
                                        size_t errorSize) = 0;

  virtual int ctrlWrite(const uint8_t *data, size_t len) = 0;
  virtual int ctrlRead() = 0;
  virtual int dataWrite(const uint8_t *data, size_t len) = 0;
  virtual int dataRead() = 0;
  virtual bool ctrlConnected() const = 0;
  virtual bool dataConnected() const = 0;
  virtual void closeData() = 0;
  virtual void closeAll() = 0;
};
```

Why this matters:

- it keeps `AUTH TLS` upgrade logic out of product code
- it allows the library to stay Opta/Mbed-first without baking that decision into every FTPS helper
- it leaves room for future alternate backends if the project later broadens beyond Opta

## 3. Prefer a Mbed-native backend for the standalone repo

Recommended primary backend:

- `MbedSecureSocketFtpsTransport`

Recommended implementation base:

- `NetworkInterface *net = Ethernet.getNetwork();`
- `TCPSocket` for initial control/data TCP sockets
- `TLSSocketWrapper` for control-channel upgrade and protected data-channel wrapping

Reasons:

- best fit for Explicit FTPS `AUTH TLS` upgrade semantics
- already verified as available in the installed Opta core
- avoids making a future repo depend on a higher-level Arduino TLS wrapper before that dependency is justified
- keeps the public repo aligned with the actual research and device constraints already proven locally

Recommended fallback positions:

1. `TLSSocket` only for limited TLS-from-connect experimentation
2. `SSLClient` only as a fallback prototype path
3. custom TLS wrapper only as a last resort

## 4. Keep TankAlarm config persistence out of the future library

The current TankAlarm FTPS plan includes fields such as:

- `ftpSecurityMode`
- `ftpTlsTrustMode`
- `ftpValidateServerCert`
- `ftpTlsServerName`
- `ftpTlsCertPath`
- `ftpTlsFingerprint`

Those are valid **product integration** fields. They should not become the public library API.

Instead, the library should accept runtime FTPS settings in memory, for example:

```cpp
enum class FtpsTrustMode : uint8_t {
  Fingerprint = 1,
  ImportedCert = 2,
};

struct FtpsServerConfig {
  const char *host = nullptr;
  uint16_t port = 21;
  const char *user = nullptr;
  const char *password = nullptr;
  const char *tlsServerName = nullptr;
  FtpsTrustMode trustMode = FtpsTrustMode::Fingerprint;
  const char *fingerprint = nullptr;
  const char *rootCaPem = nullptr;
  bool validateServerCert = true;
  bool passiveMode = true;
};
```

TankAlarm can keep `ftpTlsCertPath` and filesystem loading in its own integration layer. The library should only care about the already-loaded trust material.

## 5. Keep the protocol surface intentionally small

The future repo should not start as a full FTP toolkit.

Recommended v1 public methods:

```cpp
class FtpsClient {
public:
  bool begin(NetworkInterface *network, char *error, size_t errorSize);
  bool connect(const FtpsServerConfig &config, char *error, size_t errorSize);
  bool uploadBuffer(const char *remotePath,
                    const uint8_t *data,
                    size_t len,
                    char *error,
                    size_t errorSize);
  bool downloadBuffer(const char *remotePath,
                      uint8_t *buffer,
                      size_t capacity,
                      size_t &bytesRead,
                      char *error,
                      size_t errorSize);
  bool quit(char *error, size_t errorSize);
  void close();
};
```

If streaming becomes necessary, add callback-based upload/download later. Do not complicate the first API with directory trees, wildcard sync, or active-mode support.

## 6. Preserve the known-good PASV parsing fix during extraction

The April 13 TankAlarm fix for passive-mode parsing should be treated as part of the reusable FTPS core, not as a product quirk.

Specific recommendation:

- keep the PASV parser logic that begins parsing after the `(` in the `227` response
- make that parser a standalone helper with unit-testable input/output behavior

This avoids reintroducing the already-fixed status-line digit corruption bug.

## 7. Design the public error model for field debugging

A future public repo should expose stable error categories, not only freeform strings.

Recommended error enum:

```cpp
enum class FtpsError : uint8_t {
  None = 0,
  NoNetwork,
  ControlConnectFailed,
  BannerReadFailed,
  AuthTlsRejected,
  TlsHandshakeFailed,
  CertValidationFailed,
  PbszRejected,
  ProtPRejected,
  LoginRejected,
  PasvParseFailed,
  DataConnectFailed,
  TransferFailed,
  FinalReplyFailed,
  QuitFailed,
};
```

Keep the human-readable error buffer too, but make the enum part of the public API so product integrations can react predictably.

## 8. Fail closed in the library when FTPS is selected

The library should not silently downgrade from FTPS to plain FTP.

Recommended behavior:

- if FTPS is selected and `AUTH TLS` fails, return an error
- if certificate validation is enabled and trust material is missing, return an error
- if `PBSZ 0` or `PROT P` fails, return an error
- do not continue with plaintext credentials after a TLS failure

---

## Extraction Map From Current TankAlarm Code

The current server sketch already identifies the functions that should become library code later.

Recommended mapping:

- `FtpSession` -> `FtpsSession`
- `ftpSendCommand()` -> `FtpsClient::sendCommand()`
- `ftpConnectAndLogin()` -> `FtpsClient::connect()`
- `ftpEnterPassive()` -> `FtpsClient::enterPassive()`
- `ftpStoreBuffer()` -> `FtpsClient::uploadBuffer()`
- `ftpRetrieveBuffer()` -> `FtpsClient::downloadBuffer()`
- `ftpQuit()` -> `FtpsClient::quit()`

Code that should remain in TankAlarm:

- `performFtpBackupDetailed()` orchestration
- `performFtpRestoreDetailed()` orchestration
- client manifest backup/restore policy
- monthly archive policy
- browser download endpoint integration
- settings UI and settings JSON

---

## Proposed Repository Layout

```text
ArduinoOptaFTPS/
  README.md
  LICENSE
  library.properties
  src/
    FtpsClient.h
    FtpsClient.cpp
    FtpsTypes.h
    FtpsErrors.h
    FtpsTrust.h
    transport/
      IFtpsTransport.h
      MbedSecureSocketFtpsTransport.h
      MbedSecureSocketFtpsTransport.cpp
  examples/
    OptaExplicitFtpsPr4100Upload/
    OptaExplicitFtpsPr4100Download/
    OptaFingerprintValidation/
    OptaImportedCertValidation/
  docs/
    protocol-flow.md
    trust-model.md
    supported-targets.md
    limitations.md
  extras/
    test-notes/
  .github/
    workflows/
```

---

## Staged Plan For Creating The Repo Later

## Phase 0 - Prove the path inside TankAlarm

Do not extract anything until the following are true in the product code:

- Explicit FTPS backup succeeds against the PR4100
- Explicit FTPS restore succeeds against the PR4100
- protected passive data channel works for both `STOR` and `RETR`
- fingerprint validation works
- imported PEM trust works
- watchdog and timeout behavior are acceptable across repeated transfers

## Phase 1 - Split the product code into reusable internal modules

Refactor the server sketch so the FTPS pieces no longer depend on:

- `gServerConfig`
- embedded HTML/JS
- filesystem path policy
- archive-specific filenames
- product-wide logging globals

## Phase 2 - Extract a minimal standalone library

Only after Phase 1 is complete:

- copy the transport, protocol, trust, and error modules into a new repo
- add one upload example and one download example
- add PR4100-oriented documentation
- keep the public scope explicitly narrow

## Phase 3 - Publish after release gates pass

Publish only when:

- there are no TankAlarm includes or globals left in the extracted code
- examples compile cleanly in the intended Arduino environment
- the public README matches the actual tested scope
- the API no longer looks temporary or product-shaped

---

## Release Gates For A Public Repository

Require all of the following before public release:

- on-device PR4100 validation for upload and download
- stable `AUTH TLS` control upgrade
- stable `PROT P` passive data channel behavior
- stable trust handling for both fingerprint and imported cert workflows
- no silent fallback to plaintext FTP
- documented memory/timeout limitations
- at least one example that is not TankAlarm-specific

---

## Naming Recommendation

If the first public version is truly Opta-specific, `ArduinoOptaFTPS` is a reasonable name.

If the code later broadens to more Mbed-based Arduino boards, a better long-term name would be closer to:

- `ArduinoMbedFtpsClient`
- `ArduinoExplicitFtpsClient`

Avoid using a very broad name such as `ArduinoFTPS` unless the library genuinely supports multiple boards and transports.

---

## Main Risks For A Standalone Repo

1. Publishing too early and freezing a product-shaped API.
2. Over-promising generic Arduino support when the code is really Opta/Mbed-specific.
3. Underestimating the support burden created by NAS quirks and certificate troubleshooting.
4. Letting TankAlarm config/UI assumptions leak into the public library surface.
5. Taking on an unnecessary third-party TLS dependency when the Mbed-native path is already the strongest candidate.

---

## Bottom-Line Recommendation

Yes, a future FTPS repository could make sense, because the current ecosystem seems to have a real gap between plain Arduino FTP libraries and generic TLS building blocks.

The correct target is **not** "publish the TankAlarm FTPS code." The correct target is:

- prove the Explicit FTPS transport in TankAlarm first
- extract a small Mbed-native FTPS client layer second
- publish only when the code is clearly reusable and no longer depends on TankAlarm application behavior

If that sequence is followed, the future repo has a good chance of being useful to other Arduino Opta users instead of becoming another one-off product dump.