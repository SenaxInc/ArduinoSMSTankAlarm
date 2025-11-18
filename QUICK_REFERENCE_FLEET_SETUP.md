# Quick Reference: Fleet-Based vs Route-Based Setup

## At a Glance

| Aspect | Route-Based (OLD) | Fleet-Based (NEW) ✅ |
|--------|------------------|---------------------|
| **Notehub Setup** | Create 4-5 manual routes | Create 2 fleets, assign devices |
| **Setup Time** | ~20 minutes | ~5 minutes |
| **Client Config Field** | `serverRoute` | `serverFleet` |
| **Client Sends To** | Local `.qo` file | `fleet.<name>:<file>.qi` |
| **Server Sends To** | Local `.qo` file + device param | `device:<uid>:<file>.qi` |
| **Scaling** | Create route for each flow | Just assign new devices to fleet |
| **Maintenance** | Update routes when changing flows | No maintenance needed |

## Communication Cheat Sheet

### Client Notefiles
```cpp
// Telemetry
"fleet.tankalarm-server:telemetry.qi"

// Alarms  
"fleet.tankalarm-server:alarm.qi"

// Daily Reports
"fleet.tankalarm-server:daily.qi"
```

### Server Notefiles
```cpp
// Config to specific client
"device:<client-uid>:config.qi"

// Config to all clients (broadcast)
"fleet.tankalarm-clients:config.qi"
```

## Setup Commands

### Notehub Fleet Creation
1. **Create Server Fleet:**
   - Name: `tankalarm-server`
   - Assign: 1 server Notecard

2. **Create Client Fleet:**
   - Name: `tankalarm-clients`  
   - Assign: All client Notecards

### Configuration Fields

**Client (`serverFleet`):**
```json
{
  "serverFleet": "tankalarm-server"
}
```

**Server (`clientFleet`):**
```json
{
  "clientFleet": "tankalarm-clients"
}
```

## Common Issues & Fixes

| Problem | Cause | Solution |
|---------|-------|----------|
| No telemetry on server | Wrong `serverFleet` value | Update client config via server UI |
| Config not reaching client | Wrong device UID | Copy UID from Notehub device page |
| Client not in dropdown | Not in fleet | Assign client to `tankalarm-clients` fleet |
| Notes not syncing | Notecard offline | Check `card.wireless` status |

## Migration Checklist

- [ ] Create fleets in Notehub
- [ ] Assign devices to fleets  
- [ ] Upload new client firmware
- [ ] Upload new server firmware
- [ ] Update client configs (change `serverFleet`)
- [ ] Test telemetry flow
- [ ] Test config updates
- [ ] Test alarm flow
- [ ] Disable old routes (optional)

## Key Code Changes

### Client: publishNote()
```cpp
// OLD
JAddStringToObject(req, "file", fileName);  // e.g., "telemetry.qo"

// NEW  
char targetFile[80];
snprintf(targetFile, sizeof(targetFile), "fleet.%s:%s", 
         gConfig.serverFleet, fileName);
JAddStringToObject(req, "file", targetFile);
// Result: "fleet.tankalarm-server:telemetry.qi"
```

### Server: dispatchClientConfig()
```cpp
// OLD
JAddStringToObject(req, "file", CONFIG_OUTBOX_FILE);  // "config.qo"
JAddStringToObject(req, "device", clientUid);

// NEW
char targetFile[80];
snprintf(targetFile, sizeof(targetFile), "device:%s:config.qi", clientUid);
JAddStringToObject(req, "file", targetFile);
// Result: "device:dev:1234...abcd:config.qi"
```

## Documentation Map

```
📁 Documentation Structure
├── FLEET_SETUP.md              ← Start here for new deployments
├── DEVICE_TO_DEVICE_API.md     ← Technical details & API docs  
├── MIGRATION_GUIDE.md          ← Existing system migration
├── FLEET_IMPLEMENTATION_SUMMARY.md  ← Complete change summary
└── SETUP.md (deprecated)       ← Old route-based method
```

## Quick Troubleshooting

**Enable Debug Output:**
```cpp
notecard.setDebugOutputStream(Serial);
```

**Check Notecard Status:**
```json
{"req":"card.wireless"}
{"req":"hub.status"}
{"req":"hub.sync"}
```

**View Notehub Events:**
1. Log in to https://notehub.io
2. Select project
3. Click **Events**
4. Filter by device UID
5. Look for note traffic

**Verify Fleet Assignment:**
1. Notehub → **Devices**
2. Click on device
3. Check **Fleets** section
4. Should show assigned fleet name

## Additional Resources

- **Blues Dev Portal:** https://dev.blues.io
- **Fleet API Docs:** https://dev.blues.io/reference/notehub-api/fleet-api/
- **Arduino Opta Docs:** https://docs.arduino.cc/hardware/opta
- **Notecard API Reference:** https://dev.blues.io/api-reference/notecard-api/

## Support

**Serial Console Checks:**
```
✓ "Notecard UID: dev:..." - Device identified
✓ "Server setup complete" - Server ready
✓ "Received config update" - Client got config
✓ "Publishing telemetry" - Client sending data
```

**Notehub Event Checks:**
```
✓ Client events show "fleet.tankalarm-server:*.qi"
✓ Server events show incoming "*.qi" notes  
✓ Server events show "device:<uid>:config.qi" outbound
✓ Recent timestamps on all events
```

---

**Last Updated:** November 2025  
**Version:** 112025 Opta Release
