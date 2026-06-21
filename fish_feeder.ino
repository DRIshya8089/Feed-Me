#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>

// ---------- CONFIG ----------
char auth[] = "YOUR_BLYNK_AUTH_TOKEN";
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASS";

// Servo pin
const int SERVO_PIN = 18;

// Feeder settings
int feedDuration = 2000; // default in ms (can be changed via Blynk)
bool manualFeed = false;

// ---------- Objects ----------
Servo feederServo;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 5*3600, 60000); // IST offset +5:30

// Blynk Timer
BlynkTimer timer;

// ---------- BLYNK HANDLERS ----------
BLYNK_WRITE(V1) {
  int buttonState = param.asInt();
  if(buttonState == 1) {
    manualFeed = true;
  }
}

BLYNK_WRITE(V2) {
  feedDuration = param.asInt();
}

// ---------- FEED FUNCTION ----------
void feedFish() {
  Serial.println("Feeding...");
  feederServo.write(90);  // Open lid
  delay(feedDuration);
  feederServo.write(0);   // Close lid
  Serial.println("Done.");
}

// checks NTP time
void checkAutoFeed() {
  timeClient.update();

  int currentHour = timeClient.getHours();
  int currentMin  = timeClient.getMinutes();

  // Logging
  Serial.printf("Time: %02d:%02d\n", currentHour, currentMin);

  // Automatic at 08:00 and 20:00
  if (currentMin == 0 && (currentHour == 8 || currentHour == 20)) {
    feedFish();
    delay(60000); // prevent re-triggering same minute
  }

  // Manual
  if (manualFeed) {
    feedFish();
    manualFeed = false;
    Blynk.virtualWrite(V1, 0);
  }
}

void setup() {
  Serial.begin(115200);

  // Blynk + WiFi
  Blynk.begin(auth, ssid, pass);

  // Servo
  feederServo.attach(SERVO_PIN);
  feederServo.write(0); // initial (closed)

  // NTP
  timeClient.begin();

  // Check time every 30 seconds
  timer.setInterval(30000L, checkAutoFeed);
}

void loop() {
  Blynk.run();
  timer.run();
}
