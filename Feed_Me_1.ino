/* Final deployed version with OTA updates */

#define BLYNK_TEMPLATE_ID "-----------"
#define BLYNK_TEMPLATE_NAME "----------"
#define BLYNK_AUTH_TOKEN "--------------"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <Preferences.h>

// Async Web Server + ElegantOTA Configuration
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

// WiFi Configuration
char ssid[] = "-----------";
char pass[] = "----------------";

// Pin & System Constants
#define SERVO_PIN 18
#define BATTERY_PIN 34 
#define MAX_SCHEDULES 4 
#define uS_TO_S_FACTOR 1000000ULL 
#define TWO_HOURS_IN_SEC 7200ULL

struct Schedule {
  bool active = false;
  int hour = -1;
  int minute = -1;
};

// -------------------------------------------------------------
// RTC MEMORY & PARAMETERS - Retained across ESP32 Deep Sleep
// -------------------------------------------------------------
RTC_DATA_ATTR Schedule schedules[MAX_SCHEDULES];
RTC_DATA_ATTR int feedingCount = 0;
RTC_DATA_ATTR char rtcSleepLogs[1024] = ""; 

// Configurable Feeding Parameters via Blynk Sliders & Switches
RTC_DATA_ATTR int feedStepDeg = 30;     // Default 30 degrees increment
RTC_DATA_ATTR int feedDelayMs = 350;    // Default 350 ms delay
RTC_DATA_ATTR bool shakeEnabled = true; // Default shake enabled
RTC_DATA_ATTR bool isSleepDisabled = false; // V5 Stay-Awake Override Switch

Servo fishFeederServo;
WidgetRTC rtc;
Preferences preferences;
AsyncWebServer server(80);

// Voltage divider calculation factors
const float R1 = 10000.0;
const float R2 = 4700.0;
const float maxBatteryVoltage = 8.4;

void sendLog(String msg) {
  Serial.print(msg);
  Blynk.virtualWrite(V3, msg);
}

String getFullTimestamp() {
  String dateStr = String(day()) + "/" + String(month()) + "/" + String(year());
  String timeStr = (hour() < 10 ? "0" + String(hour()) : String(hour())) + ":" + 
                   (minute() < 10 ? "0" + String(minute()) : String(minute()));
  return dateStr + " " + timeStr;
}

String getTimeOnly(time_t t) {
  String timeStr = (hour(t) < 10 ? "0" + String(hour(t)) : String(hour(t))) + ":" + 
                   (minute(t) < 10 ? "0" + String(minute(t)) : String(minute(t)));
  return timeStr;
}

void appendSleepLog(String logEntry) {
  if (strlen(rtcSleepLogs) + logEntry.length() < sizeof(rtcSleepLogs) - 1) {
    strcat(rtcSleepLogs, logEntry.c_str());
  }
}

void checkBatteryStatus() {
  int rawADC = analogRead(BATTERY_PIN);
  float pinVoltage = (rawADC / 4095.0) * 3.3;
  float batteryVoltage = pinVoltage * ((R1 + R2) / R2);
  
  float batteryPercentage = (batteryVoltage / maxBatteryVoltage) * 100.0;
  if(batteryPercentage > 100.0) batteryPercentage = 100.0;
  if(batteryPercentage < 0.0) batteryPercentage = 0.0;
  
  Blynk.virtualWrite(V2, batteryPercentage);
}

void executeShakeSequence(String phaseLabel) {
  if (!shakeEnabled) {
    sendLog("[" + getFullTimestamp() + "] Shake (" + phaseLabel + ") Skipped (Disabled via Blynk)\n");
    return;
  }

  fishFeederServo.attach(SERVO_PIN);
  sendLog("[" + getFullTimestamp() + "] Shake (" + phaseLabel + ") Started\n");
  for (int cycle = 1; cycle <= 3; cycle++) {
    for (int pos = 0; pos <= 60; pos += 20) {
      fishFeederServo.write(pos);
      delay(150);
    }
    fishFeederServo.write(0);
    delay(200); 
  }
  fishFeederServo.detach();
}

long getSecondsToNextFeed(int currentHour, int currentMin, int currentSec, String &nextFeedTimeStr) {
  long minSecondsToWait = -1;
  int nextH = -1, nextM = -1;

  int currentMinOfDay = (currentHour * 60) + currentMin;

  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (schedules[i].active) {
      int schedMinOfDay = (schedules[i].hour * 60) + schedules[i].minute;
      int diffMin = schedMinOfDay - currentMinOfDay;
      
      if (diffMin <= 0) {
        diffMin += 1440; // 24 hours in minutes
      }

      long diffSec = (diffMin * 60) - currentSec;

      if (minSecondsToWait == -1 || diffSec < minSecondsToWait) {
        minSecondsToWait = diffSec;
        nextH = schedules[i].hour;
        nextM = schedules[i].minute;
      }
    }
  }

  if (nextH != -1) {
    nextFeedTimeStr = (nextH < 10 ? "0" : "") + String(nextH) + ":" + (nextM < 10 ? "0" : "") + String(nextM);
  } else {
    nextFeedTimeStr = "None Scheduled";
  }

  return minSecondsToWait;
}

void executeFeedingSequence(String nextFeedStr) {
  String feedStartTime = getFullTimestamp();
  
  feedingCount++;
  preferences.putInt("feed_count", feedingCount);
  Blynk.virtualWrite(V4, feedingCount);

  sendLog("\n========================================\n");
  sendLog("[" + feedStartTime + "] FEEDING SESSION #" + String(feedingCount) + " STARTED\n");

  executeShakeSequence("PRE-FEED");

  fishFeederServo.attach(SERVO_PIN);
  for (int pos = 0; pos <= 180; pos += feedStepDeg) {
    fishFeederServo.write(pos);
    delay(feedDelayMs);
  }
  if (fishFeederServo.read() != 180) {
    fishFeederServo.write(180);
    delay(feedDelayMs);
  }
  fishFeederServo.write(0);
  delay(300); 
  fishFeederServo.detach();

  executeShakeSequence("POST-FEED");

  String feedEndTime = getFullTimestamp();
  sendLog("[" + feedEndTime + "] FEEDING SESSION #" + String(feedingCount) + " COMPLETED\n");
  sendLog("Next Scheduled Feed: " + nextFeedStr + "\n");

  String emailDetails = String("Fish Feeding Complete!\n\n") +
                        "Session: #" + String(feedingCount) + "\n" +
                        "Started At: " + feedStartTime + "\n" +
                        "Ended At: " + feedEndTime + "\n" +
                        "Next Scheduled Feed: " + nextFeedStr + "\n\n" +
                        "--- Sleep/Wake Logs since last feed ---\n" +
                        String(rtcSleepLogs);
  
  Blynk.logEvent("feeding_notification", emailDetails);

  // --- REFILL CHECK NOTIFICATION (Triggered every 5 feeds) ---
  if (feedingCount > 0 && (feedingCount % 5 == 0)) {
    sendLog("[" + feedEndTime + "] REFILL WARNING: Total Feeds = " + String(feedingCount) + ". Sending refill notification.\n");
    String refillMsg = "Feeder container check required! Total feedings completed: " + String(feedingCount) + ". Please check food level.";
    Blynk.logEvent("refill_notification", refillMsg);
  }
  
  for (int i = 0; i < 30; i++) {
    Blynk.run();
    delay(100);
  }
  
  rtcSleepLogs[0] = '\0';
}

void processTimeInput(int index, const BlynkParam& param) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    schedules[index].active = true;
    schedules[index].hour = t.getStartHour();
    schedules[index].minute = t.getStartMinute();
  } else {
    schedules[index].active = false;
    schedules[index].hour = -1;
    schedules[index].minute = -1;
  }
}

// Blynk Virtual Pin Handlers
BLYNK_WRITE(V10) { processTimeInput(0, param); }
BLYNK_WRITE(V11) { processTimeInput(1, param); }
BLYNK_WRITE(V12) { processTimeInput(2, param); }
BLYNK_WRITE(V13) { processTimeInput(3, param); }

// V5: Stay-Awake / Sleep Override Switch
BLYNK_WRITE(V5) {
  isSleepDisabled = (param.asInt() == 1);
  if (isSleepDisabled) {
    sendLog("\n[STAY-AWAKE MODE ACTIVATED] Continuous operation active via Blynk (V5).\n");
    sendLog("Device online for Web/OTA updates at http://" + WiFi.localIP().toString() + "/update\n");
  } else {
    sendLog("\n[STAY-AWAKE MODE DEACTIVATED] Deep Sleep Re-enabled via Blynk (V5).\n");
  }
}

// V6: Reset Feeding Counter Button
BLYNK_WRITE(V6) {
  if (param.asInt() == 1) {
    feedingCount = 0;
    preferences.putInt("feed_count", 0);
    Blynk.virtualWrite(V4, feedingCount);
    sendLog("[" + getFullTimestamp() + "] Feeding Counter Reset to 0\n");
  }
}

// V7: Angle Increment Step Slider
BLYNK_WRITE(V7) {
  int val = param.asInt();
  if (val > 0) feedStepDeg = val;
}

// V8: Feeding Speed Delay Slider
BLYNK_WRITE(V8) {
  int val = param.asInt();
  if (val > 0) feedDelayMs = val;
}

// V9: Shake Enable/Disable Switch
BLYNK_WRITE(V9) {
  shakeEnabled = (param.asInt() == 1);
}

BLYNK_CONNECTED() {
  rtc.begin();
  // Sync schedules, counters, switches, and parameters from Blynk cloud
  Blynk.syncVirtual(V5, V6, V7, V8, V9, V10, V11, V12, V13);
}

void setup() {
  unsigned long activeStartTime = millis();

  Serial.begin(115200);
  analogReadResolution(12);

  preferences.begin("feeder", false);
  feedingCount = preferences.getInt("feed_count", feedingCount);

  // --- CLEAN & RELIABLE BLYNK CONNECT ---
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Fallback safety if Wi-Fi connection fails on wake
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed! Sleeping 2 mins before retry...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(120ULL * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  }

  // Configure Async Web Server and ElegantOTA
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "ESP32 Fish Feeder Web Server Active. Go to /update for OTA updates.");
  });
  ElegantOTA.begin(&server);
  server.begin();

  // --- Strict RTC & Cloud Sync Lock ---
  unsigned long startSync = millis();
  while ((timeStatus() != timeSet || year() < 2020) && (millis() - startSync < 10000)) {
    Blynk.run();
    delay(100);
  }

  checkBatteryStatus();

  // Allow virtual pin handlers and schedule inputs to sync from Blynk cloud
  for (int i = 0; i < 20; i++) {
    Blynk.run();
    delay(100);
  }

  String wakeTimestamp = getFullTimestamp();
  String ipAddr = WiFi.localIP().toString();

  // Calculate Next Feed and Estimated Next Wake-Up Time
  String nextFeedStr = "";
  long secondsToNextFeed = getSecondsToNextFeed(hour(), minute(), second(), nextFeedStr);
  
  uint64_t estSleepSec = TWO_HOURS_IN_SEC;
  if (secondsToNextFeed > 0 && secondsToNextFeed <= 9000) {
    if (secondsToNextFeed > 60) {
      estSleepSec = secondsToNextFeed - 60ULL;
    } else {
      estSleepSec = 30ULL;
    }
  }
  if (estSleepSec < 30ULL) estSleepSec = 30ULL;

  String nextWakeTimeStr = "";
  if (isSleepDisabled) {
    nextWakeTimeStr = "Always Awake (V5 ON)";
  } else {
    time_t nextWakeTime = now() + estSleepSec + 60; // Include active 60-second window offset
    nextWakeTimeStr = getTimeOnly(nextWakeTime);
  }

  // Print IP and wake status to Terminal
  sendLog("\n========================================\n");
  sendLog("[" + wakeTimestamp + "] ESP32 Woke Up | IP: " + ipAddr + "\n");
  sendLog("Next Feed Slot: " + nextFeedStr + "\n");
  sendLog("Est. Next Wake: " + nextWakeTimeStr + "\n");

  // Send Blynk App Wake Notification with Next Feed Slot & Wake Time
  String wakeMsg = "Feeder Woke Up at " + wakeTimestamp + "\n" +
                   "IP: " + ipAddr + "\n" +
                   "Next Feed Slot: " + nextFeedStr + "\n" +
                   "Next Wake-Up: " + nextWakeTimeStr;
                   
  Blynk.logEvent("wake_notification", wakeMsg);

  appendSleepLog("Woke up at: " + wakeTimestamp + "\n");

  // --- CONTINUOUS ACTIVE ENGINE WHEN SLEEP IS DISABLED (V5 = 1) ---
  if (isSleepDisabled) {
    sendLog("=== ALWAYS-AWAKE ACTIVE ENGINE ENABLED ===");
    sendLog("Deep sleep suspended. All device features active at IP: " + WiFi.localIP().toString() + "\n");

    int lastCheckedMinute = -1;
    unsigned long lastBatteryCheck = 0;

    // Run active continuous loop as long as V5 remains enabled
    while (isSleepDisabled) {
      Blynk.run();

      int currentH = hour();
      int currentM = minute();
      int currentS = second();

      // Real-time minute schedule evaluator
      if (currentM != lastCheckedMinute) {
        lastCheckedMinute = currentM;

        for (int i = 0; i < MAX_SCHEDULES; i++) {
          if (schedules[i].active && schedules[i].hour == currentH && schedules[i].minute == currentM) {
            String activeNextFeedStr = "";
            getSecondsToNextFeed(currentH, currentM, currentS, activeNextFeedStr);
            
            // Execute feeding immediately when slot matches
            executeFeedingSequence(activeNextFeedStr);
            break;
          }
        }
      }

      // Refresh battery telemetry every 30 seconds while awake
      if (millis() - lastBatteryCheck >= 30000) {
        lastBatteryCheck = millis();
        checkBatteryStatus();
      }

      delay(100); // yield for system processes
    }

    sendLog("\nResuming standard power management and sleep cycles...\n");
  }

  // --- STANDARD DEEP SLEEP FEEDING CHECK ---
  int currentH = hour();
  int currentM = minute();

  int targetSchedIndex = -1;
  for (int i = 0; i < MAX_SCHEDULES; i++) {
    if (schedules[i].active) {
      if (schedules[i].hour == currentH && schedules[i].minute == currentM) {
        targetSchedIndex = i;
        break;
      }
      
      int targetMin = schedules[i].minute - 1;
      int targetHour = schedules[i].hour;
      if (targetMin < 0) {
        targetMin = 59;
        targetHour = (targetHour - 1 + 24) % 24;
      }
      if (targetHour == currentH && targetMin == currentM) {
        targetSchedIndex = i;
        break;
      }
    }
  }

  if (targetSchedIndex != -1) {
    int targetH = schedules[targetSchedIndex].hour;
    int targetM = schedules[targetSchedIndex].minute;

    if (hour() != targetH || minute() != targetM) {
      sendLog("[" + getFullTimestamp() + "] Early Wake: Preparing for feed at " + 
              (targetH < 10 ? "0" : "") + String(targetH) + ":" + 
              (targetM < 10 ? "0" : "") + String(targetM) + "\n");

      unsigned long lastCountTime = 0;
      
      while (hour() != targetH || minute() != targetM) {
        Blynk.run();
        
        if (millis() - lastCountTime >= 1000) {
          lastCountTime = millis();
          int secsRemaining = 60 - second();
          if (secsRemaining < 0) secsRemaining = 0;
          
          sendLog("Next feed starts in " + String(secsRemaining) + " sec...\n");
        }
        delay(100);
      }
    }

    getSecondsToNextFeed(hour(), minute(), second(), nextFeedStr);
    executeFeedingSequence(nextFeedStr);
  }

  secondsToNextFeed = getSecondsToNextFeed(hour(), minute(), second(), nextFeedStr);

  sendLog("Next Scheduled Feed: " + nextFeedStr + "\n");

  uint64_t sleepDurationSec = TWO_HOURS_IN_SEC; 

  // Dynamic sleep calculation (up to 2.5 hours / 9000 seconds)
  if (secondsToNextFeed > 0 && secondsToNextFeed <= 9000) {
    if (secondsToNextFeed > 60) {
      sleepDurationSec = secondsToNextFeed - 60ULL; 
    } else {
      sleepDurationSec = 30ULL;
    }
  }

  if (sleepDurationSec < 30ULL) sleepDurationSec = 30ULL;

  unsigned long sleepMinutes = (unsigned long)(sleepDurationSec / 60);

  String sleepTimestamp = getFullTimestamp();
  appendSleepLog("Entering sleep at: " + sleepTimestamp + " (Duration: " + String(sleepMinutes) + " mins)\n");
  
  sendLog("[" + sleepTimestamp + "] Entering Deep Sleep for " + String(sleepMinutes) + " mins...\n\n");

  // Servo auto-homing (0° reset)
  fishFeederServo.attach(SERVO_PIN);
  fishFeederServo.write(0);
  delay(300);
  fishFeederServo.detach();

  // Enforce 60-second minimum active window before sleep
  const unsigned long MIN_ACTIVE_TIME_MS = 60000;
  while (millis() - activeStartTime < MIN_ACTIVE_TIME_MS) {
    Blynk.run();
    delay(100);
  }

  // Network teardown
  Blynk.disconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  
  esp_sleep_enable_timer_wakeup(sleepDurationSec * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
}
