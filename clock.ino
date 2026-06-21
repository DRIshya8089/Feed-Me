#include <WiFi.h>
#include <time.h>

// --- Configuration ---
const char *ssid = "SIVAN";
const char *password = "anilkrishnan0638";

const int digitPins[] = { 21, 22, 23 }; 
const int segmentPins[] = { 13, 15, 14, 27, 26, 12, 25, 24 }; // a,b,c,d,e,f,g,dp

// Standard numbers 0-9 (Common Anode: 0 is ON)
const byte numbers[10] = {
  0b11000000, 0b11111001, 0b10100100, 0b10110000, 0b10011001, 
  0b10010010, 0b10000010, 0b11111000, 0b10000000, 0b10010000 
};

// --- Custom Patterns mapped from your sketches ---
// Pattern 10: Segments f, g, b, c, d (Looks like a lower-case 'b' or square)
const byte pattern10 = 0b10100011; 

// Pattern 11: Keeping your existing vertical bars
const byte pattern11 = 0b11000000; 

// Pattern 12: Segments a, f, g, e, d (Looks like the letter 'E')
const byte pattern12 = 0b11000000;

// Custom Letter Patterns for Alarms
const byte charH = 0b10001001; 
const byte char2 = 0b10100100;
const byte charO = 0b11000000;
const byte charF = 0b10001110; 
const byte chard = 0b10100001;

const int buzzerPin = 18;
const int buttonPin = 4;

bool alarmActive = false;
int alarmType = 0; 

void setup() {
  for (int i = 0; i < 8; i++) pinMode(segmentPins[i], OUTPUT);
  for (int i = 0; i < 3; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], LOW); 
  }
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP); // Corrected for button to GND

  WiFi.begin(ssid, password);
  configTime(19800, 0, "pool.ntp.org"); 
}

void loop() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  // --- Alarm Logic ---
  if ((timeinfo.tm_min == 0 || timeinfo.tm_min == 30) && timeinfo.tm_sec == 0) {
    alarmActive = true; alarmType = 1; 
  }
  if ((timeinfo.tm_hour == 8 || timeinfo.tm_hour == 13 || timeinfo.tm_hour == 19) 
      && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
    alarmActive = true; alarmType = 2;
  }

  // --- Button Dismiss ---
  if (digitalRead(buttonPin) == LOW) { 
    alarmActive = false;
    digitalWrite(buzzerPin, LOW);
  }

  if (alarmActive) {
    digitalWrite(buzzerPin, (millis() % 1000 < 500)); 
    showWord(alarmType); 
  } else {
    digitalWrite(buzzerPin, LOW);
    int h12 = timeinfo.tm_hour % 12;
    if (h12 == 0) h12 = 12; 
    driveDisplay(h12, timeinfo.tm_min);
  }
}

void driveDisplay(int h, int m) {
  byte firstDigit;

  if (h == 10)      firstDigit = pattern10;
  else if (h == 11) firstDigit = pattern11;
  else if (h == 12) firstDigit = pattern12;
  else              firstDigit = numbers[h]; 

  byte minuteTens = numbers[m / 10];
  byte minuteOnes = numbers[m % 10];

  byte displayData[3] = {firstDigit, minuteTens, minuteOnes};

  for (int i = 0; i < 3; i++) {
    displayRaw(displayData[i]);
    digitalWrite(digitPins[i], HIGH);
    delay(5);
    digitalWrite(digitPins[i], LOW);
  }
}

void showWord(int type) {
  byte pattern[3];
  if (type == 1) { 
    pattern[0] = charH; pattern[1] = char2; pattern[2] = charO;
  } else {         
    pattern[0] = charF; pattern[1] = charO; pattern[2] = chard;
  }
  for (int i = 0; i < 3; i++) {
    displayRaw(pattern[i]);
    digitalWrite(digitPins[i], HIGH);
    delay(5);
    digitalWrite(digitPins[i], LOW);
  }
}

void displayRaw(byte b) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(segmentPins[i], bitRead(b, i));
  }
}