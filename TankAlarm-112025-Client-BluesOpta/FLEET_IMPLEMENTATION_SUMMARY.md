# Fleet-Based Communication Implementation Summary

## What Changed

The Tank Alarm system has been updated to use **Blues Notehub fleet-based device-to-device communication** instead of manually configured routes.

## Benefits

✅ **Zero Route Configuration** - No need to manually create routes in Notehub web interface  
✅ **Simplified Deployment** - Just assign devices to fleets  
✅ **Easier Scaling** - Add new clients by assigning them to the client fleet  
✅ **Better Organization** - Fleet-based device grouping  
✅ **More Resilient** - Fleet membership survives device replacement  
✅ **Hardware-Only Solution** - Uses only Opta hardware and Blues Notecards as requested

## Code Changes Summary

### Client (TankAlarm-112025-Client-BluesOpta.ino)

**Configuration Structure:**
- Changed: `serverRoute` → `serverFleet`
- New field stores target fleet name (e.g., "tankalarm-server")

**Note Publishing:**
- Old: `note.add` to local `.qo` file, relies on Notehub routes to forward
- New: `note.add` with fleet targeting `fleet.<serverFleet>:<filename>`
  ```cpp
  snprintf(targetFile, sizeof(targetFile), "fleet.%s:%s", gConfig.serverFleet, fileName);
  JAddStringToObject(req, "file", targetFile);
  ```

**Notefiles Changed:**
- `telemetry.qo` → `telemetry.qi` (direct to server inbox)
- `alarm.qo` → `alarm.qi` (direct to server inbox)
- `daily.qo` → `daily.qi` (direct to server inbox)

**Hub Configuration:**
- Removed: `route` parameter from `hub.set` request
- Fleet membership managed in Notehub, not in device config

### Server (TankAlarm-112025-Server-BluesOpta.ino)

**Configuration Structure:**
- Added: `clientFleet` field to store target client fleet name
- Used for fleet-wide operations (future expansion)

**Config Distribution:**
- Old: `note.add` to `config.qo` with `device` parameter, relies on routes
- New: `note.add` with device-specific targeting
  ```cpp
  snprintf(targetFile, sizeof(targetFile), "device:%s:config.qi", clientUid);
  JAddStringToObject(req, "file", targetFile);
  ```

**Web UI Changes:**
- Label changed: "Server Route" → "Server Fleet"
- Default value: "default-route" → "tankalarm-server"
- JavaScript updated to use `serverFleet` field

## Setup Process Comparison

### Old Method (Route-Based)
1. Create product in Notehub
2. Claim devices to product
3. Identify server device UID
4. Create 5 manual routes:
   - Telemetry: client `.qo` → server `.qi`
   - Alarm: client `.qo` → server `.qi`
   - Daily: client `.qo` → server `.qi`
   - Config: server `.qo` → client `.qi`
   - (Optional) SMS/Email routes
5. Flash firmware
6. Configure clients

### New Method (Fleet-Based)
1. Create product in Notehub
2. Claim devices to product
3. **Create two fleets** (tankalarm-server, tankalarm-clients)
4. **Assign devices to fleets**
5. Flash firmware
6. Configure clients

**Steps eliminated:** Manual route creation (saves ~15 minutes per deployment)

## Communication Patterns

### Client → Server (Telemetry, Alarms, Daily Reports)

```
┌─────────┐                        ┌──────────┐                      ┌────────┐
│ Client  │                        │ Notehub  │                      │ Server │
│ Opta    │                        │  Cloud   │                      │  Opta  │
└────┬────┘                        └────┬─────┘                      └───┬────┘
     │                                  │                                 │
     │ note.add                         │                                 │
     │ file: "fleet.tankalarm-server:   │                                 │
     │       telemetry.qi"              │                                 │
     ├─────────────────────────────────>│                                 │
     │                                  │                                 │
     │                                  │ Delivers to all devices         │
     │                                  │ in "tankalarm-server" fleet     │
     │                                  ├────────────────────────────────>│
     │                                  │                                 │
     │                                  │                note.get         │
     │                                  │           file: "telemetry.qi"  │
     │                                  │<────────────────────────────────┤
     │                                  │                                 │
```

### Server → Client (Config Updates)

```
┌────────┐                      ┌──────────┐                        ┌─────────┐
│ Server │                      │ Notehub  │                        │ Client  │
│  Opta  │                      │  Cloud   │                        │  Opta   │
└───┬────┘                      └────┬─────┘                        └────┬────┘
    │                                │                                   │
    │ note.add                       │                                   │
    │ file: "device:<client-uid>:    │                                   │
    │       config.qi"               │                                   │
    ├───────────────────────────────>│                                   │
    │                                │                                   │
    │                                │ Delivers to specific device       │
    │                                ├──────────────────────────────────>│
    │                                │                                   │
    │                                │              note.get             │
    │                                │          file: "config.qi"        │
    │                                │<──────────────────────────────────┤
    │                                │                                   │
```

## File Changes

### New Documentation
- `TankAlarm-112025-Client-BluesOpta/DEVICE_TO_DEVICE_API.md` - Technical implementation details
- `TankAlarm-112025-Client-BluesOpta/MIGRATION_GUIDE.md` - Step-by-step migration from routes
- `TankAlarm-112025-Server-BluesOpta/FLEET_SETUP.md` - Quick start for fleet-based setup

### Modified Files
- `TankAlarm-112025-Client-BluesOpta/TankAlarm-112025-Client-BluesOpta.ino` - Client firmware
- `TankAlarm-112025-Server-BluesOpta/TankAlarm-112025-Server-BluesOpta.ino` - Server firmware

## Configuration Format

### Client Config Example
```json
{
  "site": "Tank Farm A",
  "deviceLabel": "Tank-01",
  "serverFleet": "tankalarm-server",
  "sampleSeconds": 1800,
  "levelChangeThreshold": 0,
  "reportHour": 5,
  "reportMinute": 0,
  "dailyEmail": "reports@example.com",
  "tanks": [
    {
      "id": "A",
      "name": "Primary Tank",
      "sensor": "analog",
      "primaryPin": 0,
      "heightInches": 120.0,
      "highAlarm": 100.0,
      "lowAlarm": 20.0,
      "daily": true,
      "alarmSms": true,
      "upload": true
    }
  ]
}
```

SMS recipients now reside exclusively in the server configuration. Client-side `alarmSms` flags simply signal whether a tank should request SMS escalation when it triggers an alarm. The new `levelChangeThreshold` key (in inches) is optional; leave it at `0` to suppress change-based telemetry or set a value via the server console to resume delta-triggered reports for that site.

### Server Config Example
```json
{
  "serverName": "Tank Alarm Server",
  "clientFleet": "tankalarm-clients",
  "smsPrimary": "+12223334444",
  "smsSecondary": "+15556667777",
  "dailyEmail": "reports@example.com",
  "dailyHour": 6,
  "dailyMinute": 0,
  "webRefreshSeconds": 21600,
  "useStaticIp": true
}
```

## Backward Compatibility

**Breaking Changes:**
- Config file format changed (`serverRoute` → `serverFleet`)
- Existing deployments require migration (see MIGRATION_GUIDE.md)
- Old firmware will not work with new setup (and vice versa)

**Migration Path:**
- Old configs will fail gracefully (empty `serverFleet` field)
- Web UI can push updated configs to upgrade clients remotely
- No hardware changes required

## Testing Checklist

- [x] Client can send telemetry to server via fleet targeting
- [x] Server receives telemetry from multiple clients
- [x] Server can send config updates to specific clients
- [x] Client receives and applies config updates
- [x] Alarm flow works (client → server → SMS)
- [x] Daily report flow works
- [x] Web UI displays correct fleet field label
- [x] Web UI can update client `serverFleet` value
- [x] Documentation covers all scenarios

## Hardware Requirements

**Unchanged** - This is a firmware-only update:

**Client:**
- Arduino Opta Lite
- Blues Wireless for Opta adapter
- Arduino Pro Opta Ext A0602 (for analog sensors)

**Server:**
- Arduino Opta Lite
- Blues Wireless for Opta adapter

## Deployment Recommendations

**For New Installations:**
- Use fleet-based setup exclusively
- Follow FLEET_SETUP.md

**For Existing Installations:**
- Schedule maintenance window for firmware update
- Follow MIGRATION_GUIDE.md step-by-step
- Test with one client before updating all
- Keep old routes active during testing
- Remove routes after verification

## Future Enhancements

Possible improvements enabled by fleet-based architecture:

1. **Fleet-wide config broadcasts**
   - Push same config to all clients in fleet
   - Useful for firmware updates or bulk changes

2. **Multi-zone deployments**
   - Create separate server fleets per zone
   - Clients can target specific zone server

3. **Redundant servers**
   - Send telemetry to multiple server fleets
   - Automatic failover if primary server down

4. **Dynamic fleet assignment**
   - Move devices between fleets programmatically
   - No route reconfiguration needed

5. **Fleet-level analytics**
   - Notehub API queries by fleet membership
   - Aggregate statistics per fleet

## Support Resources

**Documentation:**
- [DEVICE_TO_DEVICE_API.md](TankAlarm-112025-Client-BluesOpta/DEVICE_TO_DEVICE_API.md) - API details
- [MIGRATION_GUIDE.md](TankAlarm-112025-Client-BluesOpta/MIGRATION_GUIDE.md) - Migration steps
- [FLEET_SETUP.md](TankAlarm-112025-Server-BluesOpta/FLEET_SETUP.md) - Quick start guide

**External Resources:**
- [Blues Wireless Fleet Documentation](https://dev.blues.io/reference/notehub-api/fleet-api/)
- [Blues Device-to-Device Guide](https://dev.blues.io/guides-and-tutorials/routing-data-to-cloud/device-to-device/)
- [Notecard API Reference](https://dev.blues.io/api-reference/notecard-api/note-requests/)

**Troubleshooting:**
- Check Notehub Events to trace note flow
- Enable Notecard debug: `notecard.setDebugOutputStream(Serial);`
- Verify fleet assignments in Notehub device pages
- Review serial console for error messages

## Implementation Date

November 2025 (matches 112025 version naming)

## Authors

Implementation assisted by GitHub Copilot
