/*
  Test sketch for Tank Alarm Inches/Feet Implementation
  This test validates the conversion functions and formatting
*/

#include <Arduino.h>

// Mock tank configuration for testing
float tankHeightInches = 120.0;
String siteLocationName = "Test Site";
int tankNumber = 1;

// Convert inches to feet and inches format
String formatInchesToFeetInches(float totalInches) {
  int feet = (int)(totalInches / 12);
  float inches = totalInches - (feet * 12);
  
  String result = String(feet) + "FT," + String(inches, 1) + "IN";
  return result;
}

// Create timestamp in YYYYMMDD format
String getDateTimestamp() {
  // Mock timestamp for testing
  return "2025010100:00";
}

// Test function to validate log format generation
void testLogFormats() {
  float currentLevel = 98.5; // 8 feet 2.5 inches
  float levelChange = 6.3;   // 6.3 inches increase
  
  String feetInchesFormat = formatInchesToFeetInches(currentLevel);
  String changeFeetInchesFormat = formatInchesToFeetInches(abs(levelChange));
  String changePrefix = (levelChange >= 0) ? "+" : "-";
  
  // Test hourly log format
  String hourlyLog = getDateTimestamp() + ",H," + String(tankNumber) + "," + 
                    feetInchesFormat + "," + changePrefix + changeFeetInchesFormat + ",";
  
  // Test daily log format  
  String dailyLog = getDateTimestamp() + ",D," + siteLocationName + "," + String(tankNumber) + "," + 
                   feetInchesFormat + "," + changePrefix + changeFeetInchesFormat + ",";
  
  // Test alarm log format
  String alarmLog = getDateTimestamp() + ",A," + siteLocationName + "," + String(tankNumber) + ",high";
  
  // Test large decrease log format
  float totalDecrease = 30.5; // 2 feet 6.5 inches
  String decreaseFormat = formatInchesToFeetInches(totalDecrease);
  String decreaseLog = getDateTimestamp() + ",S," + String(tankNumber) + "," + decreaseFormat;
  
  Serial.println("=== Tank Alarm Log Format Tests ===");
  Serial.println();
  Serial.println("Current Level: " + String(currentLevel, 1) + " inches = " + feetInchesFormat);
  Serial.println("Level Change: " + changePrefix + String(levelChange, 1) + " inches = " + changePrefix + changeFeetInchesFormat);
  Serial.println();
  Serial.println("Hourly Log Format:");
  Serial.println(hourlyLog);
  Serial.println();
  Serial.println("Daily Log Format:");
  Serial.println(dailyLog);
  Serial.println();
  Serial.println("Alarm Log Format:");
  Serial.println(alarmLog);
  Serial.println();
  Serial.println("Large Decrease Log Format:");
  Serial.println(decreaseLog);
  Serial.println();
  
  // Validate expected format requirements
  Serial.println("=== Format Validation ===");
  
  // Expected: YYYYMMDD00:00,H,(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,
  String expectedHourly = "2025010100:00,H,1,8FT,2.5IN,+0FT,6.3IN,";
  Serial.println("Hourly Expected: " + expectedHourly);
  Serial.println("Hourly Actual  : " + hourlyLog);
  Serial.println("Hourly Match   : " + String(hourlyLog == expectedHourly ? "PASS" : "FAIL"));
  Serial.println();
  
  // Expected: YYYYMMDD00:00,D,(site location name),(Tank Number),(Number of Feet)FT,(Number of Inches)IN,+(Number of feet added in last 24hrs)FT,(Number of inches added in last 24hrs)IN,
  String expectedDaily = "2025010100:00,D,Test Site,1,8FT,2.5IN,+0FT,6.3IN,";
  Serial.println("Daily Expected : " + expectedDaily);
  Serial.println("Daily Actual   : " + dailyLog);
  Serial.println("Daily Match    : " + String(dailyLog == expectedDaily ? "PASS" : "FAIL"));
  Serial.println();
  
  // Expected: YYYYMMDD00:00,A,(site location name),(Tank Number),(alarm state, high or low or change)
  String expectedAlarm = "2025010100:00,A,Test Site,1,high";
  Serial.println("Alarm Expected : " + expectedAlarm);
  Serial.println("Alarm Actual   : " + alarmLog);
  Serial.println("Alarm Match    : " + String(alarmLog == expectedAlarm ? "PASS" : "FAIL"));
  Serial.println();
  
  // Expected: YYYYMMDD00:00,S,(Tank Number),(total Number of Feet decreased)FT,(total Number of Inches decreased)IN
  String expectedDecrease = "2025010100:00,S,1,2FT,6.5IN";
  Serial.println("Decrease Expected: " + expectedDecrease);
  Serial.println("Decrease Actual  : " + decreaseLog);
  Serial.println("Decrease Match   : " + String(decreaseLog == expectedDecrease ? "PASS" : "FAIL"));
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  
  delay(1000);
  testLogFormats();
}

void loop() {
  // Test runs once in setup
  delay(5000);
}