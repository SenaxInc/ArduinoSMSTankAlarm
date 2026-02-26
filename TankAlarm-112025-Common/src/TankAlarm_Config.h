/**
 * TankAlarm_Config.h
 * 
 * Fleet-wide default configuration for TankAlarm 112025.
 * These values are used when no JSON config is available or as fallbacks.
 * 
 * IMPORTANT: All clients in your fleet should use the SAME Product UID.
 * Site-specific settings (site name, sensors, etc.) are configured via JSON.
 * 
 * Copyright (c) 2025 Senax Inc. All rights reserved.
 */

#ifndef TANKALARM_CONFIG_H
#define TANKALARM_CONFIG_H

// ============================================================================
// Blues Notehub Configuration (Fleet-Wide)
// ============================================================================

// Product UID - SAME for all TankAlarm clients in your deployment
// Each project (.ino) must define DEFAULT_PRODUCT_UID before including this header.
// Format: com.company.product:project
// Example: #define DEFAULT_PRODUCT_UID "com.senax.tankalarm112025"

// ============================================================================
// Default Sampling Configuration
// ============================================================================

// Default sampling interval in seconds (30 minutes)
// Used when no JSON config is loaded yet
#ifndef DEFAULT_SAMPLE_INTERVAL_SEC
#define DEFAULT_SAMPLE_INTERVAL_SEC 1800
#endif

// Minimum level change threshold in inches (0 = send all readings)
// Used when no JSON config specifies a threshold
#ifndef DEFAULT_LEVEL_CHANGE_THRESHOLD
#define DEFAULT_LEVEL_CHANGE_THRESHOLD 0.0f
#endif

// ============================================================================
// Default Daily Report Configuration
// ============================================================================

// Default daily report time (5:00 AM local time)
#ifndef DEFAULT_REPORT_HOUR
#define DEFAULT_REPORT_HOUR 5
#endif

#ifndef DEFAULT_REPORT_MINUTE
#define DEFAULT_REPORT_MINUTE 0
#endif

// ============================================================================
// Inbound Polling Intervals
// ============================================================================

// Solar-powered devices check for updates less frequently to save power
#ifndef SOLAR_INBOUND_INTERVAL_MINUTES
#define SOLAR_INBOUND_INTERVAL_MINUTES 60  // 1 hour for solar
#endif

// Solar-powered devices sync outbound less frequently to save power
#ifndef SOLAR_OUTBOUND_INTERVAL_MINUTES
#define SOLAR_OUTBOUND_INTERVAL_MINUTES 360  // 6 hours for solar
#endif

// ============================================================================
// I2C Recovery Configuration
// ============================================================================

// Number of consecutive Notecard failures before attempting I2C bus recovery
#ifndef I2C_NOTECARD_RECOVERY_THRESHOLD
#define I2C_NOTECARD_RECOVERY_THRESHOLD 10
#endif

// Number of consecutive Notecard request failures before entering offline mode
#ifndef NOTECARD_FAILURE_THRESHOLD
#define NOTECARD_FAILURE_THRESHOLD 5
#endif

// Number of consecutive loop iterations (dual failure) before attempting bus recovery
#ifndef I2C_DUAL_FAIL_RECOVERY_LOOPS
#define I2C_DUAL_FAIL_RECOVERY_LOOPS 30
#endif

// Number of consecutive loop iterations (dual failure) before forcing watchdog reset
#ifndef I2C_DUAL_FAIL_RESET_LOOPS
#define I2C_DUAL_FAIL_RESET_LOOPS 120
#endif

// Number of consecutive current-loop-only failures (all sensors) before bus recovery
#ifndef I2C_SENSOR_ONLY_RECOVERY_THRESHOLD
#define I2C_SENSOR_ONLY_RECOVERY_THRESHOLD 10
#endif

// Maximum number of I2C read retries per channel in readCurrentLoopMilliamps()
#ifndef I2C_CURRENT_LOOP_MAX_RETRIES
#define I2C_CURRENT_LOOP_MAX_RETRIES 3
#endif

// Number of startup I2C bus scan attempts when expected devices are missing
#ifndef I2C_STARTUP_SCAN_RETRIES
#define I2C_STARTUP_SCAN_RETRIES 3
#endif

// Delay (ms) between startup scan retry attempts
#ifndef I2C_STARTUP_SCAN_RETRY_DELAY_MS
#define I2C_STARTUP_SCAN_RETRY_DELAY_MS 2000
#endif

// Maximum backoff multiplier for sensor-only I2C recovery (powers of 2)
#ifndef I2C_SENSOR_RECOVERY_MAX_BACKOFF
#define I2C_SENSOR_RECOVERY_MAX_BACKOFF 8
#endif

// 24-hour I2C error count threshold that triggers an alarm note
#ifndef I2C_ERROR_ALERT_THRESHOLD
#define I2C_ERROR_ALERT_THRESHOLD 50
#endif

#endif // TANKALARM_CONFIG_H
