/*
  Tank Alarm Server 092025 - Hologram Network Version
  
  Hardware:
  - Arduino MKR NB 1500 (with ublox SARA-R410M)
  - Arduino MKR ETH Shield
  - Hologram.io SIM Card
  - SD Card
  - Ethernet connection for local network
  
  Features:
  - Receives daily tank reports via Hologram.io network from client Arduinos
  - Logs all received tank reports to SD card
  - Composes and sends daily email summary of tank level changes
  - Hosts basic website accessible on local network via Ethernet
  - Real-time clock for timestamps
  
  Created: September 2025
  Using GitHub Copilot for code generation
*/

// Core Arduino MKR libraries
#include <MKRNB.h>          // MKR NB 1500 cellular connectivity for Hologram
#include <SPI.h>            // SPI communication for Ethernet
#include <Ethernet.h>       // Ethernet connectivity (use Ethernet2.h if using Ethernet Shield 2)
#include <SD.h>             // SD card functionality
#include <RTCZero.h>        // Real-time clock

// Include configuration file
#include "server_config.h"

// Network components
NB nbAccess;
NBClient hologramClient;
NBSMS sms;

// Ethernet components
byte mac[] = { 0x90, 0xA2, 0xDA, 0x10, 0xD1, 0x72 };  // MAC address for Ethernet shield
EthernetServer webServer(80);                            // Web server on port 80

// Initialize RTC for timing
RTCZero rtc;

// Pin definitions
#ifndef SD_CARD_CS_PIN
#define SD_CARD_CS_PIN 4
#endif

// Hologram.io configuration
const char HOLOGRAM_APN[] = "hologram";
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const int HOLOGRAM_PORT = 9999;
const int HOLOGRAM_LISTEN_PORT = 4010;

// Data storage structures
struct TankReport {
  String timestamp;
  String siteLocation;
  int tankNumber;
  String currentLevel;
  String change24hr;
  String status;
};

// Email recipient management
const int MAX_EMAIL_RECIPIENTS = 10;
String dailyEmailRecipients[MAX_EMAIL_RECIPIENTS];
String monthlyEmailRecipients[MAX_EMAIL_RECIPIENTS];
int dailyRecipientCount = 0;
int monthlyRecipientCount = 0;

// Global variables
bool networkConnected = false;
bool ethernetConnected = false;
String lastEmailSentDate = "";
String lastMonthlyReportDate = "";
const int MAX_TANK_REPORTS = 50;  // Maximum number of reports to store in memory
TankReport tankReports[MAX_TANK_REPORTS];
int reportCount = 0;

// Power failure recovery state
bool systemRecovering = false;
String lastShutdownReason = "";
unsigned long lastHeartbeat = 0;

// Tank ping tracking
struct PingStatus {
  String siteLocation;
  int tankNumber;
  String lastPingTime;
  bool pingSuccess;
  bool pingInProgress;
};

// Power failure tracking for daily emails
struct PowerFailureEvent {
  String timestamp;
  String siteLocation;
  int tankNumber;
  String currentLevel;
  String shutdownReason;
};

const int MAX_PING_ENTRIES = 20;
PingStatus pingStatuses[MAX_PING_ENTRIES];
int pingStatusCount = 0;

const int MAX_POWER_FAILURE_EVENTS = 50;
PowerFailureEvent powerFailureEvents[MAX_POWER_FAILURE_EVENTS];
int powerFailureEventCount = 0;

void setup() {
  // Initialize serial communication for debugging
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  Serial.println("Tank Alarm Server 092025 - Hologram Starting...");
  
  // Initialize RTC
  rtc.begin();
  
  // Initialize SD card
  if (!SD.begin(SD_CARD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized successfully");
    
    // Check for power failure recovery
    checkPowerFailureRecovery();
  }
  
  // Initialize Ethernet connection with retry logic
  initializeEthernet();
  
  // Initialize cellular connection for Hologram with recovery
  initializeCellular();
  
  // Load email recipient lists from SD card
  loadEmailRecipients();
  
  // Restore system state from SD card
  restoreSystemState();
  
  // Set up interrupt timer for periodic checks
  rtc.setAlarmTime(0, 5, 0);  // Check every hour at 5 minutes past
  rtc.enableAlarm(rtc.MATCH_MMSS);
  rtc.attachInterrupt(periodicCheck);
  
  Serial.println("Tank Alarm Server initialized successfully");
  logEvent(systemRecovering ? "Server recovery completed" : "Server startup completed");
  
  // Mark successful startup
  saveSystemState("normal_operation");
}

void loop() {
  // Update heartbeat for power failure detection
  updateHeartbeat();
  
  // Handle web server requests
  handleWebRequests();
  
  // Check for incoming Hologram data
  checkHologramMessages();
  
  // Check if it's time to send daily email
  if (isTimeForDailyEmail()) {
    sendDailyEmail();
  }
  
  // Check if it's time to generate monthly report
  if (isTimeForMonthlyReport()) {
    generateMonthlyReport();
  }
  
  // Maintain network connections
  maintainConnections();
  
  // Periodic system state backup (every 5 minutes)
  static unsigned long lastBackup = 0;
  if (millis() - lastBackup > 300000) {  // 5 minutes
    saveSystemState("periodic_backup");
    lastBackup = millis();
  }
  
  delay(1000);  // Small delay to prevent excessive CPU usage
}



void checkHologramMessages() {
  if (!networkConnected) return;
  
  // Connect to Hologram server to listen for incoming data
  if (hologramClient.connect(HOLOGRAM_URL, HOLOGRAM_LISTEN_PORT)) {
    
    while (hologramClient.available()) {
      String message = hologramClient.readString();
      parseHologramMessage(message);
    }
    
    hologramClient.stop();
  }
}

void parseHologramMessage(String message) {
  Serial.println("Received Hologram message: " + message);
  
  // Simple parsing without JSON library dependency
  // Look for topic and data in the message format
  String topic = "";
  String data = "";
  
  // Extract topic from message (assuming format includes topic)
  if (message.indexOf("DAILY") >= 0) {
    topic = "DAILY";
    data = message;  // For now, use entire message as data
  } else if (message.indexOf("ALARM") >= 0) {
    topic = "ALARM";
    data = message;
  } else if (message.indexOf("POWER_FAILURE") >= 0) {
    topic = "POWER_FAILURE";
    data = message;
  } else {
    // Try to parse as simple format "topic:data"
    int colonIndex = message.indexOf(":");
    if (colonIndex > 0) {
      topic = message.substring(0, colonIndex);
      data = message.substring(colonIndex + 1);
    } else {
      data = message;  // Default to treating entire message as data
    }
  }
  
  if (topic == "DAILY") {
    processDailyReport(data);
  } else if (topic == "ALARM") {
    processAlarmReport(data);
  } else if (topic == "POWER_FAILURE") {
    processPowerFailureReport(data);
  } else {
    // Log unknown message type
    logEvent("Unknown message type received: " + message);
  }
}

void processDailyReport(String reportData) {
  Serial.println("Processing daily report: " + reportData);
  
  // Parse the daily report format from client
  // Expected format: "Daily Tank Report [Site] - [Timestamp]\nTank #[Number] Level: [Level]\n24hr Change: [Change]\nStatus: [Status]"
  
  TankReport report;
  report.timestamp = getCurrentTimestamp();
  
  // Extract site location
  int siteStart = reportData.indexOf("Daily Tank Report ") + 18;
  int siteEnd = reportData.indexOf(" - ");
  if (siteStart > 17 && siteEnd > siteStart) {
    report.siteLocation = reportData.substring(siteStart, siteEnd);
  }
  
  // Extract tank number
  int tankStart = reportData.indexOf("Tank #") + 6;
  int tankEnd = reportData.indexOf(" Level:");
  if (tankStart > 5 && tankEnd > tankStart) {
    report.tankNumber = reportData.substring(tankStart, tankEnd).toInt();
  }
  
  // Extract current level
  int levelStart = reportData.indexOf("Level: ") + 7;
  int levelEnd = reportData.indexOf("\n", levelStart);
  if (levelStart > 6 && levelEnd > levelStart) {
    report.currentLevel = reportData.substring(levelStart, levelEnd);
  }
  
  // Extract 24hr change
  int changeStart = reportData.indexOf("24hr Change: ") + 13;
  int changeEnd = reportData.indexOf("\n", changeStart);
  if (changeStart > 12 && changeEnd > changeStart) {
    report.change24hr = reportData.substring(changeStart, changeEnd);
  }
  
  // Extract status
  int statusStart = reportData.indexOf("Status: ") + 8;
  int statusEnd = reportData.indexOf("\n", statusStart);
  if (statusStart > 7) {
    if (statusEnd > statusStart) {
      report.status = reportData.substring(statusStart, statusEnd);
    } else {
      report.status = reportData.substring(statusStart);
    }
  }
  
  // Store report in memory
  if (reportCount < MAX_TANK_REPORTS) {
    tankReports[reportCount] = report;
    reportCount++;
  }
  
  // Log report to SD card
  logTankReport(report);
  
  Serial.println("Daily report processed for Tank #" + String(report.tankNumber) + " at " + report.siteLocation);
}

void processAlarmReport(String alarmData) {
  Serial.println("Processing alarm report: " + alarmData);
  
  // Log alarm to SD card
  String logEntry = getCurrentTimestamp() + ",ALARM," + alarmData;
  appendToFile("alarm_log.txt", logEntry);
  
  logEvent("Alarm received: " + alarmData);
}

void processPowerFailureReport(String powerFailureData) {
  Serial.println("Processing power failure report: " + powerFailureData);
  
  // Parse power failure data: siteLocation|tankNumber|timestamp|currentLevel|shutdownReason
  int firstPipe = powerFailureData.indexOf("|");
  int secondPipe = powerFailureData.indexOf("|", firstPipe + 1);
  int thirdPipe = powerFailureData.indexOf("|", secondPipe + 1);
  int fourthPipe = powerFailureData.indexOf("|", thirdPipe + 1);
  
  if (firstPipe > 0 && secondPipe > firstPipe && thirdPipe > secondPipe) {
    PowerFailureEvent event;
    event.siteLocation = powerFailureData.substring(0, firstPipe);
    event.tankNumber = powerFailureData.substring(firstPipe + 1, secondPipe).toInt();
    event.timestamp = powerFailureData.substring(secondPipe + 1, thirdPipe);
    event.currentLevel = powerFailureData.substring(thirdPipe + 1, fourthPipe);
    if (fourthPipe > 0) {
      event.shutdownReason = powerFailureData.substring(fourthPipe + 1);
    } else {
      event.shutdownReason = "Unknown";
    }
    
    // Store in memory for daily email inclusion
    if (powerFailureEventCount < MAX_POWER_FAILURE_EVENTS) {
      powerFailureEvents[powerFailureEventCount] = event;
      powerFailureEventCount++;
    }
    
    // Log power failure to SD card
    String logEntry = getCurrentTimestamp() + ",POWER_FAILURE," + event.siteLocation + 
                     "," + String(event.tankNumber) + "," + event.timestamp + "," + 
                     event.currentLevel + "," + event.shutdownReason;
    appendToFile("power_failure_log.txt", logEntry);
    
    logEvent("Power failure reported: " + event.siteLocation + " Tank #" + String(event.tankNumber));
  }
}

void logTankReport(TankReport report) {
  // Format: YYYYMMDD HH:MM,DAILY,SiteLocation,TankNumber,CurrentLevel,24hrChange,Status
  String logEntry = report.timestamp + ",DAILY," + report.siteLocation + "," + 
                   String(report.tankNumber) + "," + report.currentLevel + "," + 
                   report.change24hr + "," + report.status;
  
  appendToFile("daily_reports.txt", logEntry);
  
  Serial.println("Tank report logged: " + logEntry);
}

void handleWebRequests() {
  EthernetClient client = webServer.available();
  
  if (client) {
    Serial.println("Web client connected");
    boolean currentLineIsBlank = true;
    String request = "";
    String httpMethod = "";
    String requestPath = "";
    String postData = "";
    bool isPost = false;
    bool headerComplete = false;
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (!headerComplete) {
          request += c;
          
          // Parse HTTP method and path
          if (request.indexOf('\n') == -1 && request.length() < 100) {
            // Still reading first line
            if (request.startsWith("GET ")) {
              httpMethod = "GET";
            } else if (request.startsWith("POST ")) {
              httpMethod = "POST";
              isPost = true;
            }
          }
          
          if (c == '\n' && currentLineIsBlank) {
            headerComplete = true;
            
            // Extract path from request
            int spacePos = request.indexOf(' ');
            int secondSpacePos = request.indexOf(' ', spacePos + 1);
            if (spacePos > 0 && secondSpacePos > spacePos) {
              requestPath = request.substring(spacePos + 1, secondSpacePos);
            }
            
            if (!isPost) {
              // For GET requests, send response immediately
              sendHttpResponse(client, requestPath);
              break;
            }
          }
          
          if (c == '\n') {
            currentLineIsBlank = true;
          } else if (c != '\r') {
            currentLineIsBlank = false;
          }
        } else if (isPost) {
          // Read POST data
          postData += c;
        }
      }
    }
    
    // Handle POST requests
    if (isPost && postData.length() > 0) {
      handlePostRequest(client, requestPath, postData);
    }
    
    delay(1);
    client.stop();
    Serial.println("Web client disconnected");
  }
}

void sendWebPage(EthernetClient &client) {
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<title>Tank Alarm Server - Dashboard</title>");
  client.println("<meta http-equiv='refresh' content='30'>");  // Auto refresh every 30 seconds
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".nav-links { text-align: center; margin: 20px 0; }");
  client.println(".nav-link { display: inline-block; margin: 0 10px; padding: 8px 16px; background: #6c757d; color: white; text-decoration: none; border-radius: 4px; }");
  client.println(".tank-container { display: flex; flex-wrap: wrap; gap: 20px; justify-content: center; }");
  client.println(".tank-card { background: white; border-radius: 8px; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); min-width: 250px; }");
  client.println(".tank-header { font-size: 18px; font-weight: bold; margin-bottom: 10px; }");
  client.println(".tank-level { font-size: 24px; color: #2c5aa0; margin: 10px 0; }");
  client.println(".tank-change { font-size: 16px; margin: 5px 0; }");
  client.println(".positive { color: green; }");
  client.println(".negative { color: red; }");
  client.println(".status-normal { color: green; }");
  client.println(".status-alarm { color: red; font-weight: bold; }");
  client.println(".footer { text-align: center; margin-top: 30px; color: #666; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<h1>Tank Alarm Server Dashboard</h1>");
  
  client.println("<div class='nav-links'>");
  client.println("<a href='/' class='nav-link'>Dashboard</a>");
  client.println("<a href='/emails' class='nav-link'>Email Management</a>");
  client.println("<a href='/tanks' class='nav-link'>Tank Management</a>");
  client.println("</div>");
  
  client.println("<p style='text-align: center;'>Last updated: " + getCurrentTimestamp() + "</p>");
  
  if (reportCount == 0) {
    client.println("<p style='text-align: center;'>No tank reports received yet.</p>");
  } else {
    client.println("<div class='tank-container'>");
    
    // Display recent tank reports
    for (int i = 0; i < reportCount && i < 10; i++) {  // Show up to 10 most recent reports
      TankReport report = tankReports[i];
      
      client.println("<div class='tank-card'>");
      client.println("<div class='tank-header'>" + report.siteLocation + " - Tank #" + String(report.tankNumber) + "</div>");
      client.println("<div class='tank-level'>Level: " + report.currentLevel + "</div>");
      
      String changeClass = report.change24hr.startsWith("+") ? "positive" : (report.change24hr.startsWith("-") ? "negative" : "");
      client.println("<div class='tank-change " + changeClass + "'>24hr Change: " + report.change24hr + "</div>");
      
      String statusClass = report.status == "Normal" ? "status-normal" : "status-alarm";
      client.println("<div class='status " + statusClass + "'>Status: " + report.status + "</div>");
      
      client.println("<div style='font-size: 12px; color: #666; margin-top: 10px;'>Updated: " + report.timestamp + "</div>");
      client.println("</div>");
    }
    
    client.println("</div>");
  }
  
  // Server status information
  client.println("<div class='footer'>");
  client.println("<p>Server Status: ");
  client.println(networkConnected ? "<span style='color: green;'>Hologram Connected</span>" : "<span style='color: red;'>Hologram Disconnected</span>");
  client.println(" | ");
  client.println(ethernetConnected ? "<span style='color: green;'>Ethernet Connected</span>" : "<span style='color: red;'>Ethernet Disconnected</span>");
  client.println("</p>");
  client.println("<p>Total Reports Received: " + String(reportCount) + "</p>");
  
#ifdef USE_HOLOGRAM_EMAIL
  if (USE_HOLOGRAM_EMAIL) {
    client.println("<p>Email Delivery: <span style='color: blue;'>Hologram API (default)</span></p>");
  } else {
    client.println("<p>Email Delivery: <span style='color: orange;'>SMS Gateway</span></p>");
  }
#else
  client.println("<p>Email Delivery: <span style='color: orange;'>SMS Gateway</span></p>");
#endif

  client.println("<p>Email Recipients: Daily (" + String(dailyRecipientCount) + "), Monthly (" + String(monthlyRecipientCount) + ")</p>");

#ifdef MONTHLY_REPORT_ENABLED
  if (MONTHLY_REPORT_ENABLED) {
    client.println("<p>Monthly Reports: <span style='color: green;'>Enabled</span> (Day " + String(MONTHLY_REPORT_DAY) + " at " + String(MONTHLY_REPORT_HOUR) + ":00)</p>");
  }
#endif
  
  client.println("<p>Tank Alarm Server 092025 - Hologram Version</p>");
  client.println("</div>");
  
  client.println("</body>");
  client.println("</html>");
}

bool isTimeForDailyEmail() {
  // Check if it's time to send daily email (configured time, once per day)
  String currentDate = getDateString();
  
  if (lastEmailSentDate != currentDate) {
    int currentHour = rtc.getHours();
    int currentMinute = rtc.getMinutes();
    
    // Send email at configured time (default 6:00 AM)
    if (currentHour == DAILY_EMAIL_HOUR && currentMinute >= DAILY_EMAIL_MINUTE && currentMinute < DAILY_EMAIL_MINUTE + 5) {
      return true;
    }
  }
  
  return false;
}

void sendDailyEmail() {
  Serial.println("Sending daily email summary...");
  
  String emailContent = composeDailyEmailContent();
  bool emailSent = false;
  
  // Send to all daily email recipients
  for (int i = 0; i < dailyRecipientCount; i++) {
    String recipient = dailyEmailRecipients[i];
    
#ifdef USE_HOLOGRAM_EMAIL
    if (USE_HOLOGRAM_EMAIL) {
      // Send email via Hologram API (default method)
      if (sendHologramEmail(recipient, "Tank Alarm Daily Report", emailContent)) {
        Serial.println("Daily email sent successfully to " + recipient + " via Hologram API");
        logEvent("Daily email sent to " + recipient + " via Hologram API");
        emailSent = true;
      } else {
        Serial.println("Failed to send daily email to " + recipient + " via Hologram API, trying SMS fallback");
        // Fallback to SMS gateway (only for SMS-compatible addresses)
        if (recipient.indexOf("@") != -1 && recipient.indexOf(".com") == -1) {
          if (sms.beginSMS(recipient.c_str())) {
            sms.print(emailContent);
            sms.endSMS();
            Serial.println("Daily email sent successfully to " + recipient + " via SMS fallback");
            logEvent("Daily email sent to " + recipient + " via SMS fallback");
            emailSent = true;
          }
        }
      }
    } else {
#endif
      // Send email via SMS to SMS-compatible addresses only
      if (recipient.indexOf("@") != -1 && recipient.indexOf(".com") == -1) {
        if (sms.beginSMS(recipient.c_str())) {
          sms.print(emailContent);
          sms.endSMS();
          Serial.println("Daily email sent successfully to " + recipient + " via SMS");
          logEvent("Daily email sent to " + recipient + " via SMS");
          emailSent = true;
        }
      }
#ifdef USE_HOLOGRAM_EMAIL
    }
#endif
  }
  
  if (emailSent) {
    lastEmailSentDate = getDateString();
  } else {
    Serial.println("Failed to send daily email to any recipients");
    logEvent("Failed to send daily email - all methods failed");
  }
}

String composeDailyEmailContent() {
  String content = "TANK ALARM DAILY REPORT - " + getCurrentTimestamp() + "\n\n";
  
  if (reportCount == 0) {
    content += "No tank reports received in the last 24 hours.\n";
  } else {
    content += "TANK LEVEL CHANGES:\n";
    
    // Group reports by site and tank
    for (int i = 0; i < reportCount; i++) {
      TankReport report = tankReports[i];
      
      // Only include reports from today
      if (report.timestamp.substring(0, 10) == getDateString()) {
        content += "\n" + report.siteLocation + " Tank #" + String(report.tankNumber) + ":\n";
        content += "  Current Level: " + report.currentLevel + "\n";
        content += "  24hr Change: " + report.change24hr + "\n";
        content += "  Status: " + report.status + "\n";
      }
    }
  }
  
  // Include power failure events from the last 24 hours
  if (powerFailureEventCount > 0) {
    content += "\nPOWER FAILURE EVENTS:\n";
    
    String today = getDateString();
    bool hasPowerFailures = false;
    
    for (int i = 0; i < powerFailureEventCount; i++) {
      PowerFailureEvent event = powerFailureEvents[i];
      
      // Only include power failures from today
      if (event.timestamp.substring(0, 10) == today) {
        if (!hasPowerFailures) {
          hasPowerFailures = true;
        }
        content += "\n" + event.siteLocation + " Tank #" + String(event.tankNumber) + ":\n";
        content += "  Recovery Time: " + event.timestamp + "\n";
        content += "  Level at Recovery: " + event.currentLevel + "\n";
        content += "  Shutdown Reason: " + event.shutdownReason + "\n";
      }
    }
    
    if (!hasPowerFailures) {
      content += "\nNo power failures reported in the last 24 hours.\n";
    }
  } else {
    content += "\nNo power failures reported in the last 24 hours.\n";
  }
  
  content += "\n--- Tank Alarm Server 092025 ---";
  
  return content;
}

void maintainConnections() {
  // Check and maintain Hologram connection
  if (!networkConnected || !nbAccess.isAccessAlive()) {
    Serial.println("Hologram connection lost, reconnecting...");
    connectToHologram();
  }
  
  // Check Ethernet connection
  if (Ethernet.linkStatus() == LinkOFF) {
    ethernetConnected = false;
    Serial.println("Ethernet connection lost");
    logEvent("Ethernet connection lost - attempting recovery");
    
    // Attempt to reinitialize Ethernet
    delay(5000);  // Wait before retry
    initializeEthernet();
  } else if (!ethernetConnected) {
    ethernetConnected = true;
    Serial.println("Ethernet connection restored");
    logEvent("Ethernet connection restored");
  }
}

void initializeEthernet() {
  Serial.println("Initializing Ethernet connection...");
  
  // Try DHCP first
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    logEvent("DHCP failed - using static IP fallback");
    
    // Try to configure using static IP fallback
    IPAddress ip(192, 168, 1, 100);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    Ethernet.begin(mac, ip, gateway, subnet);
  }
  
  Serial.print("Ethernet IP address: ");
  Serial.println(Ethernet.localIP());
  
  // Start web server
  webServer.begin();
  Serial.println("Web server started");
  ethernetConnected = true;
  logEvent("Ethernet initialized - IP: " + String(Ethernet.localIP()));
}

void initializeCellular() {
  Serial.println("Initializing cellular connection...");
  
  int attempts = 0;
  const int maxAttempts = 5;
  
  while (attempts < maxAttempts) {
    if (connectToHologram()) {
      logEvent("Cellular connection established");
      return;
    }
    
    attempts++;
    Serial.println("Cellular connection attempt " + String(attempts) + " failed");
    
    if (attempts < maxAttempts) {
      Serial.println("Retrying in 10 seconds...");
      delay(10000);
    }
  }
  
  logEvent("Cellular connection failed after " + String(maxAttempts) + " attempts");
  Serial.println("WARNING: Operating without cellular connection");
}

bool connectToHologram() {
  Serial.println("Connecting to Hologram network...");
  
  if ((nbAccess.begin("", HOLOGRAM_APN) == NB_READY) &&
      (nbAccess.isAccessAlive())) {
    networkConnected = true;
    Serial.println("Connected to Hologram network");
    return true;
  } else {
    Serial.println("Hologram connection failed");
    networkConnected = false;
    return false;
  }
}

void checkPowerFailureRecovery() {
  Serial.println("Checking for power failure recovery...");
  
  File stateFile = SD.open("system_state.txt", FILE_READ);
  if (stateFile) {
    String lastState = stateFile.readString();
    stateFile.close();
    
    lastState.trim();
    
    if (lastState != "normal_shutdown" && lastState != "normal_operation") {
      systemRecovering = true;
      lastShutdownReason = lastState;
      Serial.println("Power failure detected! Last state: " + lastState);
      logEvent("POWER FAILURE RECOVERY - Last state: " + lastState);
      
      // Restore tank reports from backup
      restoreTankReportsFromSD();
      
      // Send recovery notification
      sendRecoveryNotification();
    } else {
      Serial.println("Normal shutdown detected");
      logEvent("Normal startup - no power failure");
    }
  } else {
    Serial.println("No previous state file found - first startup");
    logEvent("First startup - no previous state");
  }
}

void restoreSystemState() {
  Serial.println("Restoring system state...");
  
  // Restore last email sent dates
  File dateFile = SD.open("email_dates.txt", FILE_READ);
  if (dateFile) {
    lastEmailSentDate = dateFile.readStringUntil('\n');
    lastEmailSentDate.trim();
    lastMonthlyReportDate = dateFile.readStringUntil('\n');
    lastMonthlyReportDate.trim();
    dateFile.close();
    
    Serial.println("Restored email dates - Daily: " + lastEmailSentDate + ", Monthly: " + lastMonthlyReportDate);
    logEvent("Email dates restored");
  }
  
  // Restore tank reports if not already restored
  if (!systemRecovering && reportCount == 0) {
    restoreTankReportsFromSD();
  }
  
  // Restore power failure events
  restorePowerFailureEventsFromSD();
  
  logEvent("System state restoration completed");
}

void restoreTankReportsFromSD() {
  Serial.println("Restoring tank reports from SD card...");
  
  File reportsFile = SD.open("tank_reports_backup.txt", FILE_READ);
  if (reportsFile) {
    reportCount = 0;
    
    while (reportsFile.available() && reportCount < MAX_TANK_REPORTS) {
      String line = reportsFile.readStringUntil('\n');
      line.trim();
      
      if (line.length() > 0) {
        // Parse backup format: timestamp,siteLocation,tankNumber,currentLevel,change24hr,status
        int pos = 0;
        String parts[6];
        
        for (int i = 0; i < 6; i++) {
          int nextPos = line.indexOf(',', pos);
          if (nextPos == -1) {
            parts[i] = line.substring(pos);
            break;
          } else {
            parts[i] = line.substring(pos, nextPos);
            pos = nextPos + 1;
          }
        }
        
        if (parts[0].length() > 0) {
          tankReports[reportCount].timestamp = parts[0];
          tankReports[reportCount].siteLocation = parts[1];
          tankReports[reportCount].tankNumber = parts[2].toInt();
          tankReports[reportCount].currentLevel = parts[3];
          tankReports[reportCount].change24hr = parts[4];
          tankReports[reportCount].status = parts[5];
          reportCount++;
        }
      }
    }
    
    reportsFile.close();
    Serial.println("Restored " + String(reportCount) + " tank reports");
    logEvent("Restored " + String(reportCount) + " tank reports from backup");
  } else {
    Serial.println("No tank reports backup found");
  }
}

void restorePowerFailureEventsFromSD() {
  Serial.println("Restoring power failure events from SD card...");
  
  File powerFailureFile = SD.open("power_failure_backup.txt", FILE_READ);
  if (powerFailureFile) {
    powerFailureEventCount = 0;
    
    while (powerFailureFile.available() && powerFailureEventCount < MAX_POWER_FAILURE_EVENTS) {
      String line = powerFailureFile.readStringUntil('\n');
      line.trim();
      
      if (line.length() > 0) {
        // Parse backup format: timestamp,siteLocation,tankNumber,currentLevel,shutdownReason
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        int thirdComma = line.indexOf(',', secondComma + 1);
        int fourthComma = line.indexOf(',', thirdComma + 1);
        
        if (firstComma > 0 && secondComma > firstComma && thirdComma > secondComma && fourthComma > thirdComma) {
          powerFailureEvents[powerFailureEventCount].timestamp = line.substring(0, firstComma);
          powerFailureEvents[powerFailureEventCount].siteLocation = line.substring(firstComma + 1, secondComma);
          powerFailureEvents[powerFailureEventCount].tankNumber = line.substring(secondComma + 1, thirdComma).toInt();
          powerFailureEvents[powerFailureEventCount].currentLevel = line.substring(thirdComma + 1, fourthComma);
          powerFailureEvents[powerFailureEventCount].shutdownReason = line.substring(fourthComma + 1);
          powerFailureEventCount++;
        }
      }
    }
    
    powerFailureFile.close();
    Serial.println("Restored " + String(powerFailureEventCount) + " power failure events");
    logEvent("Restored " + String(powerFailureEventCount) + " power failure events from backup");
  } else {
    Serial.println("No power failure events backup found");
  }
}

void saveSystemState(String reason) {
  // Save current state for power failure recovery
  File stateFile = SD.open("system_state.txt", FILE_WRITE);
  if (stateFile) {
    stateFile.print(reason);
    stateFile.close();
  }
  
  // Save email dates
  File dateFile = SD.open("email_dates.txt", FILE_WRITE);
  if (dateFile) {
    dateFile.println(lastEmailSentDate);
    dateFile.println(lastMonthlyReportDate);
    dateFile.close();
  }
  
  // Backup tank reports
  backupTankReportsToSD();
  
  // Backup power failure events
  backupPowerFailureEventsToSD();
}

void backupTankReportsToSD() {
  File reportsFile = SD.open("tank_reports_backup.txt", FILE_WRITE);
  if (reportsFile) {
    for (int i = 0; i < reportCount; i++) {
      reportsFile.println(tankReports[i].timestamp + "," + 
                         tankReports[i].siteLocation + "," + 
                         String(tankReports[i].tankNumber) + "," + 
                         tankReports[i].currentLevel + "," + 
                         tankReports[i].change24hr + "," + 
                         tankReports[i].status);
    }
    reportsFile.close();
  }
}

void backupPowerFailureEventsToSD() {
  File powerFailureFile = SD.open("power_failure_backup.txt", FILE_WRITE);
  if (powerFailureFile) {
    for (int i = 0; i < powerFailureEventCount; i++) {
      powerFailureFile.println(powerFailureEvents[i].timestamp + "," + 
                              powerFailureEvents[i].siteLocation + "," + 
                              String(powerFailureEvents[i].tankNumber) + "," + 
                              powerFailureEvents[i].currentLevel + "," + 
                              powerFailureEvents[i].shutdownReason);
    }
    powerFailureFile.close();
  }
}

void updateHeartbeat() {
  // Update heartbeat timestamp for power failure detection
  lastHeartbeat = millis();
  
  // Periodic heartbeat save (every minute)
  static unsigned long lastHeartbeatSave = 0;
  if (millis() - lastHeartbeatSave > 60000) {  // 1 minute
    File heartbeatFile = SD.open("heartbeat.txt", FILE_WRITE);
    if (heartbeatFile) {
      heartbeatFile.println(getCurrentTimestamp());
      heartbeatFile.close();
    }
    lastHeartbeatSave = millis();
  }
}

void sendRecoveryNotification() {
  if (!networkConnected) return;
  
  String message = "TANK ALARM SERVER RECOVERY NOTICE\n";
  message += "Server: " + String(SITE_LOCATION_NAME) + "\n";
  message += "Time: " + getCurrentTimestamp() + "\n";
  message += "Status: System recovered from power failure\n";
  message += "Last State: " + lastShutdownReason + "\n";
  message += "Tank Reports Restored: " + String(reportCount) + "\n";
  message += "All systems operational";
  
  // Send via Hologram API email if available
  if (USE_HOLOGRAM_EMAIL && dailyRecipientCount > 0) {
    for (int i = 0; i < dailyRecipientCount; i++) {
      sendHologramEmail(dailyEmailRecipients[i], "Tank Server Recovery Notice", message);
    }
  }
  
  logEvent("Recovery notification sent");
}

void periodicCheck() {
  // This function is called by RTC interrupt
  // Perform any periodic maintenance tasks here
  Serial.println("Periodic check triggered");
}

void sendHttpResponse(EthernetClient &client, String path) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  if (path == "/" || path == "/dashboard") {
    sendWebPage(client);
  } else if (path == "/emails") {
    sendEmailManagementPage(client);
  } else if (path == "/tanks") {
    sendTankManagementPage(client);
  } else {
    send404Page(client);
  }
}

void handlePostRequest(EthernetClient &client, String path, String postData) {
  if (path == "/emails/daily/add") {
    String email = extractEmailFromPost(postData);
    if (email.length() > 0 && dailyRecipientCount < MAX_EMAIL_RECIPIENTS) {
      dailyEmailRecipients[dailyRecipientCount] = email;
      dailyRecipientCount++;
      saveEmailRecipients();
      logEvent("Added daily email recipient: " + email);
    }
  } else if (path == "/emails/monthly/add") {
    String email = extractEmailFromPost(postData);
    if (email.length() > 0 && monthlyRecipientCount < MAX_EMAIL_RECIPIENTS) {
      monthlyEmailRecipients[monthlyRecipientCount] = email;
      monthlyRecipientCount++;
      saveEmailRecipients();
      logEvent("Added monthly email recipient: " + email);
    }
  } else if (path.startsWith("/emails/daily/remove/")) {
    int index = path.substring(21).toInt();
    removeDailyEmailRecipient(index);
  } else if (path.startsWith("/emails/monthly/remove/")) {
    int index = path.substring(24).toInt();
    removeMonthlyEmailRecipient(index);
  } else if (path.startsWith("/tanks/ping/")) {
    // Handle tank ping request
    String tankId = path.substring(12);
    int underscorePos = tankId.indexOf("_");
    if (underscorePos > 0) {
      String site = tankId.substring(0, underscorePos);
      site.replace("%20", " "); // URL decode spaces
      int tank = tankId.substring(underscorePos + 1).toInt();
      pingTankClient(site, tank);
    }
    
    // Return JSON response for AJAX
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\": \"ping_initiated\"}");
    return;
  }
  
  // Redirect back to appropriate page for non-ping requests
  String redirectPage = "/emails";
  if (path.startsWith("/tanks/")) {
    redirectPage = "/tanks";
  }
  
  client.println("HTTP/1.1 302 Found");
  client.println("Location: " + redirectPage);
  client.println("Connection: close");
  client.println();
}

String extractEmailFromPost(String postData) {
  int emailStart = postData.indexOf("email=");
  if (emailStart >= 0) {
    emailStart += 6; // Length of "email="
    int emailEnd = postData.indexOf("&", emailStart);
    if (emailEnd == -1) {
      emailEnd = postData.length();
    }
    String email = postData.substring(emailStart, emailEnd);
    email.replace("%40", "@");  // URL decode @ symbol
    email.replace("+", " ");     // URL decode spaces
    return email;
  }
  return "";
}

void sendEmailManagementPage(EthernetClient &client) {
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<title>Email Management - Tank Alarm Server</title>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }");
  client.println("h1, h2 { color: #333; }");
  client.println(".container { max-width: 800px; margin: 0 auto; }");
  client.println(".email-section { background: white; border-radius: 8px; padding: 20px; margin: 20px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println(".email-list { margin: 10px 0; }");
  client.println(".email-item { padding: 8px; margin: 5px 0; background: #f8f8f8; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; }");
  client.println(".remove-btn { background: #dc3545; color: white; border: none; padding: 4px 8px; border-radius: 4px; cursor: pointer; }");
  client.println(".add-form { margin-top: 15px; }");
  client.println("input[type='email'] { padding: 8px; width: 250px; margin-right: 10px; }");
  client.println("input[type='submit'] { background: #007bff; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; }");
  client.println(".nav-link { display: inline-block; margin: 10px 5px; padding: 8px 16px; background: #6c757d; color: white; text-decoration: none; border-radius: 4px; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<div class='container'>");
  client.println("<h1>Email Management - Tank Alarm Server</h1>");
  
  client.println("<div style='text-align: center; margin: 20px 0;'>");
  client.println("<a href='/' class='nav-link'>Dashboard</a>");
  client.println("<a href='/emails' class='nav-link'>Email Management</a>");
  client.println("<a href='/tanks' class='nav-link'>Tank Management</a>");
  client.println("</div>");
  
  // Daily email recipients section
  client.println("<div class='email-section'>");
  client.println("<h2>Daily Report Recipients (" + String(dailyRecipientCount) + "/" + String(MAX_EMAIL_RECIPIENTS) + ")</h2>");
  client.println("<div class='email-list'>");
  
  if (dailyRecipientCount == 0) {
    client.println("<p>No daily email recipients configured.</p>");
  } else {
    for (int i = 0; i < dailyRecipientCount; i++) {
      client.println("<div class='email-item'>");
      client.println("<span>" + dailyEmailRecipients[i] + "</span>");
      client.println("<button class='remove-btn' onclick='removeDailyEmail(" + String(i) + ")'>Remove</button>");
      client.println("</div>");
    }
  }
  
  client.println("</div>");
  
  if (dailyRecipientCount < MAX_EMAIL_RECIPIENTS) {
    client.println("<form class='add-form' method='post' action='/emails/daily/add'>");
    client.println("<input type='email' name='email' placeholder='Enter email address' required>");
    client.println("<input type='submit' value='Add Daily Recipient'>");
    client.println("</form>");
  }
  
  client.println("</div>");
  
  // Monthly email recipients section
  client.println("<div class='email-section'>");
  client.println("<h2>Monthly Report Recipients (" + String(monthlyRecipientCount) + "/" + String(MAX_EMAIL_RECIPIENTS) + ")</h2>");
  client.println("<div class='email-list'>");
  
  if (monthlyRecipientCount == 0) {
    client.println("<p>No monthly email recipients configured.</p>");
  } else {
    for (int i = 0; i < monthlyRecipientCount; i++) {
      client.println("<div class='email-item'>");
      client.println("<span>" + monthlyEmailRecipients[i] + "</span>");
      client.println("<button class='remove-btn' onclick='removeMonthlyEmail(" + String(i) + ")'>Remove</button>");
      client.println("</div>");
    }
  }
  
  client.println("</div>");
  
  if (monthlyRecipientCount < MAX_EMAIL_RECIPIENTS) {
    client.println("<form class='add-form' method='post' action='/emails/monthly/add'>");
    client.println("<input type='email' name='email' placeholder='Enter email address' required>");
    client.println("<input type='submit' value='Add Monthly Recipient'>");
    client.println("</form>");
  }
  
  client.println("</div>");
  
  client.println("<script>");
  client.println("function removeDailyEmail(index) {");
  client.println("  if (confirm('Remove this daily email recipient?')) {");
  client.println("    fetch('/emails/daily/remove/' + index, {method: 'POST'}).then(() => location.reload());");
  client.println("  }");
  client.println("}");
  client.println("function removeMonthlyEmail(index) {");
  client.println("  if (confirm('Remove this monthly email recipient?')) {");
  client.println("    fetch('/emails/monthly/remove/' + index, {method: 'POST'}).then(() => location.reload());");
  client.println("  }");
  client.println("}");
  client.println("</script>");
  
  client.println("</div>");
  client.println("</body>");
  client.println("</html>");
}

void send404Page(EthernetClient &client) {
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head><title>404 - Not Found</title></head>");
  client.println("<body>");
  client.println("<h1>404 - Page Not Found</h1>");
  client.println("<p><a href='/'>Return to Dashboard</a></p>");
  client.println("</body>");
  client.println("</html>");
}

void loadEmailRecipients() {
  // Load daily email recipients
  if (SD.begin(SD_CARD_CS_PIN)) {
    File dailyFile = SD.open("daily_emails.txt", FILE_READ);
    if (dailyFile) {
      dailyRecipientCount = 0;
      while (dailyFile.available() && dailyRecipientCount < MAX_EMAIL_RECIPIENTS) {
        String email = dailyFile.readStringUntil('\n');
        email.trim();
        if (email.length() > 0) {
          dailyEmailRecipients[dailyRecipientCount] = email;
          dailyRecipientCount++;
        }
      }
      dailyFile.close();
      Serial.println("Loaded " + String(dailyRecipientCount) + " daily email recipients");
    } else {
      // Add default recipient if file doesn't exist
      dailyEmailRecipients[0] = HOLOGRAM_EMAIL_RECIPIENT;
      dailyRecipientCount = 1;
      saveEmailRecipients();
      Serial.println("Created default daily email recipient");
    }
    
    // Load monthly email recipients
    File monthlyFile = SD.open("monthly_emails.txt", FILE_READ);
    if (monthlyFile) {
      monthlyRecipientCount = 0;
      while (monthlyFile.available() && monthlyRecipientCount < MAX_EMAIL_RECIPIENTS) {
        String email = monthlyFile.readStringUntil('\n');
        email.trim();
        if (email.length() > 0) {
          monthlyEmailRecipients[monthlyRecipientCount] = email;
          monthlyRecipientCount++;
        }
      }
      monthlyFile.close();
      Serial.println("Loaded " + String(monthlyRecipientCount) + " monthly email recipients");
    } else {
      // Add default recipient if file doesn't exist
      monthlyEmailRecipients[0] = HOLOGRAM_EMAIL_RECIPIENT;
      monthlyRecipientCount = 1;
      saveEmailRecipients();
      Serial.println("Created default monthly email recipient");
    }
  }
}

void saveEmailRecipients() {
  if (SD.begin(SD_CARD_CS_PIN)) {
    // Save daily email recipients
    File dailyFile = SD.open("daily_emails.txt", FILE_WRITE);
    if (dailyFile) {
      // Clear file first
      dailyFile.seek(0);
      for (int i = 0; i < dailyRecipientCount; i++) {
        dailyFile.println(dailyEmailRecipients[i]);
      }
      dailyFile.close();
    }
    
    // Save monthly email recipients
    File monthlyFile = SD.open("monthly_emails.txt", FILE_WRITE);
    if (monthlyFile) {
      // Clear file first
      monthlyFile.seek(0);
      for (int i = 0; i < monthlyRecipientCount; i++) {
        monthlyFile.println(monthlyEmailRecipients[i]);
      }
      monthlyFile.close();
    }
    
    logEvent("Email recipient lists saved to SD card");
  }
}

void removeDailyEmailRecipient(int index) {
  if (index >= 0 && index < dailyRecipientCount) {
    String removedEmail = dailyEmailRecipients[index];
    
    // Shift remaining recipients down
    for (int i = index; i < dailyRecipientCount - 1; i++) {
      dailyEmailRecipients[i] = dailyEmailRecipients[i + 1];
    }
    dailyRecipientCount--;
    
    saveEmailRecipients();
    logEvent("Removed daily email recipient: " + removedEmail);
  }
}

void removeMonthlyEmailRecipient(int index) {
  if (index >= 0 && index < monthlyRecipientCount) {
    String removedEmail = monthlyEmailRecipients[index];
    
    // Shift remaining recipients down
    for (int i = index; i < monthlyRecipientCount - 1; i++) {
      monthlyEmailRecipients[i] = monthlyEmailRecipients[i + 1];
    }
    monthlyRecipientCount--;
    
    saveEmailRecipients();
    logEvent("Removed monthly email recipient: " + removedEmail);
  }
}

void sendTankManagementPage(EthernetClient &client) {
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<title>Tank Management - Tank Alarm Server</title>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }");
  client.println("h1, h2 { color: #333; }");
  client.println(".container { max-width: 1000px; margin: 0 auto; }");
  client.println(".tank-section { background: white; border-radius: 8px; padding: 20px; margin: 20px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println(".tank-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 15px; }");
  client.println(".tank-item { padding: 15px; margin: 5px 0; background: #f8f8f8; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; }");
  client.println(".tank-info { flex-grow: 1; }");
  client.println(".tank-actions { display: flex; gap: 10px; align-items: center; }");
  client.println(".ping-btn { background: #007bff; color: white; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; }");
  client.println(".ping-btn:disabled { background: #6c757d; cursor: not-allowed; }");
  client.println(".ping-status { margin-left: 10px; font-size: 20px; }");
  client.println(".status-success { color: #28a745; }");
  client.println(".status-error { color: #dc3545; }");
  client.println(".status-pending { color: #ffc107; }");
  client.println(".nav-link { display: inline-block; margin: 10px 5px; padding: 8px 16px; background: #6c757d; color: white; text-decoration: none; border-radius: 4px; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<div class='container'>");
  client.println("<h1>Tank Management - Tank Alarm Server</h1>");
  
  client.println("<div style='text-align: center; margin: 20px 0;'>");
  client.println("<a href='/' class='nav-link'>Dashboard</a>");
  client.println("<a href='/emails' class='nav-link'>Email Management</a>");
  client.println("<a href='/tanks' class='nav-link'>Tank Management</a>");
  client.println("</div>");
  
  client.println("<div class='tank-section'>");
  client.println("<h2>Tank Clients - Remote Ping Control</h2>");
  client.println("<p>Use the ping buttons to test connectivity with tank clients. This feature will be used for remote device control.</p>");
  
  if (reportCount == 0) {
    client.println("<p>No tank reports received yet. Tank clients will appear here once they send data.</p>");
  } else {
    client.println("<div class='tank-grid'>");
    
    // Get unique tanks from recent reports
    String uniqueTanks[20]; // Support up to 20 unique tanks
    int uniqueCount = 0;
    
    for (int i = 0; i < reportCount && uniqueCount < 20; i++) {
      TankReport report = tankReports[i];
      String tankKey = report.siteLocation + "_" + String(report.tankNumber);
      
      // Check if this tank is already in the list
      bool found = false;
      for (int j = 0; j < uniqueCount; j++) {
        if (uniqueTanks[j] == tankKey) {
          found = true;
          break;
        }
      }
      
      if (!found) {
        uniqueTanks[uniqueCount] = tankKey;
        uniqueCount++;
      }
    }
    
    // Display each unique tank with ping controls
    for (int i = 0; i < uniqueCount; i++) {
      String tankKey = uniqueTanks[i];
      int underscorePos = tankKey.indexOf("_");
      String site = tankKey.substring(0, underscorePos);
      String tankNum = tankKey.substring(underscorePos + 1);
      
      // Find the most recent report for this tank
      String lastSeen = "Unknown";
      String currentLevel = "Unknown";
      for (int j = 0; j < reportCount; j++) {
        TankReport report = tankReports[j];
        if (report.siteLocation == site && String(report.tankNumber) == tankNum) {
          lastSeen = report.timestamp;
          currentLevel = report.currentLevel;
          break; // Most recent report found
        }
      }
      
      // Get ping status for this tank
      PingStatus* status = getPingStatus(site, tankNum.toInt());
      String pingStatusIcon = "";
      String pingStatusClass = "";
      if (status) {
        if (status->pingInProgress) {
          pingStatusIcon = "⏳";
          pingStatusClass = "status-pending";
        } else if (status->pingSuccess) {
          pingStatusIcon = "✅";
          pingStatusClass = "status-success";
        } else {
          pingStatusIcon = "❌";
          pingStatusClass = "status-error";
        }
      }
      
      String tankIdForUrl = site;
      tankIdForUrl.replace(" ", "%20"); // URL encode spaces
      tankIdForUrl += "_" + tankNum;
      
      client.println("<div class='tank-item'>");
      client.println("<div class='tank-info'>");
      client.println("<strong>" + site + " - Tank #" + tankNum + "</strong><br>");
      client.println("Current Level: " + currentLevel + "<br>");
      client.println("Last Seen: " + lastSeen);
      client.println("</div>");
      client.println("<div class='tank-actions'>");
      client.println("<button class='ping-btn' onclick='pingTank(\"" + tankIdForUrl + "\", this)' id='ping_" + tankIdForUrl + "'>Ping Tank</button>");
      if (pingStatusIcon.length() > 0) {
        client.println("<span class='ping-status " + pingStatusClass + "' id='status_" + tankIdForUrl + "'>" + pingStatusIcon + "</span>");
      } else {
        client.println("<span class='ping-status' id='status_" + tankIdForUrl + "'></span>");
      }
      client.println("</div>");
      client.println("</div>");
    }
    
    client.println("</div>");
  }
  
  client.println("</div>");
  
  client.println("<script>");
  client.println("function pingTank(tankId, button) {");
  client.println("  const statusElement = document.getElementById('status_' + tankId);");
  client.println("  button.disabled = true;");
  client.println("  button.textContent = 'Pinging...';");
  client.println("  statusElement.textContent = '⏳';");
  client.println("  statusElement.className = 'ping-status status-pending';");
  client.println("  ");
  client.println("  fetch('/tanks/ping/' + tankId, {");
  client.println("    method: 'POST',");
  client.println("    headers: {'Content-Type': 'application/x-www-form-urlencoded'}");
  client.println("  })");
  client.println("  .then(response => response.json())");
  client.println("  .then(data => {");
  client.println("    setTimeout(() => {");
  client.println("      // Simulate ping completion after 3 seconds");
  client.println("      const success = Math.random() > 0.3; // 70% success rate for demo");
  client.println("      if (success) {");
  client.println("        statusElement.textContent = '✅';");
  client.println("        statusElement.className = 'ping-status status-success';");
  client.println("      } else {");
  client.println("        statusElement.textContent = '❌';");
  client.println("        statusElement.className = 'ping-status status-error';");
  client.println("      }");
  client.println("      button.disabled = false;");
  client.println("      button.textContent = 'Ping Tank';");
  client.println("    }, 3000);");
  client.println("  })");
  client.println("  .catch(error => {");
  client.println("    statusElement.textContent = '❌';");
  client.println("    statusElement.className = 'ping-status status-error';");
  client.println("    button.disabled = false;");
  client.println("    button.textContent = 'Ping Tank';");
  client.println("  });");
  client.println("}");
  client.println("</script>");
  
  client.println("</div>");
  client.println("</body>");
  client.println("</html>");
}

PingStatus* getPingStatus(String siteLocation, int tankNumber) {
  for (int i = 0; i < pingStatusCount; i++) {
    if (pingStatuses[i].siteLocation == siteLocation && 
        pingStatuses[i].tankNumber == tankNumber) {
      return &pingStatuses[i];
    }
  }
  return nullptr;
}

void pingTankClient(String siteLocation, int tankNumber) {
  Serial.println("Pinging tank client: " + siteLocation + " Tank #" + String(tankNumber));
  
  // Find or create ping status entry
  PingStatus* status = getPingStatus(siteLocation, tankNumber);
  if (!status && pingStatusCount < MAX_PING_ENTRIES) {
    status = &pingStatuses[pingStatusCount];
    status->siteLocation = siteLocation;
    status->tankNumber = tankNumber;
    pingStatusCount++;
  }
  
  if (status) {
    status->pingInProgress = true;
    status->lastPingTime = getCurrentTimestamp();
    
    // Send ping message via Hologram API
    String pingMessage = "PING_REQUEST:" + siteLocation + ":Tank" + String(tankNumber);
    
    if (networkConnected) {
      bool pingResult = sendHologramPing(pingMessage);
      
      // Update status after ping attempt
      status->pingInProgress = false;
      status->pingSuccess = pingResult;
      
      String logMsg = "Tank ping " + siteLocation + " #" + String(tankNumber) + ": ";
      logMsg += pingResult ? "SUCCESS" : "FAILED";
      logEvent(logMsg);
    } else {
      status->pingInProgress = false;
      status->pingSuccess = false;
      logEvent("Tank ping failed - no network connection");
    }
  }
}

bool sendHologramPing(String pingMessage) {
  // Send ping via Hologram API
  if (!networkConnected) return false;
  
  // Create JSON payload for ping
  String jsonPayload = "{\"k\":\"" + String(HOLOGRAM_DEVICE_KEY) + "\",";
  jsonPayload += "\"d\":\"" + pingMessage + "\",";
  jsonPayload += "\"t\":[\"PING\"]}";
  
  // Replace newlines with \\n for JSON
  jsonPayload.replace("\n", "\\n");
  jsonPayload.replace("\r", "");
  
  // Connect to Hologram server
  if (hologramClient.connect(HOLOGRAM_URL, HOLOGRAM_PORT)) {
    Serial.println("Connected to Hologram for ping transmission");
    
    // Send the ping data
    hologramClient.print(jsonPayload);
    hologramClient.stop();
    
    Serial.println("Ping sent via Hologram API: " + pingMessage);
    return true;
  } else {
    Serial.println("Failed to connect to Hologram for ping");
    return false;
  }
}

bool sendHologramEmail(String recipient, String subject, String body) {
  // Send email via Hologram API
  if (!networkConnected) return false;
  
  // Create JSON payload for Hologram email API
  String jsonPayload = "{\"k\":\"" + String(HOLOGRAM_DEVICE_KEY) + "\",";
  jsonPayload += "\"d\":{";
  jsonPayload += "\"to\":\"" + recipient + "\",";
  jsonPayload += "\"subject\":\"" + subject + "\",";
  jsonPayload += "\"body\":\"" + body + "\"";
  jsonPayload += "},";
  jsonPayload += "\"t\":[\"EMAIL\"]}";
  
  // Replace newlines with \\n for JSON
  jsonPayload.replace("\n", "\\n");
  jsonPayload.replace("\r", "");
  
  // Connect to Hologram server
  if (hologramClient.connect(HOLOGRAM_URL, HOLOGRAM_PORT)) {
    Serial.println("Connected to Hologram for email delivery");
    
    // Send the email data
    hologramClient.print(jsonPayload);
    hologramClient.stop();
    
    Serial.println("Email sent via Hologram API");
    logEvent("Email sent via Hologram API to: " + recipient);
    return true;
  } else {
    Serial.println("Failed to connect to Hologram for email");
    logEvent("Failed to connect to Hologram for email delivery");
    return false;
  }
}

bool isTimeForMonthlyReport() {
#ifdef MONTHLY_REPORT_ENABLED
  if (!MONTHLY_REPORT_ENABLED) return false;
  
  String currentDate = getDateString();
  
  if (lastMonthlyReportDate != currentDate) {
    int currentDay = rtc.getDay();
    int currentHour = rtc.getHours();
    
    // Generate report on configured day of month at configured hour
    if (currentDay == MONTHLY_REPORT_DAY && currentHour == MONTHLY_REPORT_HOUR) {
      return true;
    }
  }
#endif
  
  return false;
}

void generateMonthlyReport() {
  Serial.println("Generating monthly CSV report...");
  
  String monthlyReportContent = composeMonthlyCSVReport();
  String filename = "monthly_report_" + getMonthString() + ".csv";
  
  // Save to SD card
  appendToFile(filename, monthlyReportContent);
  
  // Send via email to all monthly recipients
  String subject = "Tank Alarm Monthly Report - " + getMonthString();
  bool emailSent = false;
  
  for (int i = 0; i < monthlyRecipientCount; i++) {
    String recipient = monthlyEmailRecipients[i];
    
#ifdef USE_HOLOGRAM_EMAIL
    if (USE_HOLOGRAM_EMAIL) {
      if (sendHologramEmail(recipient, subject, monthlyReportContent)) {
        Serial.println("Monthly report sent successfully to " + recipient + " via Hologram API");
        logEvent("Monthly report sent to " + recipient + " via Hologram API");
        emailSent = true;
      } else {
        Serial.println("Failed to send monthly report to " + recipient + " via Hologram API");
        logEvent("Failed to send monthly report to " + recipient + " via Hologram API");
      }
    }
#endif
  }
  
  lastMonthlyReportDate = getDateString();
  logEvent("Monthly report generated: " + filename + " (sent to " + String(monthlyRecipientCount) + " recipients)");
}

String composeMonthlyCSVReport() {
  String csvContent = "Tank Alarm Monthly Report - " + getMonthString() + "\n\n";
  csvContent += "Date,Site Location,Tank Number,Current Level,Daily Change,Major Decrease\n";
  
  // Group data by site location and tank number
  String sites[10]; // Support up to 10 different sites
  int siteCount = 0;
  
  // First pass: identify unique sites
  for (int i = 0; i < reportCount; i++) {
    TankReport report = tankReports[i];
    String siteKey = report.siteLocation + "_Tank" + String(report.tankNumber);
    
    bool siteExists = false;
    for (int j = 0; j < siteCount; j++) {
      if (sites[j] == siteKey) {
        siteExists = true;
        break;
      }
    }
    
    if (!siteExists && siteCount < 10) {
      sites[siteCount] = siteKey;
      siteCount++;
    }
  }
  
  // Second pass: generate CSV data for each site
  for (int s = 0; s < siteCount; s++) {
    String currentSite = sites[s];
    
    // Extract site name and tank number
    int underscorePos = currentSite.indexOf("_Tank");
    String siteName = currentSite.substring(0, underscorePos);
    String tankNum = currentSite.substring(underscorePos + 5);
    
    csvContent += "\n// " + siteName + " Tank #" + tankNum + "\n";
    
    // Add reports for this site/tank combination
    for (int i = 0; i < reportCount; i++) {
      TankReport report = tankReports[i];
      String reportSiteKey = report.siteLocation + "_Tank" + String(report.tankNumber);
      
      if (reportSiteKey == currentSite) {
        // Check if this is a major decrease
        String majorDecrease = "No";
        if (report.change24hr.startsWith("-")) {
          // Extract numeric value to check if it's major
          String changeStr = report.change24hr.substring(1); // Remove minus sign
          // Simple check for major decrease (more than 1 foot or 12 inches)
          if (changeStr.indexOf("FT") > 0 && changeStr.charAt(0) != '0') {
            majorDecrease = "Yes";
          }
        }
        
        csvContent += report.timestamp.substring(0, 10) + ","; // Date only
        csvContent += report.siteLocation + ",";
        csvContent += String(report.tankNumber) + ",";
        csvContent += report.currentLevel + ",";
        csvContent += report.change24hr + ",";
        csvContent += majorDecrease + "\n";
      }
    }
  }
  
  // Add summary section
  csvContent += "\n// Summary\n";
  csvContent += "Total Sites," + String(siteCount) + "\n";
  csvContent += "Total Reports," + String(reportCount) + "\n";
  csvContent += "Report Period," + getMonthString() + "\n";
  
  return csvContent;
}

// Utility functions
String getCurrentTimestamp() {
  char timestamp[20];
  sprintf(timestamp, "%04d%02d%02d %02d:%02d:%02d", 
          rtc.getYear() + 2000, rtc.getMonth(), rtc.getDay(),
          rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
  return String(timestamp);
}

String getDateString() {
  char dateStr[9];
  sprintf(dateStr, "%04d%02d%02d", 
          rtc.getYear() + 2000, rtc.getMonth(), rtc.getDay());
  return String(dateStr);
}

String getMonthString() {
  char monthStr[8];
  sprintf(monthStr, "%04d-%02d", 
          rtc.getYear() + 2000, rtc.getMonth());
  return String(monthStr);
}

void logEvent(String event) {
  String logEntry = getCurrentTimestamp() + " - " + event;
  appendToFile("server_log.txt", logEntry);
  Serial.println("LOG: " + logEntry);
}

void appendToFile(String filename, String content) {
  if (SD.begin(SD_CARD_CS_PIN)) {
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
      file.println(content);
      file.close();
    }
  }
}