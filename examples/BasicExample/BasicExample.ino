/*
   Test Arduino Manager for iPad / iPhone / Mac

   A simple test program to show the Arduino Manager
   features.

   Author: Fabrizio Boco - fabboco@gmail.com

   Version: 1.0

   07/16/2022

   All rights reserved

*/

/*

   AMController libraries, example sketches (The Software) and the related documentation (The Documentation) are supplied to you
   by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation
   constitutes acceptance of these terms.
   If you do not agree with these terms, please do not use or install The Software.
   The Author grants you a personal, non-exclusive license, under authors copyrights in this original software, to use The Software.
   Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any
   patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
   The Software and the Documentation are provided by the Author on an AS IS basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
   LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION
   ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
   REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
   STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#include <SD.h>
#include <AM_PicoWiFi.h>
#include "arduino_secrets.h"


#define SPI_MOSI    7
#define SPI_MISO    4
#define SPI_SCK     6
#define SD_SELECT   5

/*

   WIFI Library configuration

*/
IPAddress ip(192, 168, 1, 23);
IPAddress dns(8, 8, 8, 8);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

char ssid[] = SECRET_SSID;  // your network SSID (name) i.g. "MYNETWORK"
char pass[] = SECRET_PASS;  // your network password i.g. "MYPASSWORD"

int status = WL_IDLE_STATUS;

WiFiServer server(80);

/**
   Other initializations
*/
#define YELLOWLEDPIN 14
int yellowLed = HIGH;

#define BLUELEDPIN 15
int blueLed = 30;

#define CONNECTIONLEDPIN 16

#define POTENTIOMETERPIN 26
int pot;

float temperature;

unsigned long lastSample = 0;

/*
   Callbacks Prototypes
*/
void doWork();
void doSync();
void processIncomingMessages(char *variable, char *value);
void processOutgoingMessages();
void processAlarms(char *variable);
void deviceConnected();
void deviceDisconnected();

/*

   AMController Library initialization

*/
#ifdef ALARMS_SUPPORT
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &processAlarms, &deviceConnected, &deviceDisconnected);
#else
AMController amController(&server, &doWork, &doSync, &processIncomingMessages, &processOutgoingMessages, &deviceConnected, &deviceDisconnected);
#endif

void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.println("Start");

#if (defined(ALARMS_SUPPORT) || defined(SD_SUPPORT))
  Serial.println("Initializing SD card...");

  SPI.setRX(SPI_MISO);
  SPI.setCS(SD_SELECT);
  SPI.setSCK(SPI_SCK);
  SPI.setTX(SPI_MOSI);

  // see if the card is present and can be initialized:
  if (!SD.begin(SD_SELECT)) {
    Serial.println("Card failed, or not present");
  }
  else {
    Serial.println("Card initialized");
  }

  delay(1000);
#endif

  // attempt to connect to Wifi network

  WiFi.config(ip, dns, gateway, subnet);
  while (status != WL_CONNECTED) {

    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network
    status = WiFi.begin(ssid, pass);

    // wait 2 seconds for connection:
    delay(2000);
  }

  // print your WiFi shields IP address

  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

#if defined(ALARMS_SUPPORT)

  // Set a new NTP Server
  //
  // Choose your NTP Server here: www.pool.ntp.org
  //
  amController.setNTPServerAddress(IPAddress(195, 186, 4, 101));

#endif

  /**
     Other initializations
  */

  // Yellow LED on
  pinMode(YELLOWLEDPIN, OUTPUT);
  digitalWrite(YELLOWLEDPIN, yellowLed);

  // Blue LED
  pinMode(BLUELEDPIN, OUTPUT);
  analogWrite(BLUELEDPIN, blueLed);

  pinMode(POTENTIOMETERPIN, INPUT);

  // Red LED OFF
  pinMode(CONNECTIONLEDPIN, OUTPUT);
  digitalWrite(CONNECTIONLEDPIN, LOW);

#ifdef SDLOGGEDATAGRAPH_SUPPORT

  if (amController.sdFileSize("TodayT") == 0 || amController.sdFileSize("TodayT") > 2000) {
    amController.sdPurgeLogData("TodayT");
    Serial.println("TodayT purged");
    amController.sdLogLabels("TodayT", "T");
  }

#endif

  Serial.println("Ready");
}

/**

   Standard loop function

*/
void loop() {
  //amController.loop();
  amController.loop(70);
}

/**
  This function is called periodically and its equivalent to the standard loop() function
*/
void doWork() {
  //Serial.println(doWork);
  temperature = analogReadTemp();
  digitalWrite(YELLOWLEDPIN, yellowLed);
  //servo.write(servoPos);
  pot = analogRead(POTENTIOMETERPIN);

#if defined(SDLOGGEDATAGRAPH_SUPPORT)
  // Every 5 Minutes (300000 ms)
  if ( (millis() - lastSample) > 300000) {
    lastSample = millis();
    Serial.println("Temperature stored");
    amController.sdLog("TodayT", amController.now(), temperature);
  }
#endif
}


/**
  This function is called when the ios device connects and needs to initialize the position of switches and knobs
*/
void doSync () {
  amController.writeMessage("Knob1", blueLed);
  amController.writeMessage("S1", yellowLed);
  amController.writeTxtMessage("Msg", "Hello, I'm your Raspberry Pico Pi W device!");
}

/**
  This function is called when a new message is received from the iOS device
*/
void processIncomingMessages(char *variable, char *value) {
  Serial.print(variable); Serial.print(" : "); Serial.println(value);
  if (strcmp(variable, "S1") == 0) {
    yellowLed = atoi(value);
  }

  if (strcmp(variable, "Knob1") == 0) {
    blueLed = atoi(value);
    analogWrite(BLUELEDPIN, blueLed);
  }

  if (strcmp(variable, "Push1") == 0) {
    amController.temporaryDigitalWrite(CONNECTIONLEDPIN, LOW, 500);
  }

  if (strcmp(variable, "Cmd_01") == 0) {
    amController.log("Command: "); amController.logLn(value);
    Serial.print("Command: "); Serial.println(value);
  }

  if (strcmp(variable, "Cmd_02") == 0) {

    amController.log("Command: "); amController.logLn(value);

    Serial.print("Command: ");
    Serial.println(value);
  }

}

/**
  This function is called periodically and messages can be sent to the iOS device
*/
void processOutgoingMessages() {
  amController.writeMessage("T", temperature);
  amController.writeMessage("Led", yellowLed);
  amController.writeMessage("Pot", getVoltage(POTENTIOMETERPIN));
}

/**
  This function is called when a Alarm is fired
*/
void processAlarms(char *alarm) {
  Serial.print(alarm); Serial.println(" fired");
  //servoPos = 0;
}

/**
  This function is called when the iOS device connects
*/
void deviceConnected () {
  Serial.println("Device connected");
  digitalWrite(CONNECTIONLEDPIN, HIGH);
}

/**
  This function is called when the iOS device disconnects
*/
void deviceDisconnected () {
  Serial.println("Device disconnected");
  digitalWrite(CONNECTIONLEDPIN, LOW);
}

/**
  Auxiliary functions
*/

/*
  getVoltage()  returns the voltage on the analog input defined by pin
*/
float getVoltage(int pin) {
  return (analogRead(pin) * 3.3 / 1023.0);
}