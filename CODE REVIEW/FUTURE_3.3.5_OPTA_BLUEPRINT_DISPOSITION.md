# Future Improvement 3.3.5 — `Arduino_Opta_Blueprint` Directory Disposition

**Priority:** Low  
**Effort:** 1–2 hours  
**Risk:** Very Low — organizational/build system change  
**Prerequisite:** None  
**Date:** 2026-02-26  

---

## Problem Statement

The `Arduino_Opta_Blueprint/` directory in the repository root appears to be a **vendored copy** of the Arduino Pro Opta expansion library (specifically the "Opta Blueprint" library for custom expansion modules). Its own `.git/` directory indicates it was cloned from a separate repository.

### Current Directory Contents

```
Arduino_Opta_Blueprint/
├── .git/                          ← Separate git repo inside main repo
├── .gitignore
├── AboutCustomExpansionType.md
├── AddCustomExpansion.md
├── examples/
├── firmwares/
├── fwUpdater/
├── library.properties
├── LICENSE
├── OptaBlueProtoco.ods
├── Protocol.md
├── README.md
├── src/
└── tests/
```

### `library.properties` (key metadata)

The library is likely published in the Arduino Library Manager as `Arduino_Opta_Blueprint` (or similar), meaning it can be installed via:
```
arduino-cli lib install Arduino_Opta_Blueprint
```

### Issues with Current Approach

1. **Nested `.git` directory** — The directory has its own `.git`, making it a "git submodule" in spirit but not in configuration. This causes confusion:
   - `git status` in the parent repo shows `Arduino_Opta_Blueprint/` as untracked
   - Changes to the vendored copy aren't tracked by the parent repo's git
   - `git clone` of the parent repo doesn't populate this directory

2. **Version ambiguity** — No indication of which version/commit of the library is vendored. If a bug is found, it's unclear whether updating the library would fix it.

3. **Repository size** — The vendored library adds several MB to clone size, including its own `firmwares/` directory with binary files.

4. **Maintenance burden** — If the upstream library is updated, the vendored copy must be manually updated.

5. **License compliance** — The library has its own LICENSE file. Vendoring it creates an obligation to comply with that license and track upstream changes.

---

## Options

### Option A: Remove and Use Arduino Library Manager (Recommended)

**Action:** Delete `Arduino_Opta_Blueprint/`, add to `.gitignore`, and document the dependency in `README.md`.

```bash
# Remove the vendored directory
rm -rf Arduino_Opta_Blueprint/

# Add to .gitignore
echo "Arduino_Opta_Blueprint/" >> .gitignore
```

Add to README.md:
```markdown
### Dependencies

Install the following Arduino libraries before compiling:

| Library | Install Command |
|---------|----------------|
| Arduino_Opta_Blueprint | `arduino-cli lib install Arduino_Opta_Blueprint` |
| Blues note-arduino | `arduino-cli lib install Blues_Wireless_Notecard` |
| ArduinoJson | `arduino-cli lib install ArduinoJson` |
```

**Pros:**
- Clean repository — no vendored code
- Library updates are handled by Library Manager
- Smaller clone size
- No license compliance concerns with vendored code

**Cons:**
- Build requires internet access (first time) to install the library
- Library version may differ from what was tested
- If the library is removed from Library Manager, builds break

**Mitigation for version pinning:**
```bash
# Install a specific version
arduino-cli lib install Arduino_Opta_Blueprint@1.0.3
```

Document the tested version in a `DEPENDENCIES.md` or `library_versions.txt` file.

---

### Option B: Convert to Git Submodule

**Action:** Properly configure `Arduino_Opta_Blueprint/` as a git submodule.

```bash
# Remove the vendored directory (preserving history)
rm -rf Arduino_Opta_Blueprint/

# Add as a proper submodule
git submodule add https://github.com/arduino-libraries/Arduino_Opta_Blueprint.git Arduino_Opta_Blueprint
git submodule update --init
```

**Pros:**
- Version is pinned to a specific commit
- `git clone --recursive` populates it automatically
- Updates are explicit (`git submodule update --remote`)

**Cons:**
- Submodules are notoriously confusing for contributors
- `git clone` without `--recursive` leaves it empty (build fails)
- Adds complexity to CI/CD pipelines

---

### Option C: Move to `vendor/` or `lib/` Directory

**Action:** Move to a clearly-named directory and document its purpose.

```bash
mkdir -p vendor
mv Arduino_Opta_Blueprint/ vendor/Arduino_Opta_Blueprint/
```

Add `vendor/README.md`:
```markdown
# Vendor Dependencies

This directory contains vendored (copied) third-party libraries.

## Arduino_Opta_Blueprint
- Source: https://github.com/arduino-libraries/Arduino_Opta_Blueprint
- Version: Unknown (vendored before version tracking)
- License: [see vendor/Arduino_Opta_Blueprint/LICENSE]
- Purpose: Arduino Pro Opta expansion module support (custom I2C expansions)
```

**Pros:**
- Clear organizational structure
- No external dependency on Library Manager
- Works offline
- Build is self-contained

**Cons:**
- Still inflates repository size
- Still requires manual updates
- Doesn't solve the nested `.git` issue (must remove `.git/` from vendored copy)

---

### Option D: Keep As-Is (Do Nothing)

**Action:** Leave the directory untouched.

**Pros:** No risk of breaking anything.

**Cons:** All current issues remain — nested `.git`, untracked files, version ambiguity, repository bloat.

---

## Recommendation

**Option A (Remove + Library Manager)** is the cleanest approach for most projects. However, since TankAlarm deployments may need to compile firmware in field locations without reliable internet, **Option C (vendor/)** is a more practical choice:

1. Move to `vendor/Arduino_Opta_Blueprint/`
2. Remove the nested `.git/` directory
3. Add a `VENDORED_VERSION.txt` file with the commit hash/tag
4. Document the dependency in the main README

### Immediate Cleanup (regardless of option chosen)

```bash
# Remove the nested .git directory — this should be done regardless
rm -rf Arduino_Opta_Blueprint/.git
```

This prevents the "repo within a repo" confusion while deciding on the final disposition.

---

## Usage in the TankAlarm Project

Before making any changes, verify how the library is actually used:

1. **Does any sketch `#include` files from this directory?**
   - Search for `#include` referencing Arduino_Opta_Blueprint paths
   - Check if the Arduino IDE/CLI automatically detects it as a library

2. **Is it compiled as part of any sketch?**
   - Check if Arduino's library detection picks it up from the repo root
   - Or if a `library.properties` symlink exists in the sketches

3. **Is it only reference documentation?**
   - The `Protocol.md`, `AboutCustomExpansionType.md` files suggest it may have been cloned for reference rather than as a build dependency

If it's **only used for reference** (not compiled), Option A (remove) is clearly correct — just save the relevant docs elsewhere.

---

## Testing Plan

| Test | Procedure | Pass Criteria |
|------|-----------|---------------|
| Determine actual usage | Grep for includes from Arduino_Opta_Blueprint | Document what includes exist |
| Client compiles without it | Temporarily rename, compile Client | Pass / fail determines if it's a build dependency |
| Server compiles without it | Same test for Server | Pass / fail |
| Viewer compiles without it | Same test for Viewer | Pass / fail |
| I2C Utility compiles without it | Same test for I2C Utility | Pass / fail |

If all 4 sketches compile without it → Option A is safe.  
If any sketch fails → Option C (vendor/) is needed.

---

## Files Affected

| File | Change |
|------|--------|
| `Arduino_Opta_Blueprint/` | Moved or removed |
| `.gitignore` | Add exclusion pattern if removed |
| `README.md` | Add dependency documentation |
| `vendor/VENDORED_VERSION.txt` (new) | If Option C chosen |
