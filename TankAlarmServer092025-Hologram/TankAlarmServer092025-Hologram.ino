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

// Global variables
bool networkConnected = false;
bool ethernetConnected = false;
String lastEmailSentDate = "";
String lastMonthlyReportDate = "";
const int MAX_TANK_REPORTS = 50;  // Maximum number of reports to store in memory
TankReport tankReports[MAX_TANK_REPORTS];
int reportCount = 0;

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
  }
  
  // Initialize Ethernet connection
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
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
  
  // Initialize cellular connection for Hologram
  connectToHologram();
  
  // Set up interrupt timer for periodic checks
  rtc.setAlarmTime(0, 5, 0);  // Check every hour at 5 minutes past
  rtc.enableAlarm(rtc.MATCH_MMSS);
  rtc.attachInterrupt(periodicCheck);
  
  Serial.println("Tank Alarm Server initialized successfully");
  logEvent("Server startup completed");
}

void loop() {
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
  
  delay(1000);  // Small delay to prevent excessive CPU usage
}

void connectToHologram() {
  Serial.println("Connecting to Hologram network...");
  
  bool connected = false;
  while (!connected) {
    if ((nbAccess.begin("", HOLOGRAM_APN) == NB_READY) &&
        (nbAccess.isAccessAlive())) {
      connected = true;
      networkConnected = true;
      Serial.println("Connected to Hologram network");
      logEvent("Connected to Hologram network");
    } else {
      Serial.println("Hologram connection failed, retrying...");
      delay(5000);
    }
  }
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
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        if (c == '\n' && currentLineIsBlank) {
          // Send HTTP response
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          
          // Send web page
          sendWebPage(client);
          break;
        }
        
        if (c == '\n') {
          currentLineIsBlank = true;
        } else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
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
  
#ifdef USE_HOLOGRAM_EMAIL
  if (USE_HOLOGRAM_EMAIL) {
    // Send email via Hologram API (default method)
    if (sendHologramEmail(HOLOGRAM_EMAIL_RECIPIENT, "Tank Alarm Daily Report", emailContent)) {
      lastEmailSentDate = getDateString();
      Serial.println("Daily email sent successfully via Hologram API");
      logEvent("Daily email sent via Hologram API");
    } else {
      Serial.println("Failed to send daily email via Hologram API, trying SMS fallback");
      // Fallback to SMS gateway
      if (sms.beginSMS(DAILY_EMAIL_SMS_GATEWAY)) {
        sms.print(emailContent);
        sms.endSMS();
        lastEmailSentDate = getDateString();
        Serial.println("Daily email sent successfully via SMS fallback");
        logEvent("Daily email sent via SMS fallback");
      } else {
        Serial.println("Failed to send daily email via both methods");
        logEvent("Failed to send daily email - all methods failed");
      }
    }
  } else {
#endif
    // Send email via SMS to configured email-to-SMS gateway (fallback method)
    if (sms.beginSMS(DAILY_EMAIL_SMS_GATEWAY)) {
      sms.print(emailContent);
      sms.endSMS();
      
      lastEmailSentDate = getDateString();
      Serial.println("Daily email sent successfully via SMS");
      logEvent("Daily email sent via SMS");
    } else {
      Serial.println("Failed to send daily email via SMS");
      logEvent("Failed to send daily email via SMS");
    }
#ifdef USE_HOLOGRAM_EMAIL
  }
#endif
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
  } else if (!ethernetConnected) {
    ethernetConnected = true;
    Serial.println("Ethernet connection restored");
  }
}

void periodicCheck() {
  // This function is called by RTC interrupt
  // Perform any periodic maintenance tasks here
  Serial.println("Periodic check triggered");
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
  
  // Send via email if enabled
  String subject = "Tank Alarm Monthly Report - " + getMonthString();
  
#ifdef USE_HOLOGRAM_EMAIL
  if (USE_HOLOGRAM_EMAIL) {
    if (sendHologramEmail(HOLOGRAM_EMAIL_RECIPIENT, subject, monthlyReportContent)) {
      Serial.println("Monthly report sent successfully via Hologram API");
      logEvent("Monthly report sent via Hologram API");
    } else {
      Serial.println("Failed to send monthly report via Hologram API");
      logEvent("Failed to send monthly report via Hologram API");
    }
  }
#endif
  
  lastMonthlyReportDate = getDateString();
  logEvent("Monthly report generated: " + filename);
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