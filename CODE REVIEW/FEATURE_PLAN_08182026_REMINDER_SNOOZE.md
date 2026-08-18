# Feature Plan — Alarm Reminder SNOOZE / UNSNOOZE (2026-08-18)

**Scenario driving this:** a real sensor alarm fired, initial SMS + email went to the correct
contacts, and the reminder engine (`checkAlarmReminders`, every `smsReminderHours` = 6 h) keeps
re-texting. The field fix is days away — the operator wants to silence *reminders* for that one
alarm without disabling alerting for the rest of the fleet, and have everything self-restore when
the sensor returns to normal.

Target: **server-only change** (client + viewer untouched). No new Notehub routes.
Proposed version: v2.2.13 (bump `FIRMWARE_VERSION` + `FIRMWARE_BUILD_SEQ` + `library.properties`).

---

## 1. Semantics (proposed — confirm before implementing)

| Action | Effect |
|---|---|
| **Snooze** (per alarming sensor) | Suppresses the *reminder engine only* for that sensor record. Indefinite — no timer; it ends only when the alarm condition exits the alarm bounds (auto-reset below) or an explicit unsnooze. **Broadcasts a one-time "reminders snoozed" notice to the alarm's subscribed recipients** (§3.9). |
| **Unsnooze** | Manually re-arms reminders for that sensor. Next reminder fires **one full interval after unsnooze** (not instantly — see §3.4). Use case: "return to normal is imminent but not certain — resume watching." **Broadcasts a one-time "reminders resumed" notice** (§3.9). |
| **Auto-reset** | Any path that clears `alarmActive` also clears the snooze. A future re-alarm on the same sensor alerts + reminds normally. No snooze-specific broadcast — the existing clear/recovery notification already covers it. |

What snooze does **NOT** suppress (intentional):
- The initial alarm SMS/email for a *new* alarm note (`handleAlarm` path — edge-triggered, unaffected).
- The clear/recovery notification when the sensor returns to normal.
- Daily reports, stale-client alerts, system alarms (solar/battery/power), OTA alerts.
- Other sensors' reminders — scope is strictly per `SensorRecord` (per `clientUid + sensorIndex`).

Design choice — **indefinite boolean snooze** rather than timed durations ("snooze 24 h") —
**APPROVED 2026-08-18**: snooze holds until the value re-enters bounds and the alarm clears
(auto-reset), or until manual unsnooze. No duration picker. If a safety net is wanted later, a
`snoozeMaxDays` config can be added without schema breakage (epoch is stored).

---

## 2. Current code anchors (verified against HEAD, server .ino)

| What | Where |
|---|---|
| `SensorRecord` struct (`alarmActive`, `alarmType[24]`, `lastSmsAlertEpoch` reminder anchor) | ~L456–491 |
| Reminder engine `checkAlarmReminders()` | ~L14419 (swept ~60 s from `loop()` ~L4711) |
| Reminder gate config `gConfig.smsReminderHours` (default 6, 0 = off) | ~L423, ~L4993 |
| Alarm-clear sites (all 5 call `clearAlarmEvent(uid, idx)`) | L11980 (config re-dispatch stuck-off), L12670 (stuck self-clear), L12692 (fault self-clear), L12885 (`handleAlarm` clear/recovery), L13200 (daily reconcile) |
| `clearAlarmEvent()` definition (single choke point for auto-reset) | ~L7383 |
| Registry persistence `saveSensorRegistry` / `loadSensorRegistry` (keys `a`/`at`/`se`/`sa`) | ~L15416 / ~L15550 |
| `/api/clients` per-sensor serializer (`sensorObj["a"]`, `["at"]` inside `clientObj["ts"]`) | ~L10516 |
| POST route dispatch cluster to copy (`/api/sms/test`, `/api/sms/optout`) | ~L9465–9481 |
| Dashboard JS `buildSiteModel` (maps `ts[]` → `t.alarm`/`t.alarmType`) | L2307 (minified PROGMEM) |
| Dashboard JS `renderDataCard` (`dc-alarm` banner + `dc-actions` button row) | L2314 (minified PROGMEM) |
| Inbound SMS keyword handler `handleSmsInbound` (STOP/START/HELP families, v2.2.3) | ~L13895 |

---

## 3. Phase 1 — Server core + dashboard buttons

### 3.1 Data model
Add to `SensorRecord` (after `smsAlertTimestamps`):
```cpp
double reminderSnoozeEpoch;   // >0 = reminders snoozed since this epoch; 0 = active
```
One field only: `epoch > 0` doubles as the boolean and gives the dashboard a "snoozed N h ago"
display. No source string stored — who/how goes to `logTransmission` instead (RAM: +8 B × MAX_SENSOR_RECORDS).

### 3.2 Reminder suppression
In `checkAlarmReminders()` right after the `if (!rec.alarmActive) continue;` line:
```cpp
if (rec.reminderSnoozeEpoch > 0.0) continue;  // operator snoozed this alarm's reminders
```

### 3.3 Auto-reset on return to normal
Add the reset inside `clearAlarmEvent(clientUid, sensorIndex)` (~L7383) — it is already called by
**all five** alarm-clear sites (verified list in §2), so one edit covers every current and future
clear path:
```cpp
SensorRecord *sn = findSensorByHash(clientUid, sensorIndex);
if (sn && sn->reminderSnoozeEpoch > 0.0) {
  sn->reminderSnoozeEpoch = 0.0;
  gSensorRegistryDirty = true;
  addServerSerialLog("Alarm cleared - reminder snooze reset", "info", "alarm");
}
```
(Note in a comment that `clearAlarmEvent` now has this side effect beyond the alarm log.)

### 3.4 Unsnooze anchor semantics — APPROVED 2026-08-18
On **unsnooze**: `reminderSnoozeEpoch = 0` **and** `lastSmsAlertEpoch = now`.
Without the second assignment the overdue reminder would fire within ~60 s of clicking Unsnooze,
texting everyone something they already know. Setting the anchor to "now" means the next reminder
arrives one full `smsReminderHours` later — which is the actual unsnooze use case ("tell me if it
*doesn't* recover"). This matches existing anchor semantics (reminders already advance the anchor
without a send-success check). Alternative (fire immediately) noted and rejected.

On **snooze**: `reminderSnoozeEpoch = now`. `lastSmsAlertEpoch` untouched.

### 3.5 Persistence (survives reboot)
- `saveSensorRegistry`: alongside the `se`/`sa` block — `if (rec.reminderSnoozeEpoch > 0.0) obj["sz"] = rec.reminderSnoozeEpoch;`
- `loadSensorRegistry` fresh-record path: `rec.reminderSnoozeEpoch = obj["sz"] | 0.0;`
  (matches `se`/`sa`, which are also only restored on the fresh path, not the dup-merge overwrite branch — keep consistent.)
- **Eager save**: snooze/unsnooze is rare and operator-triggered — call `saveSensorRegistry()`
  immediately from the API/SMS handlers instead of waiting for the 5-min dirty sweep
  (S-4 lesson from the 07-21 review: reboot inside the window would otherwise resurrect a
  reminder the operator just silenced).

### 3.6 API — `POST /api/alarm/snooze`
Body `{"client":"dev:...","sensor":1,"action":"snooze"|"unsnooze"}`, session-auth via
`requireValidPin` (same as `/api/sms/optout`), 256 B body cap, route added to the ~L9465 cluster.

Handler `handleAlarmSnoozePost`:
- Validate action; `findSensorByHash(client, sensor)`; 404 if no record.
- `snooze` requires `rec->alarmActive` (400 "no active alarm" otherwise); `unsnooze` is
  always accepted (no-op success if not snoozed).
- Apply §3.4 field changes, `gSensorRegistryDirty = true`, eager `saveSensorRegistry()`.
- `logTransmission(uid, site, "alarm", "snooze"/"unsnooze", detail)` + `addServerSerialLog`.
- Respond `{"success":true,"snoozed":true|false}`.

### 3.7 `/api/clients` contract
In the per-sensor serializer (~L10516), after `sensorObj["at"]`:
```cpp
if (rec.reminderSnoozeEpoch > 0.0) sensorObj["sz"] = rec.reminderSnoozeEpoch;
```

### 3.8 Dashboard UI (DASHBOARD_HTML — minified PROGMEM, single line)
- `buildSiteModel` (L2307): map `sz` → `snoozeEpoch` on the sensor object (verify the exact
  per-sensor key names in that minified mapper before editing — `renderDataCard` reads
  `t.sensorIdx` in one place and `t.sensorIndex` in another, so anchor on the real text).
- `renderDataCard` (L2314), `t.alarm` branch:
  - Banner: `ALARM: high` becomes `ALARM: high — reminders snoozed <timeAgo>` when `t.snoozeEpoch>0` (reuse existing `timeAgo`).
  - `dc-actions` row (next to the existing Clear Relay button): when alarmed and not snoozed →
    `<button class="secondary btn-small" onclick="snoozeAlarm(uid,k,'snooze')">Snooze Reminders</button>`;
    when snoozed → `Unsnooze` button. Reuse existing button classes — **no new CSS** needed.
- New page-scoped JS `snoozeAlarm(uid,k,action)`: `fetch('/api/alarm/snooze',{method:'POST',body:JSON.stringify({client:uid,sensor:k,action})})` then `loadData()` refresh + `showToast`-style feedback if that helper exists on this page (verify — per-page JS scope lesson from v2.2.6).

Out of scope (state explicitly): viewer dashboard badge/buttons, site-config page badge,
settings-page `smsReminderHours` input (still API-only). Any can be a follow-up.

### 3.9 Snooze/unsnooze broadcast to subscribed recipients
When reminders are silenced for everyone, silence becomes ambiguous — recipients could read it
as "fixed." So every **state change** broadcasts once to the **same audience the reminders go
to**, reusing the reminder engine's own dispatch (association scoping, opt-out suppression,
dedupe, and the 10-recipient cap all come for free):

```cpp
sendSmsAlert(message, alarmId);                                 // alarmId = "<uid>_<sensorIndex>"
sendEmailAlert("TankAlarm Reminders Snoozed", message, alarmId); // or "... Resumed"
```

Message composer (mirror the reminder composer's `char message[160]` + short-site pattern):
- Snooze: `"SNOOZED: <site> #N reminders paused (<type> alarm, <val> <unit>). Auto-resumes on recovery. Reply UNSNOOZE to resume now."`
- Unsnooze: `"RESUMED: <site> #N reminders active again (<type> alarm, <val> <unit>)."`
- Attribution when cheap: dashboard → `"via dashboard"`; SMS keyword → contact *name* when the
  from-number matches a contact, else `"via SMS reply"` (never broadcast the raw phone number).
- Drop the "Reply UNSNOOZE" sentence if Phase 2 is deferred to a later PR.

Rules:
- **State-change gated**: no broadcast (and no re-save) when snooze/unsnooze is a no-op —
  repeated button clicks or duplicate SMS keywords cannot spam the list.
- Fire-and-forget like reminders: send once, do not retry-spin, log via `logTransmission`.
- Does **not** consume the per-sensor alarm rate-limit bucket and does **not** touch
  `lastSmsAlertEpoch` (`checkSmsRateLimit` is not in this path — same as the reminder engine;
  the unsnooze anchor write in §3.4 is separate and deliberate).
- Net message math at defaults: one notice per recipient replaces up to 4 reminders/day × recipients.
- No config toggle for now (YAGNI) — if it ever proves noisy, a `notifyOnSnooze` flag can be
  added to `ServerConfig` without schema breakage.

---

## 4. Phase 2 — SMS reply keywords SNOOZE / UNSNOOZE

**Good news:** inbound SMS replies already exist — v2.2.3 shipped `sms_inbound.qi` +
`handleSmsInbound` (STOP/START/HELP). Twilio's Advanced Opt-Out intercepts STOP/START/HELP
upstream, but **SNOOZE/UNSNOOZE pass through** the operator's Twilio Function → Notehub →
`sms_inbound.qi`. So this is ~40 lines in an existing handler, **no new route**. It only works in
the field once the Twilio Function from `/sms-setup` is deployed — if that Function isn't set up
yet, the code ships inert.
**APPROVED 2026-08-18: Phase 2 ships in the same PR.** Testable by injecting a note into
`sms_inbound.qi` from the Notehub console — no Twilio leg required.

In `handleSmsInbound` keyword chain, before the fallthrough log branch:
- **Sender gating (APPROVED 2026-08-18): commands are honored only from numbers the alarm
  actually texts.** For each active alarm record, resolve its eligible SMS recipient set exactly
  the way `sendSmsAlert(msg, alarmId)` does (enrolled in `smsAlertRecipients`, not opted out,
  `alarmAssociations` empty-or-contains-`alarmId`, matched via `samePhoneNumber`). The keyword
  applies to **every active alarm whose recipient set contains the from-number** — statelessly
  recomputed, so it stays consistent with dispatch even if contacts changed since the alarm fired.
- `SNOOZE` → for each such record with `alarmActive && reminderSnoozeEpoch==0`:
  - Set epochs per §3.4, count affected, eager-save once if count > 0.
  - Recipients (including the sender) learn of the change via the §3.9 broadcast — one per
    affected alarm. The sender-only `sendSingleSms(from, ...)` reply (pattern proven by START
    re-welcome) is kept **only for the no-op/error cases** the broadcast can't cover:
    `"No active alarms to snooze."` / `"Nothing is snoozed."`
- `UNSNOOZE` → same scoping, applies §3.4 unsnooze to snoozed records; recipients learn via the
  §3.9 "resumed" broadcast.
- **Rejection paths:**
  - From-number matches a contact but no active alarm's recipient set →
    `sendSingleSms(from, "No active alarms are associated with this number.")` + log.
  - From-number matches no contact at all → **log only, no reply** (don't spend SMS on
    strangers; the existing unknown-keyword branch already logs inbound traffic).
- `logTransmission("", "", "sms", "snooze"/"unsnooze", "SNOOZE from +1... - N alarms")`.

Docs to touch in this phase: `/sms-setup` page keyword table (it documents the automatic
inbound behaviors) and optionally the welcome SMS text; `Tutorials/NOTEHUB_ROUTES_SETUP.md`
needs **no** change (no new notefile/route).

---

## 5. Edge cases / decisions on record

1. **Alarm type morphs without clearing** (high → low on the same record, no clear note between):
   `alarmActive` stays true → snooze persists. Accepted: same episode.
2. **Client re-sends a new alarm note while snoozed:** `handleAlarm` still sends the normal alarm
   SMS (subject to existing rate limits). Snooze gates *only* `checkAlarmReminders`. Intentional —
   a fresh escalation is new information. (Client alarms are edge-triggered, so this is rare.)
3. **`smsReminderHours == 0`** (reminders globally off): buttons still render on alarm cards and
   work; they simply have nothing to suppress. Harmless; not worth hiding.
4. **Reboot:** snooze persists via registry (`sz`) + eager save. Reminder suppression therefore
   survives the known 5-min registry save window.
5. **Unknown sensor / cleared alarm on snooze request:** 404 / 400 per §3.6 — no silent success.
6. **SMS snooze from a number not in contacts:** **rejected** (log-only, no reply, no state
   change) — APPROVED 2026-08-18. Only numbers in an active alarm's resolved recipient set can
   snooze/unsnooze it (§4). A contact who is enrolled but not a recipient of any active alarm
   gets a polite "no active alarms" reply instead.
7. **Multiple recipients, one snoozes:** snooze is per-*sensor*, not per-recipient — one SNOOZE
   silences reminders for everyone. This matches the dashboard button semantics and the user's
   scenario (whole team knows the fix is days out). Per-recipient muting = different feature
   (opt-out already exists). The §3.9 broadcast is what makes this safe: everyone is told
   reminders were paused, and (with Phase 2) any recipient can reply UNSNOOZE to overrule.
8. **Repeated snooze clicks / duplicate SMS keywords:** state-change gate in the handlers means
   no re-broadcast, no extra registry save — idempotent.
9. **SMS SNOOZE targeting multiple alarms:** one §3.9 broadcast per affected alarm (each has its
   own recipient set via `alarmId`). Bounded by active-alarm count; typical case is 1.

---

## 6. Build / test / release checklist

1. `git status --porcelain` + `git diff --stat` first (working-tree stomp check), `git fetch` and
   confirm HEAD matches origin/master before editing (stale-checkout lesson).
2. Implement Phase 1 → compile:
   `arduino-cli compile --fqbn arduino:mbed_opta:opta --libraries c:/GitHub/ArduinoSMSTankAlarm/TankAlarm-112025-Common --output-dir build/server TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino`
3. Dashboard JS edits: **one `replace_string_in_file` at a time** with disk verification between
   edits (PROGMEM stomp hazard, 2026-07-20 lesson); validate script with the browser
   `new Function(...)`/`addScriptTag` technique (regex-literal false positives in `check_html_js.ps1`).
4. Flash COM3 (expect the documented possible second flash for Ethernet relink).
5. **Live verification is free right now** — the field alarm that motivated this is active:
   - Dashboard shows Snooze on the alarming card → click → badge + `sz` in `/api/clients`.
   - §3.9 broadcast arrives at the subscribed phones/inboxes exactly once; a second click
     produces no second message.
   - Transmission log shows the snooze entry; next reminder window passes with **no** SMS/email.
   - Unsnooze → confirm next reminder lands one interval later (optionally set
     `smsReminderHours=1` via `POST /api/server-settings` to shorten the wait, then restore).
   - Optional reboot persistence check (weigh Ethernet-relink risk).
   - Phase 2: inject `{"from":"+1...","body":"SNOOZE"}` into `sms_inbound.qi` from the Notehub
     console to test without the Twilio leg.
6. Version bump 2.2.13 (Common.h `FIRMWARE_VERSION` + `FIRMWARE_BUILD_SEQ` + `library.properties`),
   `release-notes/v2.2.13.md`, DASHBOARD_GUIDE snooze paragraph.
7. Commit → `git pull --rebase` (bot pushes) → push → tag `v2.2.13` only when releasing (CI
   validates tag == Common version).

## 7. Decisions (all approved 2026-08-18)

- **Indefinite snooze** — holds until the alarm condition exits the alarm bounds (auto-reset on
  clear) or manual unsnooze. No duration picker. (§1)
- **Unsnooze defers the next reminder one full interval** (`lastSmsAlertEpoch = now`). (§3.4)
- **Phase 2 (SMS SNOOZE/UNSNOOZE keywords) ships in the same PR.** (§4)
- **SMS commands are recipient-gated**: honored only from numbers in an active alarm's resolved
  SMS recipient set; enrolled-but-unmatched contacts get a "no active alarms" reply; unknown
  numbers are logged and ignored. (§4, §5.6)
- **Snooze/unsnooze broadcast** a one-time notice to the alarm's subscribed recipients (§3.9) —
  silence must never be mistakable for "fixed." On by default, no config knob.
