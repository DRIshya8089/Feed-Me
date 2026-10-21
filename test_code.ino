#include <ESP32Servo.h>



#define SERVO_PIN 18



Servo fishFeederServo;

int testFeedingCount = 0;



void executeShakeSequence(String phaseLabel) {

  fishFeederServo.attach(SERVO_PIN);

  Serial.println("\n--- Shake (" + phaseLabel + ") Started ---");

 

  for (int cycle = 1; cycle <= 3; cycle++) {

    Serial.print("  Cycle ");

    Serial.print(cycle);

    Serial.println("/4:");

   

    for (int pos = 0; pos <= 60; pos += 20) {

      fishFeederServo.write(pos);

      Serial.print("    Moving to Angle: ");

      Serial.print(pos);

      Serial.println(" deg");

      delay(150);

    }

   

    fishFeederServo.write(0);

    Serial.println("    Returning to Angle: 0 deg");

    delay(200);

  }

 

  fishFeederServo.detach(); // Detach to eliminate idle PWM jitter

  Serial.println("--- Shake (" + phaseLabel + ") Completed ---\n");

}



void runFeedingTest() {

  testFeedingCount++;

 

  Serial.println("\n========================================");

  Serial.print("TEST FEEDING SESSION #");

  Serial.print(testFeedingCount);

  Serial.println(" STARTED");

  Serial.println("========================================");



  // 1. Pre-feeding shake

  executeShakeSequence("PRE-FEED");

  delay(500);



  // 2. Main Feeding Rotation (0 to 180 degrees)

  Serial.println("--- Main Dispense Rotation Started ---");

  fishFeederServo.attach(SERVO_PIN);

 //for (int rot = 1; rot <= 3; rot++) {

  for (int pos = 0; pos <= 180; pos += 40) {

    fishFeederServo.write(pos);

    Serial.print("  Dispense Position: ");

    Serial.print(pos);

    Serial.println(" deg");

    delay(350);

  }
 //}
 

  // Return to home position

  fishFeederServo.write(0);

  Serial.println("  Returning to Home Position: 0 deg");

  delay(300);

  fishFeederServo.detach(); // Detach to eliminate idle PWM jitter

  Serial.println("--- Main Dispense Rotation Completed ---\n");



  delay(500);



  // 3. Post-feeding shake

  executeShakeSequence("POST-FEED");



  Serial.println("========================================");

  Serial.print("TEST FEEDING SESSION #");

  Serial.print(testFeedingCount);

  Serial.println(" COMPLETED SUCCESSFULLY!");

  Serial.println("========================================\n");

  Serial.println("Type 's' and press Enter to test again.");

}



void setup() {

  Serial.begin(115200);

  delay(1000);



  Serial.println("\n--- ESP32 Servo Angle Tracking Test ---");

  Serial.println("Servo Signal Pin: GPIO 18");

  Serial.println("Type 's' in the Serial Monitor and hit Send/Enter to trigger the feeder sequence.");

}



void loop() {

  if (Serial.available() > 0) {

    char inputChar = Serial.read();



    // Trigger on 's' or 'S'

    if (inputChar == 's' || inputChar == 'S') {

      runFeedingTest();

    }

  }

} 

