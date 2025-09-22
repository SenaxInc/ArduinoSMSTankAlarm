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

// Default values used when SD card config is not available
// These are fallback values loaded into variables at startup
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"
#define DAILY_EMAIL_HOUR 6
#define DAILY_EMAIL_MINUTE 0
#define USE_HOLOGRAM_EMAIL true
#define DAILY_EMAIL_SMS_GATEWAY "+19995551234"
#define HOLOGRAM_EMAIL_RECIPIENT "user@example.com"
#define DAILY_EMAIL_RECIPIENT "+15551234567@vtext.com"
#define SERVER_NAME "Tank Alarm Server 092025"
#define SERVER_LOCATION "Main Office"
#define ENABLE_SERIAL_DEBUG true
#define WEB_PAGE_REFRESH_SECONDS 30
#define MAX_REPORTS_IN_MEMORY 50
#define DAYS_TO_KEEP_LOGS 30
#define ETHERNET_MAC_BYTE_1 0x90
#define ETHERNET_MAC_BYTE_2 0xA2
#define ETHERNET_MAC_BYTE_3 0xDA
#define ETHERNET_MAC_BYTE_4 0x10
#define ETHERNET_MAC_BYTE_5 0xD1
#define ETHERNET_MAC_BYTE_6 0x72
#define STATIC_IP_ADDRESS {192, 168, 1, 100}
#define STATIC_GATEWAY {192, 168, 1, 1}
#define STATIC_SUBNET {255, 255, 255, 0}
#define HOLOGRAM_CHECK_INTERVAL_MS 5000
#define FORWARD_ALARMS_TO_EMAIL true
#define ALARM_EMAIL_RECIPIENT "+15551234567@vtext.com"
#define MONTHLY_REPORT_ENABLED true
#define MONTHLY_REPORT_DAY 1
#define MONTHLY_REPORT_HOUR 8

#endif // SERVER_CONFIG_H