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

#endif // TANKALARM_CONFIG_H
