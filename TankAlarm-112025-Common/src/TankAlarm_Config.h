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
// Replace this with your actual Notehub Product UID
// Format: com.company.product:project
#ifndef DEFAULT_PRODUCT_UID
#define DEFAULT_PRODUCT_UID "com.blues.tankalarm:fleet"
#endif

// Default server fleet name (can be overridden in JSON config)
#ifndef DEFAULT_SERVER_FLEET
#define DEFAULT_SERVER_FLEET "tankalarm-server"
#endif

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
// Power Management Defaults
// ============================================================================

// Default power source (false = grid power, true = solar)
#ifndef DEFAULT_SOLAR_POWERED
#define DEFAULT_SOLAR_POWERED false
#endif

// Default MPPT monitoring (false = disabled)
#ifndef DEFAULT_MPPT_ENABLED
#define DEFAULT_MPPT_ENABLED false
#endif

// ============================================================================
// Inbound Polling Intervals
// ============================================================================

// Solar-powered devices check for updates less frequently to save power
#ifndef SOLAR_INBOUND_INTERVAL_MINUTES
#define SOLAR_INBOUND_INTERVAL_MINUTES 60  // 1 hour for solar
#endif

#ifndef GRID_INBOUND_INTERVAL_MINUTES
#define GRID_INBOUND_INTERVAL_MINUTES 10   // 10 minutes for grid power
#endif

#endif // TANKALARM_CONFIG_H
