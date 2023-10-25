#include <Preferences.h>
#include <BluetoothSerial.h>
#include <RTClib.h>
#include <time.h>

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

Preferences preferences;
BluetoothSerial SerialBT;

bool currentState = false;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  preferences.begin(PREF_NAMESPACE, false);
  // currentState = preferences.getBool(PREF_KEY, false);
  // digitalWrite(LED_PIN, currentState ? HIGH : LOW);
  Serial.println(currentState ? "LED is ON" : "LED is OFF");

  SerialBT.begin("ESP32_"+WiFi.macAddress()); // Set the Bluetooth device name

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

  // automatically sets the RTC to the date & time on PC this sketch was compiled
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  Serial.print("ESP Board MAC Address:  ");
  Serial.println(WiFi.macAddress());

  digitalWrite(LED_PIN, HIGH);
  preferences.putBool(PREF_KEY, false);
}

void loop() {
  DateTime now = rtc.now();
  unsigned long epochNow = now.unixtime();

  String macAddress = WiFi.macAddress();
  unsigned int startDate = preferences.getUInt("start_date", 0);
  unsigned int endDate = preferences.getUInt("end_date", 0);
  unsigned int interval = preferences.getUInt("interval", 0);
  unsigned int nextInterval = preferences.getUInt("next_interval", 0);
  unsigned int lastInterval = preferences.getUInt("last_interval", 0);
  unsigned int counter = preferences.getUInt("counter", 0);
  bool status = preferences.getBool("status", false);

  currentState = preferences.getBool(PREF_KEY, false);

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

    if (command.startsWith("GET")) {
      // Serial.println(interval);
      bluetoothPrintLine("GET:" + macAddress + "," + String(epochNow) + "," + String(startDate) + "," + String(endDate) + "," + String(interval) + "," + String(nextInterval) + "," + String(lastInterval)  + "," + String(counter) + "," + String(status));
    }

    if (command.startsWith("SET")) {
      setConfig(command);
      bluetoothPrintLine("SET:1");
    }

    if (command.startsWith("SYNC")) {
      setClock(command);
      bluetoothPrintLine("SYNC:1");
    }

    if (command.startsWith("CLOCK")) {
      
      Serial.println(epochNow);
      bluetoothPrintLine("CLOCK:" + String(epochNow));
    }
  }

  if (status == true && epochNow >= endDate) {
    preferences.putBool("status", false);
    preferences.putUInt("counter", 0);
  }

  if (status == true && epochNow >= startDate && epochNow >= (lastInterval+3) && currentState == true) {
    digitalWrite(LED_PIN, HIGH);
    preferences.putBool(PREF_KEY, false);
  }
  if (status == true && epochNow >= startDate && nextInterval > 0 && epochNow >= nextInterval && epochNow <= endDate) {
    preferences.putUInt("next_interval", epochNow + interval);
    preferences.putUInt("last_interval", epochNow);
    preferences.putUInt("counter", counter+1);

    digitalWrite(LED_PIN, LOW);
    preferences.putBool(PREF_KEY, true);
  }

  delay(1000);
}

void setClock(String data) {
  data.remove(0, 5);
  time_t epochTimestamp = data.toInt();

  struct tm *tmTime = gmtime(&epochTimestamp);

  DateTime dt(tmTime->tm_year + 1900, tmTime->tm_mon + 1, tmTime->tm_mday,
               tmTime->tm_hour, tmTime->tm_min, tmTime->tm_sec);
  rtc.adjust(dt);

  DateTime now = rtc.now();
  unsigned long secondstime = now.unixtime();
  Serial.print("ESP32 RTC Date Time: ");
  Serial.println(secondstime);
}

void setConfig(String data) {
  // Find the position of the colon and comma
  // int colonIndex = data.indexOf(':');
  data.remove(0, 4);
  int comma1Index = data.indexOf(',');
  int comma2Index = data.indexOf(',', comma1Index+1);

  // Extract the values between the delimiters
  String value1Str = data.substring(0, comma1Index);
  String value2Str = data.substring(comma1Index + 1, comma2Index);
  String value3Str = data.substring(comma2Index + 1, data.length()); // -1 to exclude the trailing comma

  // Convert the extracted strings to integers
  unsigned int startDate = value1Str.toInt();
  unsigned int endDate = value2Str.toInt();
  unsigned int interval = value3Str.toInt();

  preferences.putUInt("start_date", startDate);
  preferences.putUInt("end_date", endDate);
  preferences.putUInt("interval", interval);

  if (interval > 0 && startDate > 0 && endDate > 0) {
    unsigned int nextInterval = startDate + interval;
    preferences.putUInt("next_interval", nextInterval);
    preferences.putUInt("last_interval", startDate);
    preferences.putUInt("counter", 0);
    preferences.putBool("status", true);
  }

  // Now you have the values in the variables value1, value2, and value3
  Serial.print("Start Date: ");
  Serial.println(startDate);
  bluetoothPrintLine("Start Date: " + value1Str);

  Serial.print("End Date: ");
  Serial.println(endDate);
  bluetoothPrintLine("End Date: " + value2Str);

  Serial.print("Interval: ");
  Serial.println(interval);
  bluetoothPrintLine("Interval: " + value3Str);
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