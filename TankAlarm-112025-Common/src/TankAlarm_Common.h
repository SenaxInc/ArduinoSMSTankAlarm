/**
 * TankAlarm_Common.h
 * 
 * Common constants and configuration for TankAlarm 112025 components.
 * Shared by Server, Client, and Viewer.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_COMMON_H
#define TANKALARM_COMMON_H

// ============================================================================
// Firmware Version
// ============================================================================
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.1.0"
#endif

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__
#endif

#ifndef FIRMWARE_BUILD_TIME
#define FIRMWARE_BUILD_TIME __TIME__
#endif

// ============================================================================
// Notecard Configuration
// ============================================================================
#ifndef NOTECARD_I2C_ADDRESS
#define NOTECARD_I2C_ADDRESS 0x17
#endif

#ifndef NOTECARD_I2C_FREQUENCY
#define NOTECARD_I2C_FREQUENCY 400000UL
#endif

// ============================================================================
// Ethernet Configuration
// ============================================================================
#ifndef ETHERNET_PORT
#define ETHERNET_PORT 80
#endif

// ============================================================================
// Tank Record Configuration
// ============================================================================
#ifndef MAX_TANK_RECORDS
#define MAX_TANK_RECORDS 64
#endif

// ============================================================================
// Hardware Configuration
// ============================================================================
#ifndef MAX_RELAYS
#define MAX_RELAYS 4  // Arduino Opta has 4 relay outputs (D0-D3)
#endif

// ============================================================================
// Serial Buffer Configuration
// ============================================================================
#ifndef CLIENT_SERIAL_BUFFER_SIZE
#define CLIENT_SERIAL_BUFFER_SIZE 50  // Buffer up to 50 log messages per client
#endif

// ============================================================================
// Watchdog Configuration
// ============================================================================
#ifndef WATCHDOG_TIMEOUT_SECONDS
#define WATCHDOG_TIMEOUT_SECONDS 30
#endif

// ============================================================================
// Notefile Names (for Notecard communication)
// ============================================================================
// Blues Notecard notefile naming rules:
//   - Outbound (device → Notehub): must end in .qo or .qos
//   - Inbound  (Notehub → device): must end in .qi or .qis
//   - Colons (:) are NEVER allowed in notefile names
//   - Cross-device delivery is done via Notehub Routes (Route Relay pattern)
//
// Each device defines its own perspective:
//   Client: sends .qo  (telemetry.qo, alarm.qo, etc.)
//           reads .qi   (config.qi, relay.qi, etc.)
//   Server: sends .qo  (command.qo, viewer_summary.qo, etc.)
//           reads .qi   (telemetry.qi, alarm.qi, etc.)
//   Viewer: reads .qi   (viewer_summary.qi)
//
// Route Relay wiring (configured in Notehub):
//   ClientToServerRelay: client telemetry.qo → server telemetry.qi
//   ServerToClientRelay: server command.qo   → client config.qi / relay.qi / etc.
//   ServerToViewerRelay: server viewer_summary.qo → viewer viewer_summary.qi
// ============================================================================

// --- Data notefiles: Client outbound (.qo), Server inbound (.qi) ---
#ifndef TELEMETRY_OUTBOX_FILE
#define TELEMETRY_OUTBOX_FILE "telemetry.qo"   // Client sends telemetry
#endif

#ifndef TELEMETRY_INBOX_FILE
#define TELEMETRY_INBOX_FILE "telemetry.qi"     // Server receives telemetry
#endif

#ifndef ALARM_OUTBOX_FILE
#define ALARM_OUTBOX_FILE "alarm.qo"            // Client sends alarm events
#endif

#ifndef ALARM_INBOX_FILE
#define ALARM_INBOX_FILE "alarm.qi"             // Server receives alarm events
#endif

#ifndef DAILY_OUTBOX_FILE
#define DAILY_OUTBOX_FILE "daily.qo"            // Client sends daily reports
#endif

#ifndef DAILY_INBOX_FILE
#define DAILY_INBOX_FILE "daily.qi"             // Server receives daily reports
#endif

#ifndef UNLOAD_OUTBOX_FILE
#define UNLOAD_OUTBOX_FILE "unload.qo"          // Client sends unload events
#endif

#ifndef UNLOAD_INBOX_FILE
#define UNLOAD_INBOX_FILE "unload.qi"           // Server receives unload events
#endif

// --- Config notefiles ---
// Client receives config from server (via command.qo → Route #2 → config.qi)
#ifndef CONFIG_INBOX_FILE
#define CONFIG_INBOX_FILE "config.qi"           // Client receives config from server
#endif

// --- Config acknowledgment notefiles ---
// Client sends ACK after applying config; Notehub Route delivers to server
#ifndef CONFIG_ACK_OUTBOX_FILE
#define CONFIG_ACK_OUTBOX_FILE "config_ack.qo"  // Client sends config ACK
#endif

#ifndef CONFIG_ACK_INBOX_FILE
#define CONFIG_ACK_INBOX_FILE "config_ack.qi"    // Server receives config ACK
#endif

// --- Command notefile: Server outbound (consolidated) ---
// Server sends ALL commands (config, relay, serial_request, location_request)
// via a single command.qo notefile. The body includes:
//   "_target": "<client-device-uid>"  — which client to deliver to
//   "_type":   "config"|"relay"|"serial_request"|"location_request"
// The ServerToClientRelay route in Notehub reads _type and delivers to
// the appropriate .qi notefile on the target client.
#ifndef COMMAND_OUTBOX_FILE
#define COMMAND_OUTBOX_FILE "command.qo"
#endif

// --- Relay forwarding (client-to-server-to-client) ---
// When a client alarm triggers remote relays on another client, the request
// goes through the server: Client → relay_forward.qo → Route #1 → Server
// relay_forward.qi → Server re-issues via command.qo → Route #2 → target client relay.qi
#ifndef RELAY_FORWARD_OUTBOX_FILE
#define RELAY_FORWARD_OUTBOX_FILE "relay_forward.qo"  // Client sends relay forward request
#endif

#ifndef RELAY_FORWARD_INBOX_FILE
#define RELAY_FORWARD_INBOX_FILE "relay_forward.qi"   // Server receives relay forward request
#endif

// --- Relay control ---
#ifndef RELAY_CONTROL_FILE
#define RELAY_CONTROL_FILE "relay.qi"           // Client receives relay commands
#endif

// --- Serial logging ---
#ifndef SERIAL_LOG_OUTBOX_FILE
#define SERIAL_LOG_OUTBOX_FILE "serial_log.qo"  // Client sends serial logs to server
#endif

#ifndef SERIAL_LOG_INBOX_FILE
#define SERIAL_LOG_INBOX_FILE "serial_log.qi"   // Server receives serial logs
#endif

#ifndef SERIAL_REQUEST_FILE
#define SERIAL_REQUEST_FILE "serial_request.qi" // Client receives request for logs
#endif

#ifndef SERIAL_ACK_OUTBOX_FILE
#define SERIAL_ACK_OUTBOX_FILE "serial_ack.qo"  // Client sends serial request ack
#endif

#ifndef SERIAL_ACK_INBOX_FILE
#define SERIAL_ACK_INBOX_FILE "serial_ack.qi"   // Server receives serial ack
#endif

// --- Location ---
#ifndef LOCATION_REQUEST_FILE
#define LOCATION_REQUEST_FILE "location_request.qi"   // Client receives location request
#endif

#ifndef LOCATION_RESPONSE_OUTBOX_FILE
#define LOCATION_RESPONSE_OUTBOX_FILE "location_response.qo"  // Client sends location
#endif

#ifndef LOCATION_RESPONSE_INBOX_FILE
#define LOCATION_RESPONSE_INBOX_FILE "location_response.qi"   // Server receives location
#endif

// --- Viewer summary ---
#ifndef VIEWER_SUMMARY_OUTBOX_FILE
#define VIEWER_SUMMARY_OUTBOX_FILE "viewer_summary.qo"  // Server sends viewer summary
#endif

#ifndef VIEWER_SUMMARY_INBOX_FILE
#define VIEWER_SUMMARY_INBOX_FILE "viewer_summary.qi"   // Viewer receives summary
#endif

// ============================================================================
// DFU (Device Firmware Update) Check Interval
// ============================================================================
#ifndef DFU_CHECK_INTERVAL_MS
#define DFU_CHECK_INTERVAL_MS (60UL * 60UL * 1000UL)  // 1 hour
#endif

// ============================================================================
// Include all common headers
// ============================================================================
#include "TankAlarm_Platform.h"
#include "TankAlarm_Config.h"
#include "TankAlarm_Utils.h"
#include "TankAlarm_Notecard.h"
#include "TankAlarm_Solar.h"
#include "TankAlarm_Battery.h"

#endif // TANKALARM_COMMON_H
