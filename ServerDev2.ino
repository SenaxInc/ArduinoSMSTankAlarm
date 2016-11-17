#include <SPI.h>
#include <Ethernet2.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <GSM.h>

// PIN Number
#define PINNUMBER ""

// initialize the library instance
GSM gsmAccess;
GSM_SMS sms;

// MAC address from Ethernet shield sticker under board
byte mac[] = { 0x90, 0xA2, 0xDA, 0x10, 0xD1, 0x71 };
EthernetServer server(2001);  // forward ports 80 and 8080 to internal port 2001

volatile int time_tick_check = 1;
volatile int time_tick_nosignalalarm = 1;
volatile int time_tick_daily = 1;
int timeout = 1;

int ticks_per_check = 45;

String string_latest_readvalue_silas_sw;
int latest_readvalue_silas_sw = 1;
const int silas_sw_height = 400; 
const int silas_sw_alarm = 300;

boolean notConnected = true;

void setup()
{
    Ethernet.begin(mac);  // initialize Ethernet device
    server.begin();           // start to listen for clients

    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    
    watchdogSET();
}

void loop()
{
    EthernetClient client = server.available();  // try to get client

    if (client) {  // got client?
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {   // client data available to read
                char c = client.read(); // read 1 byte (character) from client
                // last line of client request is blank and ends with \n
                // respond to client only after last line received
                if (c == '\n' && currentLineIsBlank) {
                    // send a standard http response header
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close");
                    client.println();
                    // send web page
                    client.println("<!DOCTYPE html>");
                    client.println("<html>");
                    client.println("<head>");
                    client.println("<title>Senax Tank Batteries</title>");
                    client.println("</head>");
                    client.println("<body>");
                    client.println("<table height=400 cellspacing=0 cellpadding=10 width=95% bgcolor=#DFCAA9 border=0 align=center><tr><td><b>Silas Lease</b><br><br>Data received:<b>");
                    client.println("11/05/2016 11:32");
                    client.println("<br><br>24Hr Prod:<br><center>");
                    client.println("9.4BBL");
                    client.println("</center><br><br>MTD: ");
                    client.println("280 BBL");
                    client.println("</td><td><table height=95% cellspacing=0 cellpadding=10 width=95% bgcolor=#FFFFFF border=1 align=center><tr height=99%><td><table cellspacing=0 border=0 height=100% width=100%><tr><td bgcolor=green></td><tr height=");
                    client.println(100*latest_readvalue_silas_sw/silas_sw_height);
                    client.println("%><td bgcolor=blue></td></tr></table></td><td><table border=1 height=100% width=100%><tr><td></td><tr></tr></table></td><td><table border=1 height=100% width=100%><tr><td></td><tr></tr></table></td></tr><tr><td><center>");
                    client.println(latest_readvalue_silas_sw);
                    client.println("</center></td><td><center>");
                    client.println("2ft  6in");
                    client.println("</center></td><td><center>");
                    client.println("1ft  6in");
                    client.println("</center></td></tr></table></td></tr></table>");
                    client.println("</body>");
                    client.println("</html>");
                    break;
                }
                // every line of text received from the client ends with \r\n
                if (c == '\n') {
                    // last character on line of received text
                    // starting new line with next character read
                    currentLineIsBlank = true;
                } 
                else if (c != '\r') {
                    // a text character was received from client
                    currentLineIsBlank = false;
                }
            } // end if (client.available())
        } // end while (client.connected())
        delay(1);      // give the web browser time to receive the data
        client.stop(); // close the connection
    } // end if (client)

if (time_tick_check > ticks_per_check) {
  check_sms();
}

    
} // end of loop


void check_sms()
{
            wdt_disable();
    
      // Power On GSM SHIELD          
            digitalWrite(7, HIGH);  //pin seven powers on GSM shield
            pinMode(7, OUTPUT);
            delay(100); //wait for power signal to work   
            digitalWrite(7, LOW); // turn off power signal
    
                while(notConnected) {  //when not connected check for connection
                if(gsmAccess.begin(PINNUMBER)==GSM_READY) //check for a GSM connection to network
                {
                   notConnected = false;   //when connected, move on 
                   timeout = 0;
                }
                else if (timeout > 20)
                {
                 notConnected = false;   
                }    
                else
                {
                    delay(1000); //if not connected, wait another second to check again
                    timeout ++;
                }
                }
      if (sms.available()>0) {
          string_latest_readvalue_silas_sw = sms.readString();          
          latest_readvalue_silas_sw = string_latest_readvalue_silas_sw.toInt();
          string_latest_readvalue_silas_sw = "";
          sms.flush();
      }
    time_tick_check = 1;
            gsmAccess.secureShutdown(); //turn off GSM once text sent
            notConnected = true; 
            watchdogSET(); 
}

void watchdogSET()
{
    wdt_reset();   //reset watchdog
    WDTCSR |= 0b00011000; 
    WDTCSR = 0b00100001;
    WDTCSR = WDTCSR | 0b01000000;  //put watchdog in interupt mode (interupt will happen every 8 seconds)
    wdt_reset();   //reset watchdog
}

ISR(WDT_vect)
{
    time_tick_check ++; //for each Watchdog Interupt, adds 1 to the number of 8 second ticks counted so far
    time_tick_daily ++; //seperate tick total for each day
    time_tick_nosignalalarm ++; //seperate tick total for alarm signal loss countdown
}
