# Code Review - April 16, 2026 (GitHub Copilot GPT-5.4)

## Scope

Targeted readiness review of `TankAlarm-112025-FTPS_Server_Test` with focus on:

- first USB deployment on Arduino Opta
- FTPS live-test flow against the Python FTPS test server
- GitHub Direct updater behavior and release-asset readiness
- compatibility with the current `server_config.json` schema used by the main server sketch

## Summary

- New findings: 4
- Highest-priority finding: the currently published GitHub release is missing the FTPS test firmware asset required for bi-directional remote switching.
- Code fix applied during review: `loadConfigFromFilesystem()` in the FTPS test sketch now stream-parses `server_config.json` and preserves saved static network settings.
- Practical verdict: the FTPS test sketch is suitable for a first USB flash and bench test, but the GitHub updater is not fully ready until the FTPS test asset is actually published in the release.

## Findings

### CR-H1: Current live release does not publish the FTPS test asset required by the updater

- **Component:** Release / updater integration
- **Files:**
  - `TankAlarm-112025-FTPS_Server_Test/TankAlarm-112025-FTPS_Server_Test.ino`
  - `.github/workflows/release-server-112025.yml`
- **Location:**
  - `checkGitHubReleaseForTarget()` around line 595
  - `attemptGitHubDirectInstall()` around line 818
  - release workflow lines 84-109
- **Severity:** High

**Problem**

The FTPS test sketch correctly expects release assets named:

- `TankAlarm-Server-vX.Y.Z.bin`
- `TankAlarm-FTPS-Test-vX.Y.Z.bin`

The release workflow is also configured to build and upload both artifacts. However, the currently published `v1.6.2` release only exposes `TankAlarm-Server-v1.6.2.bin` plus source archives. The FTPS test asset is not present in the live release metadata.

**Impact**

- A USB-flashed FTPS test unit should be able to switch back to the regular server firmware because the server asset exists.
- Remote switching into the FTPS test image is not available from the current release.
- Any readiness claim for the GitHub updater must distinguish between "server target works" and "all targets work".

**Recommendation**

- Re-cut or republish the release so both firmware assets are present.
- Verify the GitHub Releases API returns `name`, `browser_download_url`, `size`, and `digest` for both binaries before field use.
- Treat updater validation as incomplete until the FTPS test asset is visible in the live release, not just in workflow YAML.

### CR-M1: FTPS test sketch did not preserve saved static IP settings from `server_config.json`

- **Component:** FTPS test config compatibility
- **File:** `TankAlarm-112025-FTPS_Server_Test/TankAlarm-112025-FTPS_Server_Test.ino`
- **Location:** `loadConfigFromFilesystem()` around lines 1235-1288
- **Severity:** Moderate
- **Status:** Fixed during this review

**Problem**

The sketch header promises that the FTPS test firmware preserves the regular server's Ethernet identity and saved network configuration. Before this review, `loadConfigFromFilesystem()` only imported `useStaticIp` and ignored the saved `staticIp`, `gateway`, `subnet`, and `dns` arrays. It also parsed the file through a fixed 2 KB buffer instead of streaming the JSON directly from the file.

**Impact**

- Devices configured for a saved static IP could boot the FTPS test sketch on fallback compile-time defaults instead of the known server address.
- Larger `server_config.json` payloads could fail to parse unnecessarily.
- First USB deployment risk was higher than the sketch comments implied.

**Resolution Applied**

- Switched the loader to `deserializeJson(doc, f)` so the file is parsed directly.
- Imported `staticIp`, `gateway`, `subnet`, and `dns` from the saved server config.

**Recommendation**

- Keep this loader aligned with the main server's network schema whenever `server_config.json` changes.
- Preserve the "USB-first swap should keep the same address" assumption as an explicit compatibility requirement.

### CR-M2: FTPS test sketch still does not import the modern obfuscated FTP credentials from saved config

- **Component:** FTPS test config compatibility
- **Files:**
  - `TankAlarm-112025-FTPS_Server_Test/TankAlarm-112025-FTPS_Server_Test.ino`
  - `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
- **Location:**
  - FTPS test loader reads `ftp.user` / `ftp.pass` or legacy top-level `ftpUser` / `ftpPass` around lines 1311-1327
  - main server stores `ftp.userObf` / `ftp.passObf` and decodes them around lines 4651-4659 and 4773-4776
- **Severity:** Moderate

**Problem**

The current server schema stores FTP credentials in obfuscated fields under the nested `ftp` object. The FTPS test sketch only reads plaintext `ftp.user`, `ftp.pass`, or legacy top-level keys. It does not decode `userObf` / `passObf`.

**Impact**

- Host and port may carry over from the saved config, but credentials often will not.
- A freshly USB-flashed FTPS test sketch may still show default credentials and fail the first FTPS test until the operator updates them through the web UI.
- This is a real compatibility gap between the FTPS test harness and the main server's current config format.

**Recommendation**

- Reuse the main server's `decodeFtpCredential()` logic, ideally by moving the helper into shared code.
- If shared decoding is intentionally out of scope, document clearly that FTPS credentials must be re-entered manually after flashing the test sketch.

### CR-M3: Update discovery still depends on Notecard connectivity, not Ethernet alone

- **Component:** Updater integration
- **File:** `TankAlarm-112025-FTPS_Server_Test/TankAlarm-112025-FTPS_Server_Test.ino`
- **Location:**
  - release lookup via `web.get` in `checkGitHubReleaseForTarget()` around line 595
  - Notecard initialization in `initializeNotecard()` around line 1343
- **Severity:** Moderate

**Problem**

The sketch's direct firmware download runs over Ethernet, but release discovery still uses Notecard `web.get`. This means "Check for Updates" requires working Notecard setup and connectivity even when Ethernet is healthy.

**Impact**

- The updater can appear broken in bench testing if Ethernet is up but the Notecard product configuration or Notehub path is not ready.
- This makes the updater more operationally fragile than a pure Ethernet-based GitHub check.

**Recommendation**

- At minimum, document this dependency in the FTPS test web UI and test procedure.
- Longer term, consider doing release discovery over Ethernet in the FTPS test harness so updater behavior matches operator expectations.

## Verification Notes

- The FTPS test flow itself is coherent and exercises `begin()`, `connect()`, `mkd()`, `store()`, `size()`, `retrieve()`, and `quit()` in `runFtpsTest()` around line 1517.
- The local `ArduinoOPTA-FTPS` checkout exposes all of those methods on `FtpsClient`, so the test sketch is API-consistent with the library currently present in the workspace.
- Repository CI includes a dedicated compile step for `TankAlarm-112025-FTPS_Server_Test` in `.github/workflows/arduino-ci-112025.yml`.
- Local compile verification was not performed on this machine because `arduino-cli` is not installed in the current environment.

## Readiness Verdict

### FTPS Test Sketch

**Ready for first USB deployment and bench validation**, with the following caveats:

- static network carry-over needed a compatibility fix and has now been corrected
- saved FTP credentials may still need to be re-entered manually
- full confidence still depends on hardware validation because no local compile was run from this workstation

### GitHub Updater

**Partially ready, not fully release-ready.**

- GitHub Direct install logic appears structurally sound and closely matches the main server's implementation.
- Switching from FTPS test to regular server should be viable when the live release exposes the server binary and digest.
- Switching into the FTPS test image is not currently viable from the live release because that asset is not published.

## Recommended Next Steps

1. USB flash the FTPS test sketch onto the Opta and verify the web UI comes up on the expected preserved network address.
2. Run the FTPS test once with manually confirmed credentials if the saved config does not prefill them.
3. Validate GitHub Direct install using the `TankAlarm Server` target first.
4. Republish the release with `TankAlarm-FTPS-Test-v1.6.2.bin`, or cut the next version with both binaries, before depending on remote switching in both directions.