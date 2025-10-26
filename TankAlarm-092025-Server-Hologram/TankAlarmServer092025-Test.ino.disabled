/*
  Tank Alarm Server Test - Simple Version
  
  This is a simplified test version of the server that can be used to verify
  basic functionality without requiring all hardware components.
  
  Features tested:
  - Web server functionality
  - Data parsing and storage
  - Basic logging
  
  Hardware required for testing:
  - Arduino MKR NB 1500 (or compatible)
  - SD Card
  - Ethernet connection (optional for web testing)
*/

#include <SPI.h>
#include <SD.h>
#include <RTCZero.h>

// Conditional includes based on available hardware
#ifdef ETHERNET_AVAILABLE
#include <Ethernet.h>
byte mac[] = { 0x90, 0xA2, 0xDA, 0x10, 0xD1, 0x72 };
EthernetServer webServer(80);
bool ethernetConnected = false;
#endif

// Initialize RTC for timing
RTCZero rtc;

// Pin definitions
#define SD_CARD_CS_PIN 4

// Test data structure
struct TankReport {
  String timestamp;
  String siteLocation;
  int tankNumber;
  String currentLevel;
  String change24hr;
  String status;
};

// Global variables
const int MAX_TANK_REPORTS = 10;
TankReport tankReports[MAX_TANK_REPORTS];
int reportCount = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    delay(100);
  }
  
  Serial.println("Tank Alarm Server Test Starting...");
  
  // Initialize RTC
  rtc.begin();
  
  // Initialize SD card
  if (!SD.begin(SD_CARD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized successfully");
  }
  
#ifdef ETHERNET_AVAILABLE
  // Initialize Ethernet connection
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    IPAddress ip(192, 168, 1, 100);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    Ethernet.begin(mac, ip, gateway, subnet);
  }
  
  Serial.print("Ethernet IP address: ");
  Serial.println(Ethernet.localIP());
  
  webServer.begin();
  Serial.println("Web server started");
  ethernetConnected = true;
#endif
  
  // Add some test data
  addTestData();
  
  Serial.println("Tank Alarm Server Test initialized successfully");
  logEvent("Test server startup completed");
}

void loop() {
#ifdef ETHERNET_AVAILABLE
  handleWebRequests();
#endif
  
  // Simulate receiving a report every 30 seconds for testing
  static unsigned long lastTestReport = 0;
  if (millis() - lastTestReport > 30000) {
    simulateIncomingReport();
    lastTestReport = millis();
  }
  
  delay(1000);
}

void addTestData() {
  // Add some sample tank reports for testing
  TankReport report1;
  report1.timestamp = getCurrentTimestamp();
  report1.siteLocation = "Test Site Alpha";
  report1.tankNumber = 1;
  report1.currentLevel = "8FT,6.2IN";
  report1.change24hr = "+0FT,2.1IN";
  report1.status = "Normal";
  
  TankReport report2;
  report2.timestamp = getCurrentTimestamp();
  report2.siteLocation = "Test Site Beta";
  report2.tankNumber = 2;
  report2.currentLevel = "12FT,3.8IN";
  report2.change24hr = "-0FT,1.5IN";
  report2.status = "Normal";
  
  if (reportCount < MAX_TANK_REPORTS) {
    tankReports[reportCount++] = report1;
  }
  if (reportCount < MAX_TANK_REPORTS) {
    tankReports[reportCount++] = report2;
  }
  
  Serial.println("Test data added: " + String(reportCount) + " reports");
}

void simulateIncomingReport() {
  String testMessage = "Daily Tank Report Test Site Gamma - " + getCurrentTimestamp();
  testMessage += "\nTank #3 Level: 5FT,4.2IN";
  testMessage += "\n24hr Change: +0FT,0.8IN";
  testMessage += "\nStatus: Normal";
  
  Serial.println("Simulating incoming report...");
  processDailyReport(testMessage);
}

void processDailyReport(String reportData) {
  Serial.println("Processing daily report: " + reportData);
  
  TankReport report;
  report.timestamp = getCurrentTimestamp();
  
  // Extract site location
  int siteStart = reportData.indexOf("Daily Tank Report ") + 18;
  int siteEnd = reportData.indexOf(" - ");
  if (siteStart > 17 && siteEnd > siteStart) {
    report.siteLocation = reportData.substring(siteStart, siteEnd);
  } else {
    report.siteLocation = "Unknown Site";
  }
  
  // Extract tank number
  int tankStart = reportData.indexOf("Tank #") + 6;
  int tankEnd = reportData.indexOf(" Level:");
  if (tankStart > 5 && tankEnd > tankStart) {
    report.tankNumber = reportData.substring(tankStart, tankEnd).toInt();
  } else {
    report.tankNumber = 0;
  }
  
  // Extract current level
  int levelStart = reportData.indexOf("Level: ") + 7;
  int levelEnd = reportData.indexOf("\n", levelStart);
  if (levelStart > 6 && levelEnd > levelStart) {
    report.currentLevel = reportData.substring(levelStart, levelEnd);
  } else {
    report.currentLevel = "Unknown";
  }
  
  // Extract 24hr change
  int changeStart = reportData.indexOf("24hr Change: ") + 13;
  int changeEnd = reportData.indexOf("\n", changeStart);
  if (changeStart > 12 && changeEnd > changeStart) {
    report.change24hr = reportData.substring(changeStart, changeEnd);
  } else {
    report.change24hr = "Unknown";
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
  } else {
    report.status = "Unknown";
  }
  
  // Store report in memory (replace oldest if at capacity)
  if (reportCount < MAX_TANK_REPORTS) {
    tankReports[reportCount] = report;
    reportCount++;
  } else {
    // Shift array to make room for new report
    for (int i = 0; i < MAX_TANK_REPORTS - 1; i++) {
      tankReports[i] = tankReports[i + 1];
    }
    tankReports[MAX_TANK_REPORTS - 1] = report;
  }
  
  // Log report to SD card
  logTankReport(report);
  
  Serial.println("Daily report processed for Tank #" + String(report.tankNumber) + " at " + report.siteLocation);
}

void logTankReport(TankReport report) {
  String logEntry = report.timestamp + ",DAILY," + report.siteLocation + "," + 
                   String(report.tankNumber) + "," + report.currentLevel + "," + 
                   report.change24hr + "," + report.status;
  
  appendToFile("daily_reports.txt", logEntry);
  Serial.println("Tank report logged: " + logEntry);
}

#ifdef ETHERNET_AVAILABLE
void handleWebRequests() {
  EthernetClient client = webServer.available();
  
  if (client) {
    Serial.println("Web client connected");
    boolean currentLineIsBlank = true;
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (c == '\n' && currentLineIsBlank) {
          // Send HTTP response
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          
          // Send web page
          sendTestWebPage(client);
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

void sendTestWebPage(EthernetClient &client) {
  client.println("<!DOCTYPE html>");
  client.println("<html>");
  client.println("<head>");
  client.println("<title>Tank Alarm Server Test</title>");
  client.println("<meta http-equiv='refresh' content='10'>");
  client.println("<style>");
  client.println("body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }");
  client.println("h1 { color: #333; text-align: center; }");
  client.println(".tank-card { background: white; border-radius: 8px; padding: 15px; margin: 10px 0; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
  client.println(".positive { color: green; }");
  client.println(".negative { color: red; }");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<h1>Tank Alarm Server Test Dashboard</h1>");
  client.println("<p style='text-align: center;'>Last updated: " + getCurrentTimestamp() + "</p>");
  client.println("<p style='text-align: center;'>This is a test version of the Tank Alarm Server</p>");
  
  if (reportCount == 0) {
    client.println("<p style='text-align: center;'>No tank reports available.</p>");
  } else {
    for (int i = 0; i < reportCount; i++) {
      TankReport report = tankReports[i];
      
      client.println("<div class='tank-card'>");
      client.println("<h3>" + report.siteLocation + " - Tank #" + String(report.tankNumber) + "</h3>");
      client.println("<p><strong>Level:</strong> " + report.currentLevel + "</p>");
      
      String changeClass = report.change24hr.startsWith("+") ? "positive" : (report.change24hr.startsWith("-") ? "negative" : "");
      client.println("<p class='" + changeClass + "'><strong>24hr Change:</strong> " + report.change24hr + "</p>");
      
      client.println("<p><strong>Status:</strong> " + report.status + "</p>");
      client.println("<p><small>Updated: " + report.timestamp + "</small></p>");
      client.println("</div>");
    }
  }
  
  client.println("<div style='text-align: center; margin-top: 30px; color: #666;'>");
  client.println("<p>Total Reports: " + String(reportCount) + "</p>");
  client.println("<p>Tank Alarm Server Test Version</p>");
  client.println("</div>");
  
  client.println("</body>");
  client.println("</html>");
}
#endif

// Utility functions
String getCurrentTimestamp() {
  char timestamp[20];
  sprintf(timestamp, "%04d%02d%02d %02d:%02d:%02d", 
          rtc.getYear() + 2000, rtc.getMonth(), rtc.getDay(),
          rtc.getHours(), rtc.getMinutes(), rtc.getSeconds());
  return String(timestamp);
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