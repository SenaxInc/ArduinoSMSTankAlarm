//
// Configuration file for Tank Alarm Server 092025
// This file now contains only essential compile-time constants
// Most configuration is now loaded from server_config.txt on SD card
//

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

// Pin Configuration (hardware specific - cannot be changed at runtime)
#define SD_CARD_CS_PIN 4                           // SD card chip select pin (works for both MKR SD PROTO and MKR ETH shields)

// Network Configuration (timeouts and hardware limits)
#define CONNECTION_TIMEOUT_MS 30000                // Network connection timeout (30 seconds)
#define ETHERNET_RETRY_DELAY_MS 5000              // Delay between Ethernet connection attempts

// Logging Configuration (system limits)
#define MAX_LOG_FILE_SIZE 1000000                 // Maximum log file size in bytes (1MB)

// Web Server Configuration (port must be compile-time for security)
#define WEB_SERVER_PORT 80                        // Port for web server

// System Buffer Sizes (memory allocation at compile time)
#define MESSAGE_BUFFER_SIZE 1024                  // Size of message buffer for parsing

// Email Configuration (compile-time defaults, can be overridden by SD card config)
#ifndef USE_HOLOGRAM_EMAIL
#define USE_HOLOGRAM_EMAIL false                  // Default: do not use Hologram email (can be configured via SD card)
#endif

#ifndef HOLOGRAM_EMAIL_RECIPIENT
#define HOLOGRAM_EMAIL_RECIPIENT ""               // Default: empty (should be configured via SD card)
#endif

// No fallback defaults - SD card configuration is required

#endif // SERVER_CONFIG_H