# Version Number Locations

**Current Version:** 1.1.8  
**Last Updated:** March 16, 2026

This document lists every place a version number appears across the codebase and project files, so bumping the version in a future release is straightforward.

---

## Source Code (must update each release)

| File | Location | Type |
|------|----------|------|
| `TankAlarm-112025-Common/src/TankAlarm_Common.h` | Line ~17 — `#define FIRMWARE_VERSION "1.1.6"` | **Primary definition** — all runtime version strings derive from this |
| `TankAlarm-112025-Common/library.properties` | Line 2 — `version=1.1.6` | Arduino library version |
| `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` | Line 2 — `// Version: 1.1.6` | File header comment |
| `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` | Line 3 — `Version: 1.1.6` | File header comment |
| `TankAlarm-112025-Viewer-BluesOpta/TankAlarm-112025-Viewer-BluesOpta.ino` | Line 3 — `Version: 1.1.6` | File header comment |

---

## Project Documentation (must update each release)

| File | Location | Notes |
|------|----------|-------|
| `README.md` | Line 1 — title heading `# TankAlarm v1.1.6` | Main project title |
| `README.md` | Line 4 — `**Version:** 1.1.6` | Version badge |
| `README.md` | Line ~342 — `Firmware version 1.1.6 confirmed` | Deployment checklist item |
| `README.md` | Line ~362 — `Flash all devices with v1.1.6 firmware` | Deployment step 1 |
| `TankAlarm-112025-BillOfMaterials.md` | Line 3 — `**Version:** 1.1.6` | BOM header |

---

## Code Review / Release Docs (update or create each release)

| File | Location | Notes |
|------|----------|-------|
| `CODE REVIEW/SECURITY_AND_ADVANCED_FEATURES.md` | `#define FIRMWARE_VERSION "1.1.2"` (code block) | Example code block |
| `CODE REVIEW/SECURITY_AND_ADVANCED_FEATURES.md` | `/api/dfu/status` JSON example — `currentVersion`, `availableVersion` | API example payload |
| `CODE REVIEW/SECURITY_AND_ADVANCED_FEATURES.md` | Notehub upload form example — `**Version**: 1.1.2` | Upload instructions |
| `CODE REVIEW/SECURITY_AND_ADVANCED_FEATURES.md` | Serial monitor example — `FIRMWARE UPDATE AVAILABLE: v1.1.2` | Serial output example |
| `CODE REVIEW/COMMUNICATION_ARCHITECTURE_VERDICT_02192026.md` | `**Firmware version:** \`1.1.2\`` | Architecture review |
| `CODE REVIEW/CODE_REVIEW_01072026_AI.md` | `**Version:** 1.1.2` | Code review header |
| `CODE REVIEW/CODE_REVIEW_02062026.md` | `**Version:** 1.1.2` | Code review header |
| `CODE REVIEW/CODE_REVIEW_02192026_COMPREHENSIVE.md` | `**Version Reviewed:** 1.1.2` | Code review header |
| `CODE REVIEW/CODE_REVIEW_02192026_COMPREHENSIVE_CLAUDE.md` | `**Version Reviewed:** 1.1.2` | Code review header |

> **Note:** Keep historical code review documents accurate to the version they reviewed — don't update older reviews retroactively. Only update the code review docs that are ongoing/current.

---

## Tutorials (must update each release)

All files under `Tutorials/Tutorials-112025/` use `1.1.2+` compatibility footers and inline version references. Updated via bulk script. Files that contain version numbers:

| File | Version Reference Type |
|------|----------------------|
| `ADVANCED_CONFIGURATION_GUIDE.md` | Compatibility footer |
| `BACKUP_RECOVERY_GUIDE.md` | Compatibility footer |
| `CLIENT_INSTALLATION_GUIDE.md` | Boot banner example, compatibility footer |
| `DASHBOARD_GUIDE.md` | Dashboard UI example, compatibility footer |
| `FIRMWARE_UPDATE_GUIDE.md` | Code block, Notehub form field, serial output examples, file naming examples, git tag example |
| `FLEET_SETUP_GUIDE.md` | Compatibility footer |
| `NOTEHUB_ROUTES_SETUP.md` | Title heading, compatibility footer |
| `QUICK_START_GUIDE.md` | Revision history table, compatibility footer |
| `README.md` (Tutorials) | Version compatibility table (12 rows), compatibility footer |
| `RELAY_CONTROL_GUIDE.md` | Compatibility footer |
| `SENSOR_CALIBRATION_GUIDE.md` | Compatibility footer |
| `SERVER_INSTALLATION_GUIDE.md` | Boot banner example, compatibility footer |
| `TROUBLESHOOTING_GUIDE.md` | Serial output example, compatibility footer |
| `UNLOAD_TRACKING_GUIDE.md` | Compatibility footer |
| `WIRING_DIAGRAM.md` | Compatibility footer (if present) |

---

## README.md Changelog Section (do NOT change old entries)

The `README.md` changelog keeps a running history. When releasing a new version:
1. **Add** a new `### v1.1.2 (February 23, 2026)` section at the top of the changelog.
2. **Do not** modify the `### v1.1.1` or earlier entries.
3. Add a corresponding release notes link under **Code Reviews & Release History**.

---

## Recommended Version Bump Procedure

1. Update `FIRMWARE_VERSION` in `TankAlarm_Common.h` — this is the canonical source of truth.
2. Update `library.properties` to match.
3. Update all three `.ino` file header comments.
4. Update `README.md` title, badge, checklist, and deployment step.
5. Update `TankAlarm-112025-BillOfMaterials.md`.
6. Bulk-replace tutorials (PowerShell one-liner):
   ```powershell
   Get-ChildItem -Path "Tutorials" -Recurse -Include "*.md" | ForEach-Object {
     $c = Get-Content $_.FullName -Raw -Encoding UTF8
     $u = $c -replace '1\.1\.2', '1.1.3'   # adjust versions
     if ($c -ne $u) { Set-Content $_.FullName $u -Encoding UTF8 -NoNewline; Write-Host $_.Name }
   }
   ```
7. Create a new release notes file `CODE REVIEW/V1.1.2_RELEASE_NOTES.md`.
8. Add changelog entry and release notes link to `README.md`.
9. Update `CODE REVIEW/VERSION_LOCATIONS.md` (this file) with the new version.
