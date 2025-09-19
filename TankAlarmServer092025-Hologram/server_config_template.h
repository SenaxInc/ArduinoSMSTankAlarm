//
// Configuration file for Tank Alarm Server 092025
// Copy this file and rename to server_config.h, then update with your specific values
//

#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

// Hologram.io Configuration
#define HOLOGRAM_DEVICE_KEY "your_device_key_here"  // Replace with your actual device key
#define HOLOGRAM_APN "hologram"                     // Hologram.io APN (usually stays "hologram")

// Daily Email Configuration
#define DAILY_EMAIL_HOUR 6                         // Hour to send daily email (24-hour format)
#define DAILY_EMAIL_MINUTE 0                       // Minute to send daily email
#define USE_HOLOGRAM_EMAIL true                    // Use Hologram API for email delivery (default)
#define DAILY_EMAIL_SMS_GATEWAY "+19995551234"     // SMS gateway for email delivery (fallback)
#define HOLOGRAM_EMAIL_RECIPIENT "user@example.com" // Email address for Hologram API delivery

// Pin Configuration
#define SD_CARD_CS_PIN 4                           // SD card chip select pin

// Network Configuration
#define CONNECTION_TIMEOUT_MS 30000                // Network connection timeout (30 seconds)
#define ETHERNET_RETRY_DELAY_MS 5000              // Delay between Ethernet connection attempts

// Logging Configuration
#define ENABLE_SERIAL_DEBUG true                  // Enable/disable serial output for debugging
#define MAX_LOG_FILE_SIZE 1000000                 // Maximum log file size in bytes (1MB)

// Web Server Configuration
#define WEB_SERVER_PORT 80                        // Port for web server
#define WEB_PAGE_REFRESH_SECONDS 30               // Auto refresh interval for web page

// Email/SMS Configuration for Daily Reports
// Format examples for email-to-SMS gateways:
// Verizon: +1##########@vtext.com
// AT&T: +1##########@txt.att.net  
// T-Mobile: +1##########@tmomail.net
// Sprint: +1##########@messaging.sprintpcs.com
#define DAILY_EMAIL_RECIPIENT "+15551234567@vtext.com"  // Email-to-SMS gateway address

// Server Identification
#define SERVER_NAME "Tank Alarm Server 092025"
#define SERVER_LOCATION "Main Office"              // Location description for server

// Data Retention Settings
#define MAX_REPORTS_IN_MEMORY 50                  // Maximum tank reports to keep in memory
#define DAYS_TO_KEEP_LOGS 30                      // Days to keep log files before rotation

// Ethernet MAC Address (change if needed to avoid conflicts)
// Each device should have a unique MAC address
#define ETHERNET_MAC_BYTE_1 0x90
#define ETHERNET_MAC_BYTE_2 0xA2  
#define ETHERNET_MAC_BYTE_3 0xDA
#define ETHERNET_MAC_BYTE_4 0x10
#define ETHERNET_MAC_BYTE_5 0xD1
#define ETHERNET_MAC_BYTE_6 0x72

// Static IP Configuration (used as fallback if DHCP fails)
#define STATIC_IP_ADDRESS {192, 168, 1, 100}      // Server IP address
#define STATIC_GATEWAY {192, 168, 1, 1}           // Gateway IP address  
#define STATIC_SUBNET {255, 255, 255, 0}          // Subnet mask

// Hologram Data Reception Settings
#define HOLOGRAM_CHECK_INTERVAL_MS 5000           // How often to check for new messages
#define MESSAGE_BUFFER_SIZE 1024                  // Size of message buffer for parsing

// Alarm Notification Settings
#define FORWARD_ALARMS_TO_EMAIL true              // Forward alarm messages to email
#define ALARM_EMAIL_RECIPIENT "+15551234567@vtext.com"  // Email for alarm notifications

// Monthly Report Settings
#define MONTHLY_REPORT_ENABLED true               // Enable monthly CSV reports
#define MONTHLY_REPORT_DAY 1                      // Day of month to generate report (1-28)
#define MONTHLY_REPORT_HOUR 8                     // Hour to generate monthly report

#endif // SERVER_CONFIG_H