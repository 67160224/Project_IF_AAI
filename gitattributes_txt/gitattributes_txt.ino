int buzzer = 22;
int digitalPin = 21;
int val = 0;
void setup() {
  pinMode(buzzer, OUTPUT);
  pinMode(digitalPin, INPUT);
  Serial.begin(9600);
}

void loop() {
  val = digitalRead(digitalPin);
  Serial.print("val = ");
  Serial.println(val);
  if (val == 0) {
    digitalWrite(buzzer, HIGH);
  }
  else {
    digitalWrite(buzzer, LOW);
  }
  delay(100);
}