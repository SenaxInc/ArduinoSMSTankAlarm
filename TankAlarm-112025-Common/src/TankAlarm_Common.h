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
#define FIRMWARE_VERSION "1.0.0"
#endif

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE __DATE__
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
// Watchdog Configuration
// ============================================================================
#ifndef WATCHDOG_TIMEOUT_SECONDS
#define WATCHDOG_TIMEOUT_SECONDS 30
#endif

// ============================================================================
// Notefile Names (for Notecard communication)
// ============================================================================
#ifndef TELEMETRY_FILE
#define TELEMETRY_FILE "telemetry.qi"
#endif

#ifndef ALARM_FILE
#define ALARM_FILE "alarm.qi"
#endif

#ifndef DAILY_FILE
#define DAILY_FILE "daily.qi"
#endif

#ifndef UNLOAD_FILE
#define UNLOAD_FILE "unload.qi"
#endif

#ifndef CONFIG_OUTBOX_FILE
#define CONFIG_OUTBOX_FILE "config.qo"
#endif

#ifndef CONFIG_INBOX_FILE
#define CONFIG_INBOX_FILE "config.qi"
#endif

// ============================================================================
// Time Synchronization
// ============================================================================
#ifndef TIME_SYNC_INTERVAL_MS
#define TIME_SYNC_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)  // 6 hours
#endif

#ifndef DFU_CHECK_INTERVAL_MS
#define DFU_CHECK_INTERVAL_MS (60UL * 60UL * 1000UL)  // 1 hour
#endif

// ============================================================================
// Include all common headers
// ============================================================================
#include "TankAlarm_Platform.h"
#include "TankAlarm_Utils.h"
#include "TankAlarm_Notecard.h"

#endif // TANKALARM_COMMON_H
