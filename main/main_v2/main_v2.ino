// Frugal v1.0.3
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <time.h>
//debug
#include "nvs.h"
#include "nvs_flash.h"

Preferences p;

// Define UUIDs for service and characteristic
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
//#define BUTTON_CHARACTERISTIC_UUID "992152C8-4F38-47FA-B1C6-F2C7B6625791"
BLECharacteristic *pCharacteristic = nullptr;
//BLECharacteristic *pButtonCharacteristic = nullptr;
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;

//spray motor
const int MOTOR_PIN = 12;
// const int LED_PIN = 14;
const int RED_PIN = 21;
const int GREEN_PIN = 19;
const int BLUE_PIN = 18;

//Timer
unsigned long intervalBle = 300000;
bool bleStartBool = false;
bool bleConnected = false;
bool bleEnabled = false;
bool bleUpdated = false;

RTC_DATA_ATTR int deepSleepInt = 0;
RTC_DATA_ATTR int sprayNum = 0;

unsigned int lastDispense = 0;
unsigned int lastDispenseCt = 0;
unsigned int counter = 0;
unsigned int dispLimit = 0;
bool status = false;
bool isSync = false;
unsigned int sprayPressDuration = 0;
unsigned int pauseBetweenSpray = 0;
String dispenser;
String canister;

String jsonString = "";
unsigned long currentMillis = 0;

void killBle() {
  // assignLed();
  bleEnabled = false;
  bleConnected = false;
  bleStartBool = true;
  BLEDevice::getAdvertising()->stop();
  //pServer->stop();
  BLEDevice::deinit();
  Serial.println("killing this ble");
}

class MyServerCallbacks:public BLEServerCallbacks, public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            // Serial.println("Received chunk:");
            // Serial.println((char*)&value[0]);

            jsonString += String(value.c_str());
            jsonString.trim();

            // Serial.println("before parse");
            // Serial.print(jsonString);

            // If you have a termination character (let's say `#`), check for its presence
            // Alternatively, you could use a timer to detect the end of data
            if (jsonString.endsWith("#")) {
              jsonString = jsonString.substring(0, jsonString.length() - 1);

              // Now process the full data
              Serial.println("Full data received:");
              Serial.println(jsonString);

              if (jsonString.startsWith("TIMER:")) {
              // e.g  "D,18:52,010,0,0,0,0,0,0,0|W,14:50,015,1,0,1,0,1,0,1";
                // Serial.println(received);
                jsonString.remove(0, 6);
                setTimer(jsonString);
              }

              if (jsonString.startsWith("GET")) {
                String info = getInfo();

                pCharacteristic->setValue(info.c_str());
                pCharacteristic->notify();
              }

              if (jsonString.startsWith("SET:")) {
                jsonString.remove(0, 4);
                setConfig(jsonString);
              }

              if (jsonString.startsWith("SYNC:")) {
                jsonString.remove(0, 5);
                sync(jsonString);
              }

              if (jsonString.startsWith("CLEAR")) {
                p.begin("spraySettings", false);
                p.clear();
                p.end();

                String response = "CLEAR:1";
                pCharacteristic->setValue(response.c_str());
                pCharacteristic->notify();
              }

              if (jsonString.startsWith("CLOCK")) {
                printCurrentTime();
              }

              if (jsonString.startsWith("DEMO")) {
                demoSpray(1);

                String response = "DEMO:1";
                pCharacteristic->setValue(response.c_str());
                pCharacteristic->notify();
              }

              jsonString = "";
            }
        }

    }

    void onConnect(BLEServer* pServer) {
      bleConnected = true;
      // changeLed("blue");
    }

    void onDisconnect(BLEServer* pServer) {
        // Client has disconnected
        Serial.println("Client disconnected");
        // pServer->startAdvertising();
        // currentMillis = millis();
        killBle();
    }
};

void setup() {
  // Set timezone to local time
  // setenv("TZ", "UTC-8", 1);
  // tzset();
  
  Serial.begin(115200);
  pinMode(MOTOR_PIN,OUTPUT);
  pinMode(RED_PIN,OUTPUT);
  pinMode(GREEN_PIN,OUTPUT);
  pinMode(BLUE_PIN,OUTPUT);

  digitalWrite(MOTOR_PIN,LOW);

  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);

  loadSettings();

  Serial.println("deepSleepInt:" + String(deepSleepInt) + ", bleStartBool: " + bleStartBool);

  if (deepSleepInt == 0) {
    // updateTime("2023-12-27 18:00:00");
    enableBle();
  } else {
    bleStartBool = true;
    Serial.println("Wake up from deep sleep and start spraying");
    if (status) {
      sprayCan(sprayNum, true);
    }
    // assignLed();
  }
  // printCurrentTime();
  Serial.print("Flash chip size: ");
  Serial.println(ESP.getFlashChipSize());
  nvsCheck();
}

void loop() {
  // put your main code here, to run repeatedly:

  if (bleEnabled && !bleConnected && !bleUpdated) {
    BleEnabledLed();
  }

  if (bleEnabled && bleConnected && !bleUpdated) {
    BleConnectedLed();
  }

  if (bleEnabled && bleConnected && bleUpdated) {
    BleUpdatedLed();
  }

  timerKillBle();

  if (bleStartBool == true) {
    enterDeepSleep();
  }
}

void enableBle() {
  //String bleAddress =String(BLEDevice::getAddress().toString().c_str());
    //Serial.println(bleAddress);
    String macAddress = getBleMacAddress();
    String deviceName = "FRUGAL " + macAddress;
    BLEDevice::init(deviceName.c_str());
    BLEDevice::setMTU(517);
    pServer = BLEDevice::createServer();

    // Set server callbacks
    pServer->setCallbacks(new MyServerCallbacks());

    pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
                       CHARACTERISTIC_UUID,
                       BLECharacteristic::PROPERTY_READ |
                       BLECharacteristic::PROPERTY_WRITE |
                       BLECharacteristic::PROPERTY_NOTIFY |
                       BLECharacteristic::PROPERTY_INDICATE
                     );

    pCharacteristic->setCallbacks(new MyServerCallbacks());

    String info = getInfo();

    pCharacteristic->setValue(info.c_str());  // default value
    pCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();

    // changeLed("blue");
    // assignLed();
    bleEnabled = true;
  
    Serial.println("Characteristic defined!");
    Serial.println("Now you can read/write it with your phone!");  
}

void timerKillBle(){
  if(millis() - currentMillis > intervalBle && pServer->getConnectedCount() == 0){
      killBle();
  }
  else if( deepSleepInt == 1){
      return;
  }
}

void sprayCan(int spraynum, bool updateCt){
    Serial.print("spraynum is ");
    Serial.println(spraynum);
    if(spraynum == 0) return;
    time_t now;
    time(&now);

    if(updateCt) lastDispenseCt = spraynum;
    if(updateCt) lastDispense = (int)now;

    if(updateCt) {
      Serial.println("updateLog");
      p.begin("spraySettings", false);

      p.putUInt("last_dispense", lastDispense);
      p.putUInt("last_disp_ct", lastDispenseCt);
      // updateLog();
      // kill ble after spray to save battery
      // if(pServer->getConnectedCount() == 0){
      //   delay(5000);
      //   killBle();
        
      // }
    }

    for(int i=0;i<spraynum;i++){
        Serial.println(i);
        digitalWrite(MOTOR_PIN,HIGH);
        delay(sprayPressDuration);
        digitalWrite(MOTOR_PIN,LOW);

        if(updateCt) counter++;
        delay(pauseBetweenSpray);
    }

    if (updateCt) {
      p.putUInt("ct", counter);

      p.end();
    }
}

void demoSpray(int spraynum) {
  sprayCan(spraynum, false);
}

void enterDeepSleep() {
  Serial.println("enterDeepSleep");
    deepSleepInt = 1;

    unsigned long long sleepDuration = getSleepDuration();
    // unsigned long long sleepDuration = 30;
    if (sleepDuration > 0) {
        Serial.println("Entering deep sleep mode");
        Serial.print("Sleeping for ");
        Serial.print(sleepDuration);
        Serial.print(" seconds and will wake up to spray ");
        Serial.print(String(sprayNum));
        Serial.println(" times");
        
        esp_sleep_enable_timer_wakeup(sleepDuration * 1000000);  // Convert to microseconds
        esp_deep_sleep_start();
    }
}

void nvsCheck(){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    // Get NVS Stats
    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    Serial.println("NVS Stats:");
    Serial.print("Total Entries: "); Serial.println(nvs_stats.total_entries);
    Serial.print("Used Entries: "); Serial.println(nvs_stats.used_entries);
    Serial.print("Free Entries: "); Serial.println(nvs_stats.free_entries);
}

void updateLog() {
  Serial.println("updateLog");
  p.begin("spraySettings", false);

  p.putUInt("last_dispense", lastDispense);
  p.putUInt("last_disp_ct", lastDispenseCt);
  p.putUInt("ct", counter);

  p.end();
}

void updateTime(String dateTimeString) {
    int year, month, day, hour, minute, second;
    sscanf(dateTimeString.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second); // Parse the date and time string

    struct tm timeInfo;
    time_t now;
    time(&now); // Get current time; this gets updated every cycle and is required.
    localtime_r(&now, &timeInfo); // Convert time_t to tm struct

    // Update the struct tm with new values
    timeInfo.tm_year = year - 1900; // tm_year is the number of years since 1900
    timeInfo.tm_mon = month - 1; // tm_mon is months since January (0-11)
    timeInfo.tm_mday = day;
    timeInfo.tm_hour = hour;
    timeInfo.tm_min = minute;
    timeInfo.tm_sec = second;

    // Convert tm struct to time_t
    now = mktime(&timeInfo);
    
    // Set the time to the system
    struct timeval tv;
    tv.tv_sec = now;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    // Print the updated time
    struct tm* updatedTime = localtime(&now); // Get the current system time in `struct tm` format
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", updatedTime);
    Serial.println("Updated RTC Time: " + String(buf));
}

void printCurrentTime(){
    time_t now;
    time(&now);  // Get the current time
    struct tm* currentTime = localtime(&now); // Get the current system time in `struct tm` format
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", currentTime);
    Serial.println("Current RTC Time: " + String(buf));

    int currentDayOfWeek =  currentTime->tm_wday;

    Serial.println("Day of the week: ");
    Serial.print(currentDayOfWeek);

    if (pServer->getConnectedCount() > 0) {
      pCharacteristic->setValue(String(buf).c_str());
      pCharacteristic->notify();
    }

    

    // char currentTimeStr[6];
    // strftime(currentTimeStr, sizeof(currentTimeStr), "%H:%M", currentTime);
    // Serial.println("Current time: " + String(currentTimeStr));

    // calculateTimeDifference(currentTimeStr, "19:00", 0);
}

long calculateTimeDifference(const String& currentTime, const String& settingTime, int elapsedDays) {
  struct tm currentTm, settingTm;
  time_t now;
  time(&now);

  // Initialize both structs with current date
  localtime_r(&now, &currentTm);
  localtime_r(&now, &settingTm);

  // Parse and set the time
  sscanf(currentTime.c_str(), "%d:%d", &currentTm.tm_hour, &currentTm.tm_min);
  sscanf(settingTime.c_str(), "%d:%d", &settingTm.tm_hour, &settingTm.tm_min);
  // currentTm.tm_sec = 
  settingTm.tm_sec = 0; // Reset seconds to 0

  // Add elapsed days to the setting time
  settingTm.tm_mday += elapsedDays;

  // Convert tm struct to time_t and normalize
  time_t current = mktime(&currentTm);
  time_t setting = mktime(&settingTm);

  // Calculate the difference
  long difference = difftime(setting,current);
  Serial.println(difference);
  return difference;
}

long getSleepDuration() {
  Serial.println("getSleepDuration");
  p.begin("sprayTimer",false);//Read Write access

  String mode = p.getString("mode", "");

  if (mode == "custom") {
    time_t now;
    time(&now);
    struct tm* currentTime = localtime(&now);
    char currentTimeStr[6];
    strftime(currentTimeStr, sizeof(currentTimeStr), "%H:%M", currentTime);
    int currentDayOfWeek =  currentTime->tm_wday;
    Serial.print("currentDayOfWeek is ");
    Serial.println(currentDayOfWeek);

    String nextTime;
    long minDiff = LONG_MAX;
    bool foundFutureTime = false;
    int elapseDays = 0;

    //debug
    String debugInt = p.getString("d1st1","12:34");
    Serial.print("debug Int is : ");
    Serial.println(debugInt);

    for(int i = currentDayOfWeek; i < 7; i++){
        long diff;
        String sprayCountTotalKey = "d"+String(i)+"sct";
        int sprayCountTotal = p.getString(sprayCountTotalKey.c_str(), "0").toInt();
        Serial.println(sprayCountTotalKey +": " + String(sprayCountTotal));
        if(sprayCountTotal == 0){ //if today dont have continue to see tomorrow etc]
            elapseDays++;
            Serial.println("today no more");
            continue;
        }
        for (int j=0; j <sprayCountTotal; j++) {
            String sprayTimeKey = "d"+String(i)+"st" + String(j+1);
            String sprayCountKey = "d"+String(i)+"sc" + String(j+1);
            String settingTime = p.getString(sprayTimeKey.c_str(), "00:00");
            Serial.println(settingTime);
            diff = calculateTimeDifference(currentTimeStr, settingTime, elapseDays);
    
            if (diff > 0 && diff < minDiff) {  // Select only future times
                minDiff = diff;
                nextTime = settingTime;
                foundFutureTime = true;
                sprayNum = p.getString(sprayCountKey.c_str(),"0").toInt();
                
            }
        }
        if(foundFutureTime == true) {
          p.end();
          return minDiff;
        }
        else elapseDays++;
    
    }
    if (!foundFutureTime){
        for(int i=0; i <= currentDayOfWeek; i++){
            long diff;
            String sprayCountTotalKey = "d"+String(i)+"sct";
            int sprayCountTotal = p.getString(sprayCountTotalKey.c_str(),"0").toInt();
            Serial.println("Spray Count Total: " + String(sprayCountTotal));
            if(sprayCountTotal == 0){ //if today dont have continue to see tomorrow etc
                elapseDays++;
                continue;
            }
            for (int j=0; j <sprayCountTotal; j++) {
                String sprayTimeKey = "d"+String(i)+"st" + String(j+1);
                String sprayCountKey = "d"+String(i)+"sc" + String(j+1);
                String settingTime = p.getString(sprayTimeKey.c_str(), "00:00:00");
                diff = calculateTimeDifference(currentTimeStr, settingTime, elapseDays);
        
                if (diff > 0 && diff < minDiff) {  // Select only future times
                    minDiff = diff;
                    nextTime = settingTime;
                    foundFutureTime = true;
                    sprayNum = p.getString(sprayCountKey.c_str(),"0").toInt();
                }
            }
            if(foundFutureTime == true) {
              p.end();
              return minDiff;
            }
            else elapseDays++;
        }
        Serial.println("failed 1");
        p.end();
        return 604800;
    }
  }

  if (mode == "preset") {
    int dispInterval = p.getInt("interval", 60);
    sprayNum = p.getInt("dispAmt", 0);
    long nextInterval = getNearestInterval(dispInterval);
    p.end();
    return nextInterval;
  }

  Serial.println("failed 2");
  p.end();
  return 604800;
}

void loadSettings() {
  p.begin("spraySettings",false);

  lastDispense = p.getUInt("last_dispense", 0);
  lastDispenseCt = p.getUInt("last_disp_ct", 0);
  counter = p.getUInt("ct", 0);
  dispLimit = p.getUInt("disp_limit", 0);

  status = p.getBool("status", false);

  isSync = p.getBool("is_sync", false);

  sprayPressDuration = p.getUInt("press_duration", 500);
  pauseBetweenSpray = p.getUInt("pause_spray", 3000);

  dispenser = p.getString("dispenser", "");
  canister = p.getString("canister", "");

  p.end();
}

long getNearestInterval(int dispInterval) {
   struct tm settingTm, currentTm;
    time_t now;
    time(&now);

    // Initialize both structs with current date
    localtime_r(&now, &settingTm);
    localtime_r(&now, &currentTm);
    
    // Calculate the current interval and the nearest future interval
    int currentInterval = (settingTm.tm_min / dispInterval) * dispInterval;
    int nearestMinute = currentInterval + dispInterval;

    // if (nearestMinute >= 60) {
    //     nearestMinute -= 60;
    //     settingTm.tm_hour = (settingTm.tm_hour + 1) % 24;  // Handle hour overflow
    // } else {
    //     settingTm.tm_hour = settingTm.tm_hour;  // Maintain current hour if not rolling over
    // }
     if (nearestMinute >= 60) {
        nearestMinute -= 60;
        settingTm.tm_hour++;  

        if (settingTm.tm_hour >= 24) {  
            settingTm.tm_hour = 0;  // Reset to midnight
            settingTm.tm_mday++;    // Move to the next day
        }
    }

    // Update the minute and second fields
    settingTm.tm_min = nearestMinute;
    settingTm.tm_sec = 0;  // Set seconds to 0 for the next interval

    time_t newTime = mktime(&settingTm);  // Updated time with the nearest interval
    time_t current = mktime(&currentTm);
    long diffSecs = difftime(newTime, current);
    char nearestTimeStr[9];  // Buffer to store the formatted time
    char currentTimeStr[9];
    strftime(nearestTimeStr, sizeof(nearestTimeStr), "%H:%M:%S", localtime(&newTime));
    strftime(currentTimeStr, sizeof(currentTimeStr), "%H:%M:%S", localtime(&current));

    // Print or return the nearest time
    Serial.print("Current time: ");
    Serial.println(currentTimeStr);
    Serial.print("Next nearest time: ");
    Serial.println(nearestTimeStr);
    Serial.print("Seconds left: ");
    Serial.println(diffSecs);

    return diffSecs;
}

void setTimer(String data) {

  const size_t capacity = 50000;
                
  DynamicJsonDocument doc(capacity);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, data);

  // Test if parsing succeeds.
  if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      // return;
  }

  Serial.println("set timer: ");
  Serial.println(data);

  Serial.println("memory usage: ");
  Serial.println(doc.memoryUsage());

  // Extract values
  String timeValue = doc["time"].as<String>();  // "2023-01-23 18:57:19"
  int testSprayCount;
  if(!doc["spray"].isNull()){
      testSprayCount = doc["spray"].as<int>();    
  }
  else{
      testSprayCount = 0;
  }
  String timerMode = doc["mode"].as<String>();

  updateTime(timeValue);
  JsonArray settings = doc["settings"].as<JsonArray>();

  //return if received empty string
  if(timeValue == "" || settings.size() == 0){
      Serial.println("data error from client");
      // return;
  }

  p.begin("sprayTimer",false);
  p.clear();

  Serial.println("Settings: ");
  p.putString("mode", timerMode);

  if (timerMode == "preset") {
    String timeStart = doc["startTime"].as<String>();
    int dispAmt = doc["dispenseAmount"].as<int>();
    int dispInterval = doc["interval"].as<int>();

    p.putInt("interval", dispInterval);
    p.putInt("dispAmt", dispAmt);

  } else if (timerMode == "custom") {
    for(int i = 0; i < settings.size(); i++) {
        JsonVariant setting = settings[i];
        String sprayCountTotalKey = "d"+String(i)+"sct";
        if(setting.size() == 0){
            p.putString(sprayCountTotalKey.c_str(),"0");
            Serial.print("day_");
            Serial.print(i);
            Serial.print("_sprayCountTotal is : ");
            Serial.println("0");
            continue;
        }
        else{
            int sprayCountTotal = setting.size();
            p.putString(sprayCountTotalKey.c_str(),String(sprayCountTotal));
            Serial.print("day_");
            Serial.print(i);
            Serial.print("_sprayCountTotal is : ");
            Serial.println(sprayCountTotal);
            for(int j = 0; j<setting.size();j++){
                JsonVariant setting_daily = setting[j];
                String timeStr = setting_daily["time"];
                String dispenseValue = setting_daily["dispense"];
        
                String sprayCount = "d"+String(i)+"sc" + String(j+1);
                String sprayTime = "d"+String(i)+"st" + String(j+1);
                p.putString(sprayCount.c_str(),dispenseValue);
                p.putString(sprayTime.c_str(),timeStr);
                if(p.getString(sprayTime.c_str(),"-1") == "-1"){
                    Serial.print(sprayTime);
                    Serial.println(" failed to save");
                }
                // Serial.print("Index: ");
                // Serial.print(sprayCount);
                Serial.println(" Time: " + timeStr + " Dispense: " + String(dispenseValue));

                //debug
                // nvsCheck();
            }
        }

    }
  }

  p.end();
  Serial.println("start spraying test");
  sprayCan(testSprayCount, false);

  // BleUpdatedLed();

  if (pServer->getConnectedCount() > 0) {
    bleUpdated = true;
    String response = "TIMER:1";
    pCharacteristic->setValue(response.c_str());
    pCharacteristic->notify();
  }

}

void sync(String data) {

  DynamicJsonDocument doc(1000);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, data);

  // Test if parsing succeeds.
  if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      // return;
  } else {
    String newTimeStr = doc["time"].as<String>();
    String dispenserStr = doc["dispenser"].as<String>();
    String canisterStr = doc["canister"].as<String>();
    int dispLimitInt = doc["dispense_limit"].as<int>();

    updateTime(newTimeStr);

    p.begin("spraySettings", false);
    
    dispenser = p.getString("dispenser", "");
    canister = p.getString("canister", "");

    int countSync = 0;
    if (dispenser == "" && dispenserStr != "") {
      p.putString("dispenser", dispenserStr);
      dispenser = dispenserStr;
      countSync++;
    } else {
      countSync++;
    }
    // Serial.println("disp limit: " + String(value4Str.toInt()));
    dispLimit = dispLimitInt;
    p.putUInt("disp_limit", dispLimit);

    if (canister == "" && canisterStr != "") {
      p.putString("canister", canisterStr);
      p.putUInt("ct", 0);
      canister = canisterStr;
      counter = 0;
      countSync++;
    } else {
      if (canister != canisterStr) {
        p.putString("canister", canisterStr);
        canister = canisterStr;
        counter = 0;
        lastDispenseCt = 0;
        lastDispense = 0;
        p.putUInt("ct", 0);
        p.putUInt("last_dispense", lastDispense);
        p.putUInt("last_disp_ct", lastDispenseCt);
      }
      countSync++;
    }

    if (countSync >= 2) {
      isSync = true;
      status = true;
      p.putBool("is_sync", isSync);
      p.putBool("status", status);
    }

    p.end();
  }

  if (pServer->getConnectedCount() > 0) {
    // bleUpdated = true;
    String response = "SYNC:1";
    pCharacteristic->setValue(response.c_str());
    pCharacteristic->notify();
  }
}

void setConfig(String data) {
  DynamicJsonDocument doc(500);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, data);

  // Test if parsing succeeds.
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
  } else {
    p.begin("spraySettings", false);

    // Convert the extracted strings to integers
    sprayPressDuration = doc["spray_press_duration"].as<int>();
    pauseBetweenSpray = doc["pause_between_spray"].as<int>();

    p.putUInt("press_duration", sprayPressDuration);
    p.putUInt("pause_spray", pauseBetweenSpray);

    // Now you have the values in the variables value1, value2, and value3
    Serial.print("Spray Press Duration: ");
    Serial.println(sprayPressDuration);

    Serial.print("Pause Between Spray: ");
    Serial.println(pauseBetweenSpray);
    
    p.end();
  }

  if (pServer->getConnectedCount() > 0) {
    bleUpdated = true;
    String response = "SET:1";
    pCharacteristic->setValue(response.c_str());
    pCharacteristic->notify();
  }
}

String getInfo() {
  Serial.println("get info");
  p.begin("spraySettings", false);
  String macAddress = getBleMacAddress();

  time_t now;
  time(&now);
  int epochNow = (int)now;

  p.end();
  String info = "GET:" + macAddress + "," + dispenser + "," + canister + "," + String(epochNow) + "," + String(sprayPressDuration) + ","  + String(pauseBetweenSpray) + "," + String(lastDispense) + "," + String(lastDispenseCt) + "," + String(counter) + "," + String(dispLimit) + "," + String(status) + "," + String(isSync);
  
  return info;
}

String getBleMacAddress() {
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

void changeLed(String color) {
  if (color == "red") {
    analogWrite(RED_PIN, 255);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);
  }
  if (color == "green") {
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 255);
    analogWrite(BLUE_PIN, 0);
  }
  if (color == "blue") {
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 255);
  }
  if (color == "orange") {
    analogWrite(RED_PIN, 255);
    analogWrite(GREEN_PIN, 165);
    analogWrite(BLUE_PIN, 0);
  }
}

void assignLed() {
  if (counter >= 0 && counter < 700) {
    changeLed("green");
  }
  if (counter >= 700 && counter < 800) {
    changeLed("orange");
  }
  if (counter >= 800) {
    changeLed("red");
  }
}

void BleEnabledLed() {
  digitalWrite(BLUE_PIN, HIGH);
  delay(750);
  digitalWrite(BLUE_PIN, LOW);
  delay(750);
}

void BleConnectedLed() {
  digitalWrite(BLUE_PIN, HIGH);
}

void BleUpdatedLed() {
  for (int j=0; j <6; j++) {
    digitalWrite(BLUE_PIN, HIGH);
    delay(100);
    digitalWrite(BLUE_PIN, LOW);
    delay(100);
  }

  bleUpdated = false;
}



