// import the GSM and rtos library
#include <GSM.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <EEPROM.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

volatile int time_tick_hours = 1; //start tick count at 1
volatile int time_tick_report = 1; //start tick count at 1

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
int ticks_per_report = 10575;  //23.5 hours of 8 second ticks to account for shifts in ticks total


// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";
char receivedNumber[20]; //does sms only recognize "remoteNumber" char?

// char array of the message
char char_reporttext[50]; //text sent with each report
char char_alarmtext[50]; //text sent when alarm triggered
char char_currentsettings[100]; //is length required?

String string_settingtext_raw;
String stringOne = "Power ON. Height = ";  //not sure why, but string causes output of 1 in text message

// set connection state variable
boolean notConnected = true;

// this is the threshold used for reading
int readvalue_one;
int contents_one;
int constant_one;
int toground_one;
int alarm_one = 310; //default value for 5 feet

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

  
//power up and read sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(5, HIGH);  //pin #5 powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A1);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(5, LOW); // turn off sensor
  
  
  // Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
  
  //connect to network
              while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
  sms.beginSMS(remoteNumber);
  sms.print(readvalue_one); 
  sms.endSMS();
  delay(10000);
  //READ RECIEVED TEXTS HERE

  receiveSETTINGS();
  
  //SHUTDOWN GSM
  gsmAccess.shutdown();
  notConnected = true;


//SET START UP EEPROM VARIABLES  
  
//On Startup - DEFINE ALARM TRIGGER FROM EEPROM DATA


contents_one = EEPROM.read(30);

if(contents_one == 1)
{
  constant_one = (EEPROM.read(20))*4*10; //stored as XXX0 instead of X.XX to use int variable
}
if(contents_one == 2)
{
  constant_one = 3000+((EEPROM.read(20))*4); //stored as 3XXX instead of 3.XXX to use int variable
}
//subtract height of sensor from alarm height to adjust for actual reading
alarm_one = ((constant_one*(EEPROM.read(10)-EEPROM.read(40))/1000)+114); //also convert from inches to arduino value. divide by 1000 before add 114
  
ticks_per_report = (((EEPROM.read(0))*60*60/8)-225);  //subtract 30 minutes to account for shifts
  
  
watchdogSET();  //define watchdog settings
  

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

else{  //if day has not elapsed then check hourly ticks
  //hourly wake up to check for alarm trigger
        if (time_tick_hours > ticks_per_sleep) {  //if number of ticks has reach hour goal send text 
  wdt_disable();              
            sleepyTEXT();
                                    
            time_tick_hours = 1;   //rest ticks
  watchdogSET();                                    
        } //end hourly text/check
    }
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
        digitalWrite(5, HIGH);  //pin #5 powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(10000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A1);  // read a sensor from analog pin #A1
        delay(1000);
        digitalWrite(5, LOW); // turn off sensor
    
        if (readvalue_one > alarm_one) {{ // if the sensor is over height
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize
// Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
          

// Connect to GSM network
            while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
        }
         
        //CONSTRUCT ALARM TEXT HERE                                
                                        
//Send SMS                                              
        sms.beginSMS(remoteNumber);
        sms.print(readvalue_one);
        sms.endSMS();
                                        
        //Check for settings messages here
        receiveSETTINGS();                          
                                  
        gsmAccess.shutdown(); //turn off GSM once text sent
        notConnected = true;                                    
        delay(4000);
//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
    }  //end hourly text 
}


void dailyTEXT()
{
         // turn off interupts durring sesnsor read and transmission noInterrupts ();
//prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_enable(); //enable ADC module    
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
        digitalWrite(5, HIGH);  //pin five powers 5V to sensor
        pinMode(5, OUTPUT);
        delay(6000); //wait for sensor signal to normalize    
        readfresh = analogRead(A1);  //dummy read to refresh adc after wake up
        delay(2000);
        readvalue_one = analogRead(A0);  // read a sensor from analog pin 0 // A0 = pin 14
        digitalWrite(5, LOW); // turn off sensor
    
// prepare to send SMS
            //turn on USART to be ready for GSM
          
            delay(6000);    //delay to normalize
// Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(500); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
          

// Connect to GSM network
            while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                   notConnected = false;   //when connected, move on 
                else {
                      delay(1000); //if not connected, wait another second to check again
                }
            }
   //CONSTRUCT REPORT TEXT HERE
          String string_currentsettings = "TANK GAUGE:";
  if(EEPROM.read(30)!=0){
          string_currentsettings +=" (1) ";
          string_currentsettings +=readvalue_one;
  }
    if(EEPROM.read(31)!=0){
          string_currentsettings +=" (2) ";
          string_currentsettings +=readvalue_two;
  } 
      if(EEPROM.read(32)!=0){
          string_currentsettings +=" (3) ";
          string_currentsettings +=readvalue_three;
  } 
          //turn string of settings into a character array so it can be sms'd
          string_currentsettings.toCharArray(char_currentsettings,100);
  
//Send SMS                                    
        sms.beginSMS(remoteNumber);
        sms.print(readvalue_one); //INCLUDE CURRENT EEPROM SETTINGS IN TEXT
        sms.endSMS();
  
        //Check for settings messages here
        receiveSETTINGS();     
  
        gsmAccess.shutdown(); //turn off GSM once text sent
        notConnected = true;                                    
        delay(4000);
//prepare for sleep
        power_adc_disable(); //disable the clock to the ADC module
        ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
        //turn interupts back on interrupts (); 
      //end hourly text 
}

void receiveSETTINGS()
{
   //READ RECIEVED TEXTS HERE
  while (sms.available() > 0)
  {
        if(sms.peek() == 'A') //A = ALARM
    {
          //extract text stream into string
          string_settingtext_raw = sms.readString();  
          
          //delete "A" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "A" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
          
          //convert the remaining digits in the string to the trigger intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(9+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
            if(sms.peek() == 'C') //C = Constant
    {
          //extract text stream into string
          string_settingtext_raw = sms.readString();  
          
          //delete "C" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "C" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
              
          //read digit after tank number to get tank contents code number (1 or 2)
          settingtext_tankcontents = string_settingtext_raw.charAt(1);
          
          //delete contents code from string
          string_settingtext_raw.remove(1,1); 

          //WRITE content code NUMBER TO EEPROM HERE
          EEPROM.update(29+settingtext_tanknumber, settingtext_tankcontents);     
          
          //wait for eeprom just for fun
          delay(1000);
              
          //convert the remaining digits in the string to the constant intiger
          //divide by 4 to store in eeprom
          settingtext_value = (string_settingtext_raw.toInt())/4;
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW psi constant TO EEPROM HERE
          EEPROM.update(19+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
            if(sms.peek() == 'T') //T = time between reports
    {
          //extract text into string
          string_settingtext_raw = sms.readString();  
          
          //delete "T" from begining of string here
          string_settingtext_raw.remove(1,1);
          
          //convert the remaining digits in the string to the trigger intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(0, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
                if(sms.peek() == 'H')
    {
          //extract text into string
          string_settingtext_raw = sms.readString();  
          
          //delete "H" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "H" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //delete tank number from begining of string here
          string_settingtext_raw.remove(1,1); 
          
          //convert the remaining digits in the string to the height intiger
          settingtext_value = string_settingtext_raw.toInt();
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //WRITE NEW TRIGGER NUMBER TO EEPROM HERE
          EEPROM.update(39+settingtext_tanknumber, settingtext_value); 
          
          //wait for eeprom just for fun
          delay(1000);
    }
                    if(sms.peek() == 'S') //Request for Currrent Settings for tank one would = S1
    {
          //extract text into string
          string_settingtext_raw = sms.readString();  
          
          //delete "H" from begining of string here
          string_settingtext_raw.remove(1,1);

          //READ NEXT DIGIT AFTER "H" TO ASSIGN TO TANK # 1-9
          settingtext_tanknumber = string_settingtext_raw.charAt(1);
          
          //clear out string for fun
          string_settingtext_raw = "";          

          //CREATE STRING OF CURRENT EEPROM SETTINGS BASED ON TANK NUMBER
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
                      
          //turn string of settings into a character array so it can be sms'd
          string_currentsettings.toCharArray(char_currentsettings,100);
                      
          //wait for eeprom just for fun
          delay(1000);            
          
          //SEND TEXT WIHT SETTINGS TO NUMBER TEXT RECEIVED FROM
          sms.remoteNumber(receivedNumber, 20); //define phone number as number text received from
          sms.beginSMS(receivedNumber);
          sms.print(char_currentsettings); //INCLUDE CURRENT EEPROM SETTINGS IN TEXT
          sms.endSMS();       

          //clear out string for fun
          string_currentsettings = "";                   
                      
          //wait for eeprom just for fun
          delay(1000);
    }
              //delete text
          sms.flush();
  } 
}
  
  
ISR(WDT_vect)
{
    time_tick_hours ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_report ++; //seperate tick total for each day
}
