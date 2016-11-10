
#include <SPI.h>
#include <Ethernet.h>

// MAC address from Ethernet shield sticker under board
byte mac[] = { 0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX };
IPAddress ip(192, 168, XXX, XXX); // IP address, may need to change depending on network
EthernetServer server(XXXX);  // us port forwarding on router, 80 is usually blocked by home ISPs

void setup()
{
    Ethernet.begin(mac, ip);  // initialize Ethernet device
    server.begin();           // start to listen for clients
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
                    client.println("30");
                    client.println("%><td bgcolor=blue></td></tr></table></td><td><table border=1 height=100% width=100%><tr><td></td><tr></tr></table></td><td><table border=1 height=100% width=100%><tr><td></td><tr></tr></table></td></tr><tr><td><center>");
                    client.println("4ft  6in");
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
}
