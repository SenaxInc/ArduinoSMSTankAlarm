# Device-to-Device Communication Without Manual Routes

## Overview

This implementation eliminates the need for manually configured routes in Blues Notehub by using **fleet-based note targeting** and the Notecard's built-in device-to-device communication capabilities.

## How It Works

### Traditional Route-Based Approach (OLD)
1. Client sends note to `telemetry.qo` 
2. Notehub route manually configured to forward `telemetry.qo` → server device `telemetry.qi`
3. Server polls `telemetry.qi` for incoming data
4. **Problem**: Requires manual route setup in Notehub web interface for each note file type

### Route Relay Approach (NEW)
1. Client and server Notecards are organized into the same project in Notehub
2. Client sends notes to standard `.qo` outbox notefiles
3. Notehub Routes (ClientToServerRelay, ServerToClientRelay) deliver notes to the correct device
4. **Benefit**: No colon-based notefile syntax — uses standard `.qo` files with route-based delivery

## Implementation Strategy

### Method 1: Client-to-Server via Route Relay (Recommended)

Client sends notes to standard `.qo` outbox files. The ClientToServerRelay route in Notehub delivers them as `.qi` inbox files on the server.

**Client sends telemetry:**
```cpp
// Client sends to standard .qo outbox file
// ClientToServerRelay route delivers as telemetry.qi on server
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "telemetry.qo");
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

### Method 2: Server-to-Client via Consolidated Command Outbox

For server-to-client updates (config, relay commands), the server sends to a single `command.qo` outbox with `_target` (device UID) and `_type` (command type) in the body. The ServerToClientRelay route in Notehub reads these fields and delivers the note to the correct `.qi` file on the target client.

```cpp
// Server sends config to specific client device via command.qo
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "command.qo");
JAddStringToObject(configBody, "_target", clientDeviceUID);
JAddStringToObject(configBody, "_type", "config");
JAddItemToObject(req, "body", configBody);
notecard.sendRequest(req);
```

### Method 3: Device Identification in Note Body

Include the device UID in the note body so the server knows the source:

```cpp
// Client publishes to standard .qo outbox with device UID in body
J *req = notecard.newRequest("note.add");
JAddStringToObject(req, "file", "telemetry.qo");
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
- Send notes to standard `.qo` outbox notefiles (e.g., `telemetry.qo`)
- ClientToServerRelay route in Notehub delivers them to the server as `.qi` files

**Server device:**
- Poll local notefiles (`.qi` files) - Notehub routes deliver automatically
- Send commands via `command.qo` with `_target` and `_type` in body

## Notefile Naming Convention

| Communication | Old Route-Based | New Route Relay |
|---------------|-----------------|------------------|
| Client → Server telemetry | `telemetry.qo` → route → server `telemetry.qi` | `telemetry.qo` → ClientToServerRelay → server `telemetry.qi` |
| Client → Server alarm | `alarm.qo` → route → server `alarm.qi` | `alarm.qo` → ClientToServerRelay → server `alarm.qi` |
| Client → Server daily | `daily.qo` → route → server `daily.qi` | `daily.qo` → ClientToServerRelay → server `daily.qi` |
| Server → Client config | `config.qo` → route → client `config.qi` | `command.qo` (with `_target`/`_type`) → ServerToClientRelay → client `config.qi` |

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
