#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>   //Use SoftwareSerial to communicate with LTEshield

//Click here to get the library: http://librarymanager/All#SparkFun_LTE_Shield_Arduino_Library
#include <SparkFun_LTE_Shield_Arduino_Library.h>

// Create a SoftwareSerial object to pass to the LTE_Shield library
SoftwareSerial lteSerial(8, 9);
// Create a LTE_Shield object to use throughout the sketch
#define LTE_SHIELD_POWER_PULSE_PERIOD 3200
#define POWER_PIN 5
#define RESET_PIN 6
LTE_Shield lte;

//CONNECT SENSOR #1 to PINS 11 and A1
#define SENSORVCC1_PIN 11
#define SENSORADC1_PIN A1
//CONNECT SENSOR #2 to PINS 12 and A2
#define SENSORVCC1_PIN 12
#define SENSORADC1_PIN A2
//CONNECT SENSOR #3 to PINS 13 and A3
#define SENSORVCC1_PIN 13
#define SENSORADC1_PIN A3

// Plug in your Hologram device key here:
String HOLOGRAM_DEVICE_KEY = "Ab12CdE4";

// These values should remain the same:
const char HOLOGRAM_URL[] = "cloudsocket.hologram.io";
const unsigned int HOLOGRAM_PORT = 9999;

// PIN Number
#define PINNUMBER ""

volatile int time_tick_hours = 1; //start tick count at 1
volatile int time_tick_report = 1; //start tick count at 1

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
int ticks_per_report = 442;  //default 59 min of 8 second ticks 59*60/8

// char array of the telephone number to send SMS

//USE "S" for SETUP, "A" for ALARM, "D" for DAILY
//server
//char remoteNumber[20]= "1918XXXXXXX"; //server

//daily text
//char remoteNumber[20]= "1918XXXXXXX"; //server

//alarm
char remoteNumber_two[20]= "1918XXXXXXX"; //alarm contact

// char array of the message
char char_reporttext[50]; //text sent with each report
char char_alarmtext[50]; //text sent when alarm triggered
char char_currentsettings[100]; //is length required?

String string_settingtext_raw;

// set connection state variable
boolean notConnected = true;

// this is the threshold used for reading
int readvalue_one;
int readvalue_two;
int readvalue_three;
int readinches_one;
int readinches_two;
int readinches_three;
int contents_one;
int constant_one;
int toground_one;
int alarm_one = 1; //alarm set to always go off until set otherwise

int settingtext_tanknumber;
int settingtext_value;
int settingtext_tankcontents;
int readfresh;

void setup() {
//start up routine 
  wdt_disable(); //recomended
  sei();  //enable interrupts
  ADCSRA |= (1<<ADEN); //ADC hex code set to on
  power_adc_enable(); //enable ADC module    
//always off power saving settings

defineSETTINGS();
  
//power up and read sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(SENSORVCC1_PIN, HIGH);  //pin #5 powers 5V to sensor
        pinMode(SENSORVCC1_PIN, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(SENSORADC1_PIN);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(SENSORADC1_PIN);  // read a sensor from analog pin #A0
        delay(1000);
        digitalWrite(SENSORVCC1_PIN, LOW); // turn off sensor
        readinches_one = (10*(readvalue_one-102))/(8180/(10000/((EEPROM.read(20)*4)/12))); //converts to inches
  
  // Start LTEshied communication     

lte.begin(lteSerial, 9600);
         
  
  //compose text string
  
  //tank 1
  settingtext_tanknumber=1;  //can this be a for statement?
          String string_currentsettings = "Current Settings Tank #";
          string_currentsettings +=settingtext_tanknumber;
          string_currentsettings +="\n";
          string_currentsettings +="T";
          string_currentsettings +=EEPROM.read(0);
          string_currentsettings +="\n";
          string_currentsettings +="A";    
          string_currentsettings +=EEPROM.read(9+settingtext_tanknumber);
          string_currentsettings +="\n";
          string_currentsettings +="C";    
          string_currentsettings +=EEPROM.read(29+settingtext_tanknumber);
          string_currentsettings +=EEPROM.read(19+settingtext_tanknumber);            
          string_currentsettings +="\n";
          string_currentsettings +="H";    
          string_currentsettings +=EEPROM.read(39+settingtext_tanknumber);
          string_currentsettings +="\n";
          string_currentsettings +="Current Fluid Level Tank 1:";    
          string_currentsettings +=readinches_one;   //convert to feet and inches here
          string_currentsettings +=" inches"; 
                      
          //turn string of settings into a character array so it can be sms'd
          string_currentsettings.toCharArray(char_currentsettings,100);
                      
          //wait for eeprom just for fun
          delay(1000);   
  
  //connect to network
  int socket = -1;
  String hologramMessage;
  static String message = "";
  
  // New lines are not handled well
  message.replace('\r', ' ');
  message.replace('\n', ' ');

  // Construct a JSON-encoded Hologram message string:
  hologramMessage = "{\"k\":\"" + HOLOGRAM_DEVICE_KEY + "\",\"d\":\"" +
    message + "\"}";
  
  // Open a socket
  socket = lte.socketOpen(LTE_SHIELD_TCP);
  // On success, socketOpen will return a value between 0-5. On fail -1.
  if (socket >= 0) {
    // Use the socket to connec to the Hologram server
    if (lte.socketConnect(socket, HOLOGRAM_URL, HOLOGRAM_PORT) == LTE_SHIELD_SUCCESS) {
      // Send our message to the server:
      if (lte.socketWrite(socket, hologramMessage) == LTE_SHIELD_SUCCESS)
      {
        // On succesful write, close the socket.
        if (lte.socketClose(socket) == LTE_SHIELD_SUCCESS) {
        }
      } else {
          // TODO - tick to retry 10 times
            lte.poll();
        }
    }
  }
  message = ""; // Clear message string

  lte.poll();

  //lte.powerOn();

    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, LOW);
    delay(LTE_SHIELD_POWER_PULSE_PERIOD);
    pinMode(POWER_PIN, INPUT); // Return to high-impedance, rely on SARA module internal pull-up
    
  
              /*
              while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
  sms.beginSMS(remoteNumber);
  sms.print(char_currentsettings); //INCLUDE CURRENT EEPROM SETTINGS IN TEXT
  sms.endSMS();
  delay(10000);
  */
          string_currentsettings = "";                   
                      
          //wait for eeprom just for fun
          delay(1000);


  
  //SHUTDOWN GSM
  gsmAccess.shutdown();
  notConnected = true;

delay(500);


//define watchdog settings
watchdogSET();  

//prepare for sleep - turn off some settings
    power_adc_disable(); //disable the clock to the ADC module
    ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
                         //can USART be turned off here? power_usart_disable()
}

void loop() {
  
    tickSleep();  //puts arduino to sleep for about 8 seconds
  
//wake from sleep interput here//
  
//check for daily trigger                          

if (time_tick_report > ticks_per_report && time_tick_hours > ticks_per_sleep) {   //if number of ticks has reached 24 hours worth send text no matter what
  wdt_disable();
            dailyTEXT();
          
            time_tick_report = 1;  //daily tick reset
            time_tick_hours = 1;  //hourly tick reset
  watchdogSET();          
        } //end daily text/check

//if day has not elapsed then check hourly ticks
  //hourly wake up to check for alarm trigger
else if (time_tick_hours > ticks_per_sleep) {  //if number of ticks has reach hour goal send text 
  wdt_disable();              
            sleepyTEXT();
                                    
            time_tick_hours = 1;   //rest ticks
  watchdogSET();                                    
        } //end hourly text/check
    
}
    
void tickSleep()   
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
    sleep_enable();
    sei();  //enable interrupts
    sleep_mode();
//after about 8 seconds the Watchdog Interupt will progress the code to the disable sleep command
    sleep_disable();             
}

void watchdogSET()
{
    wdt_reset();   //reset watchdog
    WDTCSR |= 0b00011000; 
    WDTCSR = 0b00100001;
    WDTCSR = WDTCSR | 0b01000000;  //put watchdog in interupt mode (interupt will happen every 8 seconds)
    wdt_reset();   //reset watchdog
}

void sleepyTEXT()
{
         // turn off interupts durring sesnsor read and transmission noInterrupts ();
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(SENSORVCC1_PIN, HIGH);  //pin #5 powers 5V to sensor
        pinMode(SENSORVCC1_PIN, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(SENSORADC1_PIN);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(SENSORADC1_PIN);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(SENSORVCC1_PIN, LOW); // turn off sensor
        readinches_one = (10*(readvalue_one-102))/(8180/(10000/((EEPROM.read(20)*4)/12))); //converts to inches
    
        if (readvalue_one > alarm_one) {
           // if the sensor is over height
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize
// Power On GSM SHIELD          
  //            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
  //            pinMode(7, OUTPUT);
  //            digitalWrite(7, LOW); // turn off power signal

lte.begin(lteSerial, 9600);   //begin lte communication
            delay(1000); //wait for power signal to work   
          

// Connect to GSM network
            while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
        
         
        //CONSTRUCT ALARM TEXT HERE                                
          String string_text = "ALARM! TANK:";
          string_text +=" (1) ";
          string_text +=readinches_one;
          string_text +="inches";

          //turn string of settings into a character array so it can be sms'd
          string_text.toCharArray(char_alarmtext,100);                     
//Send SMS                                              
        sms.beginSMS(remoteNumber);
        sms.print(char_alarmtext);
        sms.endSMS();
                                        
        sms.beginSMS(remoteNumber_two);
        sms.print(char_alarmtext);
        sms.endSMS();                                

        string_text = "";

        gsmAccess.shutdown(); //turn off GSM once text sent
        notConnected = true;                                    
        delay(4000);
        
//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
    }  //end hourly text 
} //end sleepytext void


void dailyTEXT()
{
// turn off interupts durring sesnsor read and transmission noInterrupts ();
         
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    
        
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(SENSORVCC1_PIN, HIGH);  //pin five powers 5V to sensor
        pinMode(SENSORVCC1_PIN, OUTPUT);
        delay(6000); //wait for sensor signal to normalize    
        readfresh = analogRead(SENSORADC1_PIN);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(SENSORADC1_PIN);  // read a sensor from analog pin 0 // A0 = pin 14
        digitalWrite(SENSORVCC1_PIN, LOW); // turn off sensor
        readinches_one = (10*(readvalue_one-102))/(8180/(10000/((EEPROM.read(20)*4)/12))); //converts to inches
    
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize


   //CONSTRUCT REPORT TEXT HERE
          String string_text = "TANK GAUGE:";
  if(EEPROM.read(30)!=0){
          string_text +=" (1) ";
          string_text +=readinches_one;
  }
  if(EEPROM.read(31)!=0){
          string_text +=" (2) ";
          string_text +=readinches_two;
  } 
  if(EEPROM.read(32)!=0){
          string_text +=" (3) ";
          string_text +=readinches_three;
  } 
          //turn string of settings into a character array so it can be sms'd
          string_text.toCharArray(char_reporttext,100);
  

//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
      //end hourly text 
}


  
void defineSETTINGS()
{
    ticks_per_report = (((EEPROM.read(0))*60*60/8)-225);  //subtract 30 minutes to account for shifts  
  //tank one variables - maybe use a FOR statement in the future

   //subtract height of sensor from alarm height to adjust for actual reading
  alarm_one = ((((EEPROM.read(10)-EEPROM.read(40))*(8180/(10000/((EEPROM.read(20)*4)/12))))/10)+102); //converts from inches to arduino value.
}

  
ISR(WDT_vect)
{
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_report ++; //seperate tick total for each day
}
