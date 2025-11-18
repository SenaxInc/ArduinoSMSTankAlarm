# Device-to-Device Communication Without Manual Routes

## Overview

This implementation eliminates the need for manually configured routes in Blues Notehub by using **fleet-based note targeting** and the Notecard's built-in device-to-device communication capabilities.

## How It Works

### Traditional Route-Based Approach (OLD)
1. Client sends note to `telemetry.qo` 
2. Notehub route manually configured to forward `telemetry.qo` → server device `telemetry.qi`
3. Server polls `telemetry.qi` for incoming data
4. **Problem**: Requires manual route setup in Notehub web interface for each note file type

### Fleet-Based API Approach (NEW)
1. Client and server Notecards are organized into **Fleets** in Notehub
2. Client sends notes to **fleet-scoped notefile** using special syntax
3. Notes automatically delivered to all devices in target fleet
4. **Benefit**: No manual route configuration needed - just fleet membership

## Implementation Strategy

### Method 1: Fleet-to-Fleet Communication (Recommended)

Blues Notecard supports sending notes to a fleet using the format: `<fleet>:<notefile>`.

**Client sends to server fleet:**
```cpp
// Instead of:
// note.add to "telemetry.qo" with route configuration

// Use fleet targeting:
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "fleet.server:telemetry.qi");
JAddItemToObject(req, "body", body);
notecard.sendRequest(req);
```

**Server receives from any fleet member:**
```cpp
// Server just polls local notefile - Notehub delivers automatically
J *req = notecard.newRequest("note.get");
JAddStringToObject(req, "file", "telemetry.qi");
JAddBoolToObject(req, "delete", true);
J *rsp = notecard.requestAndResponse(req);
```

### Method 2: Device-Specific Targeting

For server-to-client config updates, target specific device UID:

```cpp
// Server sends config to specific client device
J *req = notecard.newRequest("note.add");
char targetFile[80];
snprintf(targetFile, sizeof(targetFile), "device:%s:config.qi", clientDeviceUID);
JAddStringToObject(req, "file", targetFile);
JAddItemToObject(req, "body", configBody);
notecard.sendRequest(req);
```

### Method 3: Project-Wide Notefiles

Use project-level notefiles that all devices can access:

```cpp
// Client publishes to project-level notefile
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "project:telemetry");
// Add device UID in body so server knows source
J *body = JCreateObject();
JAddStringToObject(body, "device", gDeviceUID);
// ... add other data ...
JAddItemToObject(req, "body", body);
notecard.sendRequest(req);
```

## Setup Requirements

### In Blues Notehub:

1. **Create Fleets:**
   - Fleet: `tankalarm-clients` (for all field devices)
   - Fleet: `tankalarm-server` (for base station)

2. **Assign Devices to Fleets:**
   - Navigate to Devices
   - Select each client Notecard → assign to `tankalarm-clients` fleet
   - Select server Notecard → assign to `tankalarm-server` fleet

3. **Done!** No route configuration needed.

### In Device Configuration:

**Client devices:**
- Store the server fleet name (e.g., "tankalarm-server") in configuration
- Use `fleet.tankalarm-server:<filename>` syntax when publishing

**Server device:**
- Poll local notefiles (`.qi` files) - Notehub automatically delivers
- Store client fleet name for broadcasting config updates

## Notefile Naming Convention

| Communication | Old Route-Based | New Fleet-Based |
|---------------|-----------------|-----------------|
| Client → Server telemetry | `telemetry.qo` → route → server `telemetry.qi` | `fleet.tankalarm-server:telemetry.qi` |
| Client → Server alarm | `alarm.qo` → route → server `alarm.qi` | `fleet.tankalarm-server:alarm.qi` |
| Client → Server daily | `daily.qo` → route → server `daily.qi` | `fleet.tankalarm-server:daily.qi` |
| Server → Client config | `config.qo` → route → client `config.qi` | `device:<uid>:config.qi` or `fleet.tankalarm-clients:config.qi` |

## Benefits

1. **Zero Route Configuration**: No manual route setup in Notehub web interface
2. **Easier Deployment**: Just assign devices to fleets
3. **Scalable**: Add new clients by assigning them to client fleet
4. **Flexible**: Can target specific devices or broadcast to entire fleet
5. **Resilient**: Fleet membership managed in Notehub, survives device replacement

## Configuration Changes Needed

### Client Configuration (config.json)
```json
{
  "deviceLabel": "Tank01",
  "serverFleet": "tankalarm-server",
  // Remove: "serverRoute": "default-route"
  ...
}
```

### Server Configuration (server_config.json)
```json
{
  "serverLabel": "TankAlarmServer",
  "clientFleet": "tankalarm-clients",
  ...
}
```

## Migration Path

1. Create fleets in Notehub
2. Assign devices to appropriate fleets
3. Update client and server firmware with new fleet-based code
4. Deploy updated firmware
5. Verify communication works
6. Delete old routes from Notehub (optional, for cleanup)

## Troubleshooting

**Notes not arriving at server:**
- Verify server Notecard is in `tankalarm-server` fleet
- Check client configuration has correct `serverFleet` name
- Use Notehub Events view to see if notes are being sent
- Confirm continuous mode enabled on both devices

**Config updates not reaching clients:**
- Verify clients are in correct fleet
- Check server is using correct fleet name or device UID
- For device-specific targeting, ensure UID is correct (use `card.uuid` response)

**General debugging:**
- Enable Notecard debug output: `notecard.setDebugOutputStream(Serial);`
- Check Notehub Events for each device to see note traffic
- Verify both devices have recent sync timestamps

## Hardware Requirements

- **Client**: Arduino Opta Lite + Blues Wireless for Opta adapter + analog extension (unchanged)
- **Server**: Arduino Opta Lite + Blues Wireless for Opta adapter (unchanged)

No additional hardware needed - this is purely a software/configuration change.

## References

- [Blues Wireless Fleet Documentation](https://dev.blues.io/reference/notehub-api/fleet-api/)
- [Notecard note.add API](https://dev.blues.io/api-reference/notecard-api/note-requests/#note-add)
- [Device-to-Device Communication](https://dev.blues.io/guides-and-tutorials/routing-data-to-cloud/device-to-device/)
