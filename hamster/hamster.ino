/*
 *  Hamster Tracker
 *  This program counts the rotations of a hamster wheel using a hall effect sensor. It
 *  then uploads timestamped data to Cosm website to be analyzed.
 *
 * Author: Dave LeRoy
 * Source: https://github.com/dleroy/Arduino.git
 * Version: $Id:$
 */
#include <SPI.h>
#include <WiFi.h>
#include <HttpClient.h>
#include <Cosm.h>
#include <Time.h>
#include <stdlib.h>
#include "config.h"

// Configuration constants
#define TRUE                 1
#define FALSE                0
#define PROCESSING_INTERVAL  20000        // 20 secs by default
#define MAX_BANK_SAMPLES       256       // sizing of the banks
#define WHEEL_CIRC           18.75       // hamster wheel circumference in inches
#define INIT_FASTEST_LAP     0xffffffff  // initial fastest lap in milliseconds for comparison

// Constants used in stats calculations
#define INCHES_PER_MILE         63360
#define INCHES_PER_METER        39.3700787
#define MSEC_PER_REV_CONV       1065.3409       // conversion factor to go from millis/rev to miles per hour

//////////////////////////////////////////////////////////////////////////////////////////

// Cosm related constants

char cosmKey[] = API_KEY;                                          // Your Cosm key to let you upload data 

// Wifi related variable declarations
char ssid[] = MY_SSID;      //  your network SSID (name) 
char pass[] = MY_WIFIPASS;   // your network password

int status = WL_IDLE_STATUS;

// Define the strings for our datastream IDs
char lapId[] = "laps";
char milesId[] = "miles";
char timestampId[] = "timestamps";
const int bufferSize = 140;
CosmDatastream datastreams[] = {
  CosmDatastream(lapId, strlen(lapId), DATASTREAM_INT),
  CosmDatastream(milesId, strlen(milesId), DATASTREAM_FLOAT),
  CosmDatastream(timestampId, strlen(timestampId), DATASTREAM_INT)
};
// Finally, wrap the datastreams into a feed
CosmFeed feed(FEED_ID, datastreams, 3 /* number of datastreams */);

WiFiClient client;
CosmClient cosmclient(client);

unsigned long lastConnectionTime = 0;          // last time you connected to the server, in milliseconds
boolean lastConnected = false;                 // state of the connection last time through the main loop
const unsigned long postingInterval = 10*1000; //delay between updates to pachube.com

//////////////////////////////////////////////////////////////////////////////////////////

// Variables used in stats collection
int volatile currBank;                                  // which of the 2 banks is currently accumlating sensor input.
int lastBank;                                           // which of the 2 banks is currently being looked at for stats. 
unsigned long volatile bank[2][MAX_BANK_SAMPLES];       // the 2 banks to hold sample data
unsigned int volatile currBankIndex[2];                 // current location in each bank
unsigned long gLastTimestamp;                            // store last timestamp reading from prior bank to do speed calculations
int gFastestLap;                                        // store fastest lap for max speed calulations
unsigned int volatile gHigh;

// Hamster wheel statistics calculated
unsigned int   gStatTotalRevolutions;           // total hamster revolutions
unsigned int   gStatTotalMeters;               // total hamster meters
float gStatTotalMiles;                          // total hamster miles
float gStatAvgSpeed;                            // average hamster speed mph
float gStatMaxSpeed;                            // max hamster speed mph

// Time related definitions
time_t gBaseTime;                              // Time when first initialized
unsigned long gBaseMillis;                     // accompanying millis() reading when first initialized

//////////////////////////////////////////////////////////////////////////////////////////


// the setup routine runs once when you press reset:
void setup() {           
  initDataCollection();                          // Initialize collection banks and statistics
  initSerial(); 
  initWifi();                                    // Get Wifi connection established before we start collecting data.
  initTime();
  initInterrupts();                              // Interrupts disabled by this routine
  pinMode(13, OUTPUT);                           // Blink LED on each lap
}

// the loop routine runs over and over again forever:
void loop() {
  switchBanks();                   // Start collecting data in other bank
  calcStats();                     // Calculate stats on data collected in last interval
  printStats();                    // Print update stats
  sendFeedUpdate();                // Send data to Cosm
  delay(PROCESSING_INTERVAL);      // Wait while new data is collected
}

// Initialize serial interface
void initSerial() {
  Serial.begin(9600);                            // Start serial port
} 

// Initialize interrupts
void initInterrupts() {
  noInterrupts();                                // Disable interrupts until we have set time
  attachInterrupt(0, hamsterInterrupt, FALLING);  // Interrupt 0 on Mega2560 is pin 2
}

// Reset hamster statistics
void resetStatistics() {
 gStatTotalMiles = 0;
 gStatTotalRevolutions = 0;
 gStatTotalMeters = 0;
 gStatAvgSpeed = 0;
 gStatMaxSpeed = 0;
 gLastTimestamp = 0;
 gFastestLap = INIT_FASTEST_LAP;
}

// Initialize banks and statistics
void initDataCollection() {
  currBank = 0; lastBank = 1;
  currBankIndex[0] = currBankIndex[1] = 0;   // XXX switch to bank ptrs later
  resetStatistics();
}

// switch active banks. 1 always collecting data, the other being analyzed for stats update
void switchBanks() {
  noInterrupts();                // protect bank switching with lock
  gLastTimestamp = bank[currBank][currBankIndex[currBank]];    // Store last timestamp before switch banks
  currBank = (currBank + 1) % 2; // switch banks
  lastBank = (lastBank + 1) % 2; // easy reference to last bank for stats calculations
  currBankIndex[currBank] = 0;   // reset ptr to current bank to start collecting from beginning of bank
  interrupts();
}

// Interrupt handler for updating wheel statistics
void hamsterInterrupt() {
  // simply stamp time in appropriate bank/location and increment ptr
  bank[currBank][currBankIndex[currBank]++] = millis();
  if (gHigh) {
    digitalWrite(13, HIGH);
    gHigh = 0;
  } else {
    digitalWrite(13, LOW);
    gHigh = 1;
  }
}

// update the total revolutions calculation
void calcTotalRevolutions() {
  gStatTotalRevolutions += currBankIndex[lastBank];
}

// update the total miles calculation
void calcTotalMiles() {
  int revolutions = currBankIndex[lastBank];
  gStatTotalMiles +=  (revolutions * WHEEL_CIRC) / INCHES_PER_MILE;
}

// update the total meters calculation
void calcTotalMeters() {
  int revolutions = currBankIndex[lastBank];
  gStatTotalMeters +=  (revolutions * WHEEL_CIRC) / INCHES_PER_METER;
}

// update the avg speed calculation. More complicated. Ignore time when hamsters arent active.
void calcAvgSpeed() {
}

// update the max speed calculation. Current just taking fastest revolution
void calcMaxSpeed() {
}

// Calculate Statistics
void calcStats() {
   calcTotalRevolutions();
   calcTotalMiles();                  // depends on total revolution count being calculated first
   calcTotalMeters();                 // depends on total revolution count being calculated first
   calcAvgSpeed();
   calcMaxSpeed();
}

// Print Statistics
void printStats() {
  Serial.print("Total Revolutions:     ");
  Serial.println(gStatTotalRevolutions);
  Serial.print("Total Distance (meters): ");
  Serial.println(gStatTotalMeters);
  Serial.print("Total Distance (miles): ");
  Serial.println(gStatTotalMiles);
  Serial.println("");
}


// Initialize time of day
void initTime() {
  // Set current time...from some source TBD ...need to offset by 8 hours for Cosm
  setTime(20,30,00,9,3,2013);

  // mark accompanying millis() value ...good enough for what we are doing
  gBaseTime = now();
  gBaseMillis = millis();
   
  return;
}

// Initialize Wifi connection
void initWifi() {
  
  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present"); 
    // don't continue:
    while(true);
  } 
  
  // attempt to connect to Wifi network:
  while ( status != WL_CONNECTED) { 
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:    
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  } 
  // you're connected now, so print out the status:
  printWifiStatus();
}

void sendFeedUpdate() {

  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  while (client.available()) {
    char c = client.read();
    Serial.print(c);
  }

  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }

  // if you're not connected, and ten seconds have passed since
  // your last connection, then connect again and send data:
  if(!client.connected() && (millis() - lastConnectionTime > postingInterval)) {
    sendHamsterData();               // our method to send datastreams to Cosm
  }
  // store the state of the connection for next time through
  // the loop:
  lastConnected = client.connected();
}

// this method sends datastreams to Cosm using Cosm Client
void sendHamsterData() {

  // put Laps into datastream 0
  datastreams[0].setInt(currBankIndex[lastBank]);
  datastreams[1].setFloat(gStatTotalMiles);
  // Put normal datastream updates first
  int ret = cosmclient.put(feed, cosmKey);
  if (ret < 0) {
    // log error
    Serial.print("Error putting data to Cosm: ");
    Serial.println(ret*-1);
  }

  // If there were any new points, then updated timestamped points
  if (currBankIndex[lastBank]) {
    String pointsJSON = "";
    dataPointstoJSON(pointsJSON);
    int ret2 = cosmclient.putPoints(feed, pointsJSON, cosmKey);
      if (ret2 < 0) {
      // log error
      Serial.print("Error putting points to Cosm: ");
      Serial.println(ret2*-1);
    }
  }
  lastConnectionTime = millis();
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

// Given a millisecond count, generate valid Cosm timestamp for datapoint update
// e.g "2013-03-07T11:01:43.235Z"
int GenerateTimeString(String& timestring, unsigned long msec) {
  unsigned long msecdiff;
  unsigned long timenow;
  tmElements_t tm;

  if (msec < gBaseMillis) {
    Serial.println("Invalid msec reading passed to GenerateTimeString");
    Serial.print("msec: ");
    Serial.println(msec);
    Serial.print("basemsec: ");
    Serial.println(gBaseMillis);
    return -1;
  } else {
    msecdiff = msec - gBaseMillis;
  }
  
  // Calculate new timenow based on elapsed seconds from base millis() reading
  timenow = (unsigned long)gBaseTime + msecdiff/1000;

  // get msec remainder
  msecdiff = msecdiff - ((msecdiff/1000) * 1000);

  breakTime((time_t)timenow, tm);
  timestring.concat(tmYearToCalendar(tm.Year));
  timestring += "-";
  timestring.concat(tm.Month);
  timestring += "-";
  timestring.concat(tm.Day);
  timestring += "T";
  timestring.concat(tm.Hour);
  timestring += ":";
  timestring.concat(tm.Minute);
  timestring += ":";
  timestring.concat(tm.Second);
  timestring += ".";
  timestring.concat(msecdiff);
  timestring += "Z";
 
  return 0;
}

// Print last collected datapoints in JSON format for COSM datastream update
// e.g.
//
// {
//  "datapoints":[
//    {"at":"2013-03-07T11:01:43Z","value":"294"},
//
//   ]
// }
void dataPointstoJSON(String& points) {

  int i;
  
  if (currBankIndex[lastBank]) {
    points = "{\n  \"datapoints\":[\n";

    for(i=0; i< currBankIndex[lastBank]; i++) {
      String timestring = String("");
      if (GenerateTimeString(timestring, bank[lastBank][i]) >= 0) {
        points += "    {\"at\":\"";
        points += timestring;
        if (i == (currBankIndex[lastBank] -1)) {
          points += "\",\"value\":\"1\"}\n";
        } else {
          points += "\",\"value\":\"1\"},\n";
        }
      }
    }
    points += "  ]\n";
    points += "}\n";
  }
}




