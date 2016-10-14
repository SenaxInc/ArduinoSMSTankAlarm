// import the GSM and rtos library
#include <GSM.h>
#include <avr/sleep.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

volatile int time_tick = 0;

const int sleep_hours = 1;
const int ticks_per_sleep = (sleep_hours*60*60)/8;
const int ticks_per_day = 10800;

// char array of the telephone number to send SMS
char remoteNumber[20]= "1918XXXXXXX";

// char array of the message
char txtMsg[200]="High Tank Alarm - Testing 1 2 3";

// connection state   -- JWW doesnt know what this does
boolean notConnected = true;

// this is the threshold used for reading -- JWW sensor trigger
const int threshold = 500;

void setup() {
  //always off power saving settings
  power_spi_disable(); //SPI off
  SPCR = 0; //disable SPI
  power_twi_disable(); //TWI off
  
  //power on first contact
  int readvalue = analogRead(A0);
  sms.beginSMS(remoteNumber);
  sms.print("Power On",readvalue);
  sms.endSMS();
  gsmAccess.shutdown();

  watchdogSET();  //define watchdog settings
  
  //sleep
    //sleepy time power saving settings
        //ADC off
    power_adc_disable(); //disable the clock to the ADC module
    ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
              ???            //can USART be turned off here?
}

void loop() {
  tickSleep();    
  if (time_tick > ticks_per_sleep) {
  
  noInterrupts (); // turn off interupts durring sesnsor read and transmission
  
   
    //prepare to read sensor
        ADCSRA |= (1<<ADEN); //ADC hex code set to on
        power_adc_disable(); //enable ADC module
    
//power up sensor - GSM shield uses pins 0,1,2,3,7 + 8 for mega, 10 for yun 
digitalWrite(5, HIGH);  //pin five powers 5V to sensor
pinMode(5, OUTPUT);
    delay(6000); //wait for sensor signal to normalize
       // read a sensor from analog pin 0
       int readvalue = analogRead(A0);

    // turn off sensor
digitalWrite(5, LOW);

           // if the sensor is over height 
       if (readvalue > threshold) {{
      // prepare to send SMS
             ???  //turn on USART to be ready for GSM
              delay(6000);    //delay to normalize
               // Start GSM SHIELD
      // If your SIM has PIN, pass it as a parameter of begin() in quotes
      while(notConnected) {
        if(gsmAccess.begin(PINNUMBER)==GSM_READY)
          notConnected = false;
        else
        {
          delay(1000);
        }
      }
    }
  sms.beginSMS(remoteNumber);
  sms.print(readvalue);
  sms.endSMS();
  gsmAccess.shutdown();
  // would like to include something here to make arduino sleep for an hour
                                   //Turn off ADC
        power_adc_disable(); //disable the clock to the ADC module
    ADCSRA &= ~(1<<ADEN);  //ADC hex code set to off
                                   //Turn off USART
Interrupts (); //turn interupts back on
                               
                                   
   if (time_tick > ticks_per_day) {   //daily text tigger

     
     time_tick = 0;  //daily tick reset
 }
}

    
void tickSleep()   
{
set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
sleep_enable();
sleep_mode();
//after about 8 seconds the Watchdog Interupt will progress the code to the disable sleep command
sleep_disable();             
}

void watchdogSET() {
  MCUSR = MCUSR & B11110111;  //reset watchdog
  WDTCSR = WDTCSR | B00011000; 
  WDTCSR = B00100001;
  WDTCSR = WDTCSR | B01000000;  //put watchdog in interupt mode (interupt will happen every 8 seconds)
  MCUSR = MCUSR & B11110111;  //reset watchdog
}

ISR(WDT_vect)
{
time_tick ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
}
