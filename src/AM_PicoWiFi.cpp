/*
   AMController libraries, example sketches (“The Software”) and the related documentation (“The Documentation”) are supplied to you
   by the Author in consideration of your agreement to the following terms, and your use or installation of The Software and the use of The Documentation
   constitutes acceptance of these terms.
   If you do not agree with these terms, please do not use or install The Software.
   The Author grants you a personal, non-exclusive license, under author's copyrights in this original software, to use The Software.
   Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by the Author, including but not limited to any
   patent rights that may be infringed by your derivative works or by other works in which The Software may be incorporated.
   The Software and the Documentation are provided by the Author on an "AS IS" basis.  THE AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT
   LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION
   ALONE OR IN COMBINATION WITH YOUR PRODUCTS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
   REPRODUCTION AND MODIFICATION OF THE SOFTWARE AND OR OF THE DOCUMENTATION, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
   STRICT LIABILITY OR OTHERWISE, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Author: Fabrizio Boco - fabboco@gmail.com

   All rights reserved

*/

#include "AM_PicoWiFi.h"

#ifdef DEBUG
#define LEAP_YEAR(Y)     ( ((1970+Y)>0) && !((1970+Y)%4) && ( ((1970+Y)%100) || !((1970+Y)%400) ) )

static  const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // API starts months from 1, this array starts from 0
#endif

char *dtostrf (double val, signed char width, unsigned char prec, char *sout);

#if defined(ALARMS_SUPPORT)

bool check(uint8_t *pRecord, void *pData) {
  Alarm a;
  memcpy(&a, pRecord, sizeof(a));
  if (strcmp(a.id, (char *)pData) == 0)
    return true;
  return false;
}

#endif

#if defined(ALARMS_SUPPORT)

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
#if defined(ALARMS_SUPPORT)
                           void (*processAlarms)(char *alarm),
#endif
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void)
                          ) : AMController(server, doWork, doSync, processIncomingMessages, processOutgoingMessages, deviceConnected, deviceDisconnected) {
#ifdef ALARMS_SUPPORT
  _alarmFile = "alarms1.txt";
  //_timeServerAddress = IPAddress(64,90,182,55);  // New York City, NY NTP Server nist1-ny.ustiming.org
  _timeServerAddress = IPAddress(129, 6, 15, 28); // time.nist.gov

  _processAlarms = processAlarms;
  _startTime = 0;
  _sendNtpRequest = false;
  _lastAlarmCheck = 0;
#endif
}
#endif

AMController::AMController(WiFiServer *server,
                           void (*doWork)(void),
                           void (*doSync)(void),
                           void (*processIncomingMessages)(char *variable, char *value),
                           void (*processOutgoingMessages)(void),
                           void (*deviceConnected)(void),
                           void (*deviceDisconnected)(void)
                          ) {
  _var = true;
  _idx = 0;
  _server = server;
  _doWork = doWork;
  _doSync = doSync;
  _processIncomingMessages = processIncomingMessages;
  _processOutgoingMessages = processOutgoingMessages;
  _deviceConnected = deviceConnected;
  _deviceDisconnected = deviceDisconnected;
  _initialized = false;
  _pClient = NULL;

  _variable[0] = '\0';
  _value[0]    = '\0';

#ifdef ALARMS_SUPPORT
  _processAlarms = NULL;
#endif
}

void AMController::loop() {
  this->loop(20);
}

void AMController::loop(unsigned long _delay) {
  if (!_initialized) {
    _initialized = true;
    _server->begin();
    delay(1500);
  }
#ifdef ALARMS_SUPPORT

  if ( (millis() / 1000 < 20 && _startTime == 0) || _sendNtpRequest) {
    this->syncTime();
    _startTime = 100;
  }

  if ( _udp.parsePacket() ) {
    this->readTime();
  }

  if (_processAlarms != NULL) {
    unsigned long now = _startTime + millis() / 1000;
    if ( (now - _lastAlarmCheck) > ALARM_CHECK_INTERVAL) {
      _lastAlarmCheck = now;
      this->checkAndFireAlarms();
    }
  }

#endif

  _doWork();

  WiFiClient localClient = _server->available();
  _pClient = &localClient;

  if (localClient) {
    PRINTLN("Client connected");
    // Client connected

    if (_deviceConnected != NULL) {
      delay(250);
      _deviceConnected();
      delay(250);
    }

    while (_pClient->connected()) {

      // Read incoming messages if any
      this->readVariable();

      if (strlen(_value) > 0 && strcmp(_variable, "Sync") == 0) {
        // Process sync messages for the variable _value
        _doSync();
      }
      else {
        // Manages Alarm creation and update requests

#ifdef ALARMS_SUPPORT
        if (strlen(_value) > 0 && strcmp(_variable, "$AlarmId$") == 0) {
          _variable[0] = '\0';
          strcpy(this->_id, _value);
          PRINT("$AlarmId$: "); PRINTLN(this->_id);
        } else if (strlen(_value) > 0 && strcmp(_variable, "$AlarmT$") == 0) {
          _variable[0] = '\0';
          this->_time = atol(_value);
          PRINT("$AlarmT$: "); PRINTLN(this->_time);
        }
        else if (strlen(_value) > 0 && strcmp(_variable, "$AlarmR$") == 0) {
          _variable[0] = '\0';
          if (this->_time == 0) {
            PRINT("Deleting Alarm "); PRINTLN(this->_id);
            this->removeAlarm(this->_id);
          }
          else {
            PRINT("Creating/Updating Alarm "); PRINT(this->_id); PRINT(" @Time: "); PRINTLN(this->_time);
            this->createUpdateAlarm(this->_id, this->_time, atoi(_value));
          }
        }
#endif
#ifdef SD_SUPPORT
        if (strlen(_variable) > 0 && strcmp(_variable, "SD") == 0) {
          PRINTLN("List of Files");
          File root = SD.open("/");
          if (!root) {
            PRINTLN("Failed to open /");
            return;
          }

          root.rewindDirectory();
          File entry =  root.openNextFile();
          if (!entry) {
            PRINTLN("Failed to get first file");
            return;
          }

          while (entry) {
            if (!entry.isDirectory()) {
              this->writeTxtMessage("SD", entry.name());
              PRINTLN(entry.name());
            }
            entry.close();
            entry =	root.openNextFile();
          }

          root.close();

          uint8_t buffer[10];
          strcpy((char *)&buffer[0], "SD=$EFL$#");
          _pClient->write(buffer, 9 * sizeof(uint8_t));
          PRINTLN("File: list sent");
        }
        else if (strlen(_variable) > 0 && strcmp(_variable, "$SDDL$") == 0) {
          PRINT("File: "); PRINTLN(_value);
          File dataFile = SD.open(_value, FILE_READ);
          if (dataFile) {
            unsigned long n = 0;
            uint8_t buffer[64];
            strcpy((char *)&buffer[0], "SD=$C$#");
            _pClient->write(buffer, 7 * sizeof(uint8_t));

            delay(3000); // OK

            while (dataFile.available()) {
              n = dataFile.read(buffer, sizeof(buffer));
              _pClient->write(buffer, n * sizeof(uint8_t));
            }
            strcpy((char *)&buffer[0], "SD=$E$#");
            _pClient->write(buffer, 7 * sizeof(uint8_t));
            delay(150);
            dataFile.close();
            PRINTLN("File: Sent");
          }
          _pClient->flush();
        }
#endif

#ifdef SDLOGGEDATAGRAPH_SUPPORT
        if (strlen(_variable) > 0 && strcmp(_variable, "$SDLogData$") == 0) {
          PRINT("Logged data request for: "); PRINTLN(_value);
          sdSendLogData(_value);
        }
#endif
        if (strlen(_variable) > 0 && strlen(_value) > 0) {
          // Process incoming messages
          _processIncomingMessages(_variable, _value);
        }
      }

#ifdef ALARMS_SUPPORT
      // Check and Fire Alarms
      if (_processAlarms != NULL) {
        unsigned long now = _startTime + millis() / 1000;
        if ( (now - _lastAlarmCheck) > ALARM_CHECK_INTERVAL) {
          _lastAlarmCheck = now;
          this->checkAndFireAlarms();
        }
      }
#endif
      // Write outgoing messages
      _processOutgoingMessages();

#ifdef ALARMS_SUPPORT
      // Sync local time with NTP Server
      if (_sendNtpRequest) {

        this->syncTime();
      }

      if ( _udp.parsePacket() ) {

        this->readTime();
      }
#endif

      _doWork();
      delay(_delay);
    }

    // Client disconnected

    localClient.flush();
    localClient.stop();
    _pClient = NULL;

    if (_deviceDisconnected != NULL)
      _deviceDisconnected();
  }

}

void AMController::readVariable(void) {
  _variable[0] = '\0';
  _value[0] = '\0';
  _var = true;
  _idx = 0;

  while (_pClient->available()) {
    char c = _pClient->read();
    if (isprint (c)) {
      if (c == '=') {
        _variable[_idx] = '\0';
        _var = false;
        _idx = 0;
      }
      else {
        if (c == '#') {
          _value[_idx] = '\0';
          _var = true;
          _idx = 0;

          return;
        }
        else {
          if (_var) {
            if (_idx == VARIABLELEN)
              _variable[_idx] = '\0';
            else
              _variable[_idx++] = c;
          }
          else {
            if (_idx == VALUELEN)
              _value[_idx] = '\0';
            else
              _value[_idx++] = c;
          }
        }
      }
    }
  }
}

void AMController::writeMessage(const char *variable, int value) {
  char buffer[VARIABLELEN + VALUELEN + 3];
  if (_pClient == NULL)
    return;
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%d#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::writeMessage(const char *variable, float value) {
  char buffer[VARIABLELEN + VALUELEN + 3];
  if (_pClient == NULL)
    return;
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%.3f#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::writeTripleMessage(const char *variable, float vX, float vY, float vZ) {
  char buffer[VARIABLELEN + VALUELEN + 3];
  char vbufferAx[VALUELEN];
  char vbufferAy[VALUELEN];
  char vbufferAz[VALUELEN];

  dtostrf(vX, 0, 2, vbufferAx);
  dtostrf(vY, 0, 2, vbufferAy);
  dtostrf(vZ, 0, 2, vbufferAz);
  snprintf(buffer, VARIABLELEN + VALUELEN + 3, "%s=%s:%s:%s#", variable, vbufferAx, vbufferAy, vbufferAz);

  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::writeTxtMessage(const char *variable, const char *value) {
  char buffer[128];
  
  snprintf(buffer, 128, "%s=%s#", variable, value);
  _pClient->write((const uint8_t *)buffer, strlen(buffer)*sizeof(char));
}

void AMController::log(const char *msg) {
  this->writeTxtMessage("$D$", msg);
}

void AMController::log(int msg) {
  char buffer[11];
  
  itoa(msg, buffer, 10);
  this->writeTxtMessage("$D$", buffer);
}


void AMController::logLn(const char *msg) {
  this->writeTxtMessage("$DLN$", msg);
}

void AMController::logLn(int msg) {
  char buffer[11];
  
  itoa(msg, buffer, 10);
  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(long msg) {
  char buffer[11];
  
  ltoa(msg, buffer, 10);
  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::logLn(unsigned long msg) {

  char buffer[11];
  ltoa(msg, buffer, 10);

  this->writeTxtMessage("$DLN$", buffer);
}

void AMController::temporaryDigitalWrite(uint8_t pin, uint8_t value, unsigned long ms) {
  boolean previousValue = digitalRead(pin);
  
  digitalWrite(pin, value);
  delay(ms);
  digitalWrite(pin, previousValue);
}

// Time Management

#ifdef ALARMS_SUPPORT

void AMController::setNTPServerAddress(IPAddress address) {
  _timeServerAddress = address;
}

void AMController::syncTime() {
  // Send Request to NTP Server
  _sendNtpRequest = false;
  _udp.begin(2390); // Local Port to listen for UDP packets
  this->sendNTPpacket(_timeServerAddress, _udp);
  PRINT("NNP Request Sent to address ");
  PRINTLN(_timeServerAddress);
}

unsigned long AMController::now() {
  unsigned long now = _startTime + millis() / 1000;
  return now;
}

void AMController::readTime() {

  // Packet Received from NTP Server
  _udp.read(_packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:

  unsigned long highWord = word(_packetBuffer[40], _packetBuffer[41]);
  unsigned long lowWord = word(_packetBuffer[42], _packetBuffer[43]);

  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;

  // now convert NTP time into everyday time:
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years to get Unix time:
  _startTime = secsSince1900 - seventyYears;

  // subtract current millis to sync with time in Arduino
  _startTime -= millis() / 1000;
  PRINTLN("NNP Respose");
  this->printTime(_startTime);
  PRINTLN();
}

// send an NTP request to the time server at the given address
void AMController::sendNTPpacket(IPAddress& address, WiFiUDP udp) {

  // set all bytes in the buffer to 0
  memset(_packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  //PRINTLN("2");
  _packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  _packetBuffer[1] = 0;     // Stratum, or type of clock
  _packetBuffer[2] = 6;     // Polling Interval
  _packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  _packetBuffer[12]  = 49;
  _packetBuffer[13]  = 0x4E;
  _packetBuffer[14]  = 49;
  _packetBuffer[15]  = 52;

  //PRINTLN("3");

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  //PRINTLN("4");
  udp.write(_packetBuffer, NTP_PACKET_SIZE);
  //PRINTLN("5");
  udp.endPacket();
}

#ifdef DEBUG

void AMController::breakTime(unsigned long time, int *seconds, int *minutes, int *hours, int *Wday, long *Year, int *Month, int *Day) {
  // break the given time_t into time components
  // this is a more compact version of the C library localtime function
  // note that year is offset from 1970 !!!

  unsigned long year;
  uint8_t month, monthLength;
  unsigned long days;

  *seconds = time % 60;
  time /= 60; // now it is minutes
  *minutes = time % 60;
  time /= 60; // now it is hours
  *hours = time % 24;
  time /= 24; // now it is days
  *Wday = ((time + 4) % 7) + 1;  // Sunday is day 1

  year = 0;
  days = 0;
  while ((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
    year++;
  }
  *Year = year + 1970; // year is offset from 1970

  days -= LEAP_YEAR(year) ? 366 : 365;
  time -= days; // now it is days in this year, starting at 0

  days = 0;
  month = 0;
  monthLength = 0;
  for (month = 0; month < 12; month++) {
    if (month == 1) { // february
      if (LEAP_YEAR(year)) {
        monthLength = 29;
      }
      else {
        monthLength = 28;
      }
    }
    else {
      monthLength = monthDays[month];
    }

    if (time >= monthLength) {
      time -= monthLength;
    }
    else {
      break;
    }
  }
  *Month = month + 1;  // jan is month 1
  *Day = time + 1;     // day of month
}

void AMController::printTime(unsigned long time) {

  int seconds;
  int minutes;
  int hours;
  int Wday;
  long Year;
  int Month;
  int Day;

  this->breakTime(time, &seconds, &minutes, &hours, &Wday, &Year, &Month, &Day);

  PRINT(Day);
  PRINT("/");
  PRINT(Month);
  PRINT("/");
  PRINT(Year);
  PRINT(" ");
  PRINT(hours);
  PRINT(":");
  PRINT(minutes);
  PRINT(":");
  PRINT(seconds);
}

#endif

#ifdef ALARMS_SUPPORT

void AMController::createUpdateAlarm(char *id, unsigned long time, bool repeat) {
  FileManager	fileManager;
  Alarm 		a;
  int 		pos;

  PRINT("Finding alarm "); PRINTLN(id);
  pos = fileManager.find(_alarmFile, (uint8_t*)&a, sizeof(a), &check, id);
  PRINT("Result "); PRINTLN(pos);

  if (pos > -1) {
    a.time = time;
    a.repeat = repeat;

    PRINTLN("Updating alarm " + *id);
    fileManager.update(_alarmFile, pos, (uint8_t *)&a, sizeof(a));

#ifdef DEBUG
    dumpAlarms();
#endif
    return;
  }

  strcpy(a.id, id);
  a.time = time;
  a.repeat = repeat;

  PRINT("Appending alarm "); PRINTLN(*id);
  fileManager.append(_alarmFile, (uint8_t *)&a, sizeof(a));

#ifdef DEBUG
  dumpAlarms();
#endif
}

void AMController::removeAlarm(char *id) {

  FileManager	fileManager;
  Alarm 		a;
  int 		pos;
  pos = fileManager.find(_alarmFile, (uint8_t*)&a, sizeof(a), &check, id);

  if (pos > -1) {

    fileManager.remove(_alarmFile, pos, sizeof(a));
  }
#ifdef DEBUG
  dumpAlarms();
#endif
}


#ifdef DEBUG
void AMController::dumpAlarms() {
  PRINTLN("\t----Dump Alarms -----");
  FileManager	fileManager;
  for (int i = 0; i < MAX_ALARMS; i++) {
    Alarm a;
    if (!fileManager.read(_alarmFile, i, (uint8_t *)&a, sizeof(a)))
      return;

    PRINT("\tId: "); PRINT(a.id);
    PRINT(" time: "); printTime(a.time);
    PRINT(" Repeat: "); PRINTLN(a.repeat);
  }
}
#endif

void AMController::checkAndFireAlarms() {

  FileManager	fileManager;
  unsigned long now = _startTime + millis() / 1000;

#ifdef DEBUG
  PRINT("checkAndFireAlarms ");
  this->printTime(now);
  PRINTLN();
  this->dumpAlarms();
#endif

  for (int i = 0; i < MAX_ALARMS; i++) {
    Alarm a;
    if (!fileManager.read(_alarmFile, i, (uint8_t *)&a, sizeof(a)))
      return;

    if (a.time <= now) {
      PRINT("Firing "); PRINTLN(a.id);
      // First character of id is A and has to be removed
      _processAlarms(a.id);
      if (a.repeat) {
        a.time += 86400; // Scheduled again tomorrow
        fileManager.update(_alarmFile, i, (uint8_t *)&a, sizeof(a));
#ifdef DEBUG
        PRINT("Alarm rescheduled at ");
        this->printTime(a.time);
        PRINTLN();
#endif
      }
      else {
        //     Alarm removed
        fileManager.remove(_alarmFile, i, sizeof(a));
#ifdef DEBUG
        this->dumpAlarms();
#endif
      }
    }
  }
}

#endif
#endif

#ifdef SDLOGGEDATAGRAPH_SUPPORT

void AMController::sdLogLabels(const char *variable, const char *label1) {
  this->sdLogLabels(variable, label1, NULL, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2) {
  this->sdLogLabels(variable, label1, label2, NULL, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3) {
  this->sdLogLabels(variable, label1, label2, label3, NULL, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4) {
  this->sdLogLabels(variable, label1, label2, label3, label4, NULL);
}

void AMController::sdLogLabels(const char *variable, const char *label1, const char *label2, const char *label3, const char *label4, const char *label5) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile) {
    dataFile.print("-");
    dataFile.print(";");
    dataFile.print(label1);
    dataFile.print(";");

    if (label2 != NULL)
      dataFile.print(label2);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label3 != NULL)
      dataFile.print(label3);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label4 != NULL)
      dataFile.print(label4);
    else
      dataFile.print("-");
    dataFile.print(";");

    if (label5 != NULL)
      dataFile.println(label5);
    else
      dataFile.println("-");

    dataFile.flush();
    dataFile.close();
  } else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}


void AMController::sdLog(const char *variable, unsigned long time, float v1) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);

    dataFile.print(";-;-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);

    dataFile.print(";-;-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);

    dataFile.print(";-;-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");
    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);

    dataFile.println(";-");
    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}

void AMController::sdLog(const char *variable, unsigned long time, float v1, float v2, float v3, float v4, float v5) {
  File dataFile = SD.open(variable, FILE_WRITE);
  
  if (dataFile && time > 0) {
    dataFile.print(time);
    dataFile.print(";");

    dataFile.print(v1);
    dataFile.print(";");

    dataFile.print(v2);
    dataFile.print(";");

    dataFile.print(v3);
    dataFile.print(";");

    dataFile.print(v4);
    dataFile.print(";");

    dataFile.println(v5);

    dataFile.println();

    dataFile.flush();
    dataFile.close();
  }
  else {
    PRINT("Error opening"); PRINTLN(variable);
  }
}

void AMController::sdSendLogData(const char *variable) {
  File dataFile = SD.open(variable, FILE_READ);
  if (dataFile) {
    char c;
    char buffer[128];
    int i = 0;
    dataFile.seek(0);
    
    PRINTLN("Start reading file");
    
    while ( dataFile.available() ) {
      c = dataFile.read();
      if (c == '\n') {
        buffer[i++] = '\0';
        PRINTLN(buffer);
        this->writeTxtMessage(variable, buffer);
        i = 0;
      }
      else {
        buffer[i++] = c;
      }
    }
    PRINTLN("All data sent");
    dataFile.close();
  }
  else {
    PRINT("Error opening "); PRINTLN(variable);
  }

  this->writeTxtMessage(variable, "");
}

// Size in bytes
uint16_t AMController::sdFileSize(const char *variable) {
  File dataFile = SD.open(variable, FILE_WRITE);
  if (dataFile) {
    return dataFile.size();
  }
  return -1;
}

void AMController::sdPurgeLogData(const char *variable) {
  noInterrupts();
  SD.remove(variable);
  interrupts();
}

#endif
