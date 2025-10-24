#include <ESP32Servo.h> // 1. Correct syntax for including the library

Servo servo1;             // 2. Declare the Servo object

void setup() {
  // put your setup code here, to run once:
  
  servo1.attach(2);
  delay(100);
}

void loop() {
  // put your main code here, to run repeatedly:
  servo1.write(0);
  delay(1000);

  servo1.write(90);
  delay(1000);

  servo1.write(180);
  delay(1000);
  
}