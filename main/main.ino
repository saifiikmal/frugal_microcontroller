#include <Preferences.h>
#include <BluetoothSerial.h>
#include <RTClib.h>
#include <time.h>
#include <map>
#include <string>
#include <iostream>

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

RTC_DS1307 rtc;

const int LED_PIN = 13;
const char* PREF_NAMESPACE = "led_state";
const char* PREF_KEY = "state";
const char* PREF_KEY2 = "state2";
std::map<std::string, std::map<std::string, std::string>> dispenseTimer;

int cycleTimer = 0;
int cycleCounter = 0;
int cycleAmount = 0;
bool cycleInterval = false;
int cycleStart = 0;
unsigned int lastDispense;
unsigned int lastDispenseCounter;
unsigned int counter;
unsigned int dispLimit;

DateTime lastDate;

Preferences preferences;
BluetoothSerial SerialBT;

bool currentState = false;

void setup() {
  // setenv("TZ", "UTC8", 1);
  // tzset();

  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  preferences.begin(PREF_NAMESPACE, false);
  Serial.println(currentState ? "LED is ON" : "LED is OFF");

  String macAddress = getMacAddress();

  SerialBT.begin("ESP32_"+macAddress); // Set the Bluetooth device name

  // Check if there is an existing connection
  if (SerialBT.connected()) {
    Serial.println("Bluetooth connected");
  }

  // SETUP RTC MODULE
  if (! rtc.begin()) {
    Serial.println("RTC module is NOT found");
    Serial.flush();
    while (1);
  }

  Serial.print("ESP Board MAC Address:  ");
  Serial.println(macAddress);

  digitalWrite(LED_PIN, HIGH);
  preferences.putBool(PREF_KEY, false);
  preferences.putBool("on_dispense", false);
  
  DateTime now = rtc.now();
  lastDate = DateTime(now.year(), now.month(), now.day(), now.hour() + 8, now.minute(), now.second());
  
  lastDispense = preferences.getUInt("last_dispense", 0);
  lastDispenseCounter = preferences.getUInt("last_disp_ct", 0);
  counter = preferences.getUInt("ct", 0);
  dispLimit = preferences.getUInt("disp_limit", 0);

  String timerStr = preferences.getString("timer");

  if (!timerStr.isEmpty()) {
    setTimer(timerStr);
  }

}

void loop() {
  DateTime now = rtc.now();
  unsigned long epochNow = now.unixtime();

  DateTime dateNow = DateTime(now.year(), now.month(), now.day(), now.hour() + 8, now.minute(), now.second());

  String macAddress = getMacAddress();
  
  bool status = preferences.getBool("status", true);
  bool onDispense = preferences.getBool("on_dispense", false);

  bool isSync = preferences.getBool("is_sync", false);

  unsigned int sprayPressDuration = preferences.getUInt("press_duration", 3);
  unsigned int pauseBetweenSpray = preferences.getUInt("pause_spray", 3);

  currentState = preferences.getBool(PREF_KEY, false);
  String timerStr = preferences.getString("timer");
  String dispenser = preferences.getString("dispenser", "");
  String canister = preferences.getString("canister", "");


  if (status == true && getFormattedTime(lastDate) != getFormattedTime(dateNow) && onDispense == false) {
    lastDate = dateNow;
    String dateStr = getFormattedTime(dateNow);
    Serial.println(getFormattedTime(dateNow));
    if (!dispenseTimer.empty()) {

      if (dispenseTimer.find(dateStr.c_str()) != dispenseTimer.end()) {
        Serial.println(getFormattedTime(dateNow));
        Serial.println(dateNow.dayOfTheWeek());
        if (dispenseTimer[dateStr.c_str()]["type"] == "D") {
          Serial.println("dispense type: D");
          cycleAmount = std::stoi(dispenseTimer[dateStr.c_str()]["amount"]);
          preferences.putBool("on_dispense", true);
        }
        if (dispenseTimer[dateStr.c_str()]["type"] == "W") {
          Serial.println("dispense type: W - " + String(dateNow.dayOfTheWeek()));
          if (dispenseTimer[dateStr.c_str()][std::to_string(dateNow.dayOfTheWeek())] == "1") {
            Serial.println("dispense type W: " + String(dispenseTimer[dateStr.c_str()][std::to_string(dateNow.dayOfTheWeek())].c_str()));
            cycleAmount = std::stoi(dispenseTimer[dateStr.c_str()]["amount"]);
            preferences.putBool("on_dispense", true);
          }
        }
      }
    }
  }

  if (status == true && counter <= dispLimit && onDispense == true && cycleAmount > 0) {
    // Serial.println("cycle counter: " + String(cycleCounter));
    // Serial.println("cycle timer: " + String(cycleTimer));
    // Serial.println("now: " + String(epochNow));

    if (cycleTimer > 0) {
      // if spray counter < spray amount
      if (cycleCounter < cycleAmount) {
        // spray until reach spray press duration
        if (epochNow < cycleTimer && cycleInterval == true && epochNow >= cycleStart) {
          Serial.println("on 2: " + String(epochNow) + "," + String(cycleTimer) + "," + String(cycleStart) + "," + String(cycleCounter));
          cycleInterval = false;

          digitalWrite(LED_PIN, LOW);
        }
        // after reach spray press duration, turn off spray
        if (epochNow >= cycleTimer) {
          Serial.println("off: " + String(epochNow) + "," + String(cycleTimer) + "," + String(cycleStart) + "," + String(cycleCounter));
          digitalWrite(LED_PIN, HIGH);

          cycleTimer = epochNow + sprayPressDuration + pauseBetweenSpray;
          cycleStart = epochNow + pauseBetweenSpray;

          lastDispenseCounter = cycleCounter+1;
          counter = counter + 1;
          preferences.putUInt("last_dispense", lastDispense);
          preferences.putUInt("last_disp_ct", lastDispenseCounter);
          preferences.putUInt("ct", counter);

          cycleCounter++;
          cycleInterval = true;
        }
      }
      if (cycleCounter >= cycleAmount) {
        onDispense = false;
        preferences.putBool("on_dispense", false);
        cycleCounter = 0;
        cycleTimer = 0;
        cycleAmount = 0;

        digitalWrite(LED_PIN, HIGH);
        preferences.putBool(PREF_KEY, false);
      }
    }

    // start of new cycle
    if (onDispense == true && cycleTimer == 0) {
      Serial.println("on 1: " + String(epochNow) + "," + String(cycleTimer) + "," + String(cycleStart) + "," + String(cycleCounter));
      cycleTimer = epochNow + sprayPressDuration;
      lastDispense = epochNow;
      
      digitalWrite(LED_PIN, LOW);

    } 
    
  }

  // Check for Bluetooth commands
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();

    if (command == "ON") {
      digitalWrite(LED_PIN, LOW);
      preferences.putBool(PREF_KEY2, true);
      Serial.println("LED is ON");
    } 
    
    if (command == "OFF") {
      digitalWrite(LED_PIN, HIGH);
      preferences.putBool(PREF_KEY2, false);
      Serial.println("LED is OFF");
    }

    if (command.startsWith("ACTIVATE")) {
      preferences.putBool("status", true);
    }

    if (command.startsWith("DEACTIVATE")) {
      preferences.putBool("status", false);
    }

    if (command.startsWith("REGCANISTER")) {
      setCanister(command);
      bluetoothPrintLine("REGCANISTER:1");
    }

    if (command.startsWith("REGDISPENSER")) {
      command.remove(0, 13);
      preferences.putString("dispenser", command);
      bluetoothPrintLine("REGDISPENSER:1");
    }

    if (command.startsWith("GET")) {
      // Serial.println(interval);
      bluetoothPrintLine("GET:" + macAddress + "," + dispenser + "," + canister + "," + String(epochNow) + "," + String(sprayPressDuration) + ","  + String(pauseBetweenSpray) + "," + String(lastDispense) + "," + String(lastDispenseCounter) + "," + String(counter) + "," + String(dispLimit) + "," + String(status) + "," + String(isSync));
    }

    if (command.startsWith("SET")) {
      setConfig(command);
      bluetoothPrintLine("SET:1");

    }

    if (command.startsWith("SYNC")) {
      sync(command);
      bluetoothPrintLine("SYNC:1");
    }

    if (command.startsWith("CLOCK")) {
      
      Serial.println(epochNow);
      bluetoothPrintLine("CLOCK:" + String(epochNow));
    }

    if (command.startsWith("TIMER")) {
      // e.g  "D,18:52,010,0,0,0,0,0,0,0|W,14:50,015,1,0,1,0,1,0,1";
      command.remove(0, 6);
      preferences.putString("timer", command);
      setTimer(command);
      bluetoothPrintLine("TIMER:1");
    }

    if (command.startsWith("CLEAR")) {
      preferences.putBool("status", false);
      preferences.putBool("is_sync", false);
      preferences.putString("dispenser", "");
      preferences.putString("canister", "");
      preferences.putString("timer", "");

      dispLimit = 0;
      preferences.putUInt("disp_limit", 0);

      counter = 0;
      preferences.putUInt("ct", 0);

      lastDispense = 0;
      preferences.putUInt("last_dispense", 0);

      lastDispenseCounter = 0;
      preferences.putUInt("last_disp_ct", 0);

      bluetoothPrintLine("CLEAR:1");

    }
  }

  delay(1000);
}

String getFormattedTime(const DateTime& dateTime) {
    // Extract hour and minute components from DateTime object
    int hour = dateTime.hour();
    int minute = dateTime.minute();

    // Format the time as "HH:mm"
    String formattedTime = String(hour < 10 ? "0" + String(hour) : String(hour)) + ":" +
                           String(minute < 10 ? "0" + String(minute) : String(minute));

    return formattedTime;
}

void sync(String data) {
  data.remove(0, 5);

  int comma1Index = data.indexOf(',');
  int comma2Index = data.indexOf(',', comma1Index+1);
  int comma3Index = data.indexOf(',', comma2Index+1);

  // Extract the values between the delimiters
  String value1Str = data.substring(0, comma1Index);
  String value2Str = data.substring(comma1Index + 1, comma2Index);
  String value3Str = data.substring(comma2Index + 1, comma3Index);
  String value4Str = data.substring(comma3Index + 1, data.length());
  
  time_t epochTimestamp = value1Str.toInt();

  struct tm *tmTime = gmtime(&epochTimestamp);

  DateTime dt(tmTime->tm_year + 1900, tmTime->tm_mon + 1, tmTime->tm_mday,
               tmTime->tm_hour, tmTime->tm_min, tmTime->tm_sec);
  rtc.adjust(dt);

  DateTime now = rtc.now();
  unsigned long secondstime = now.unixtime();
  Serial.print("ESP32 RTC Date Time: ");
  Serial.println(secondstime);

  String dispenser = preferences.getString("dispenser", "");
  String canister = preferences.getString("canister", "");

  int countSync = 0;
  if (dispenser == "") {
    preferences.putString("dispenser", value2Str);
    countSync++;
  }
  // Serial.println("disp limit: " + String(value4Str.toInt()));
  if (value4Str != "") {
    dispLimit = value4Str.toInt();
    preferences.putUInt("disp_limit", dispLimit);
  }

  if (canister == "") {
    preferences.putString("canister", value3Str);
    preferences.putUInt("ct", 0);
    counter = 0;
    countSync++;
  } else {
    if (canister != value3Str) {
      preferences.putString("canister", value3Str);
      counter = 0;
      lastDispenseCounter = 0;
      lastDispense = 0;
      preferences.putUInt("ct", 0);
      preferences.putUInt("last_dispense", lastDispense);
      preferences.putUInt("last_disp_ct", lastDispenseCounter);
    }
  }

  if (countSync >= 2) {
    preferences.putBool("is_sync", true);
    preferences.putBool("status", true);
  }
}

void setCanister(String data) {
  data.remove(0, 12);

  String canister = preferences.getString("canister", "");

  if (data != canister) {
      counter = 0;
      lastDispenseCounter = 0;
      lastDispense = 0;
      preferences.putUInt("ct", 0);
      preferences.putUInt("last_dispense", lastDispense);
      preferences.putUInt("last_disp_ct", lastDispenseCounter);
      preferences.putString("canister", data);
  } 
}

void setTimer(String data) {
  int MAX_ENTRIES = 0;
  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) == '|') {
      MAX_ENTRIES++;
    }
  }
  MAX_ENTRIES++; // Increment for the last entry

  String* entries = new String[MAX_ENTRIES];
  int count = 0;
  int startPos = 0;

  for (int i = 0; i < data.length(); i++) {
    if (data.charAt(i) == '|') {
      entries[count] = data.substring(startPos, i);
      startPos = i + 1;
      count++;
    }
  }

  entries[count] = data.substring(startPos);

  for (int i = 0; i < MAX_ENTRIES; i++) {
    String entry = entries[i];
    Serial.print("Entry: ");
    Serial.println(entry);

    char type = entry.charAt(0);
    Serial.print("Type: ");
    Serial.println(type);

    String time = entry.substring(2, 7);
    Serial.print("Time: ");
    Serial.println(time);

    String amount = entry.substring(8, 11);
    Serial.print("Amount: ");
    Serial.println(amount);

    dispenseTimer[time.c_str()]["type"] = type; 
    dispenseTimer[time.c_str()]["amount"] = amount.c_str(); 

    char days[7];
    for (int j = 0; j < 7; j++) {
      Serial.print("Entry2: ");
      Serial.println(entry);

      days[j] = entry.charAt(12 + (2 * j));

      Serial.print("Day: ");
      Serial.println(days[j]);

      dispenseTimer[time.c_str()][std::to_string(j)] = days[j];
    }
  }

}

void setConfig(String data) {
  // Find the position of the colon and comma
  // int colonIndex = data.indexOf(':');
  data.remove(0, 4);
  int comma1Index = data.indexOf(',');

  // Extract the values between the delimiters
  String value1Str = data.substring(0, comma1Index);
  String value2Str = data.substring(comma1Index + 1, data.length());

  // Convert the extracted strings to integers
  unsigned int sprayPressDuration = value1Str.toInt();
  unsigned int pauseBetweenSpray = value2Str.toInt();

  preferences.putUInt("press_duration", sprayPressDuration);
  preferences.putUInt("pause_spray", pauseBetweenSpray);

  // Now you have the values in the variables value1, value2, and value3
  Serial.print("Spray Press Duration: ");
  Serial.println(sprayPressDuration);
  bluetoothPrintLine("Spray Press Duration: " + value1Str);

  Serial.print("Pause Between Spray: ");
  Serial.println(pauseBetweenSpray);
  bluetoothPrintLine("Pause Between Spray: " + value2Str);

}

void bluetoothPrintLine(String line)
{
  unsigned l = line.length();
  for (int i = 0; i < l; i++)
  {
    if (line[i] != '\0')
      SerialBT.write(byte(line[i]));
  }
  SerialBT.write(10); // \n
}

String getMacAddress() {
  // Get the MAC address of the Bluetooth interface
  uint8_t baseMac[6];

  esp_read_mac(baseMac, ESP_MAC_BT);
  String macAddress = "";
  
  for (int i = 0; i < 5; i++) {
    macAddress += String(baseMac[i], HEX);
    macAddress += ":";
  }
  
  macAddress += String(baseMac[5], HEX);

  // Convert the MAC address string to uppercase
  for (char &c : macAddress) {
    c = toupper(c);
  }
  
  return macAddress;

}