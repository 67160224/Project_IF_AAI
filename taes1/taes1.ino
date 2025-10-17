#include <WiFi.h>
#include <HTTPClient.h>

// ===== WiFi (สามารถคงไว้เพื่อทดสอบเชื่อมต่อได้) =====
const char* ssid = "jung";
const char* password = "12345678";

// ===== HC-SR04 Pin =====
const int trigPin = 5;
const int echoPin = 18;

long duration;
float distance;

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // ---- เชื่อมต่อ Wi-Fi ----
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // ---- อ่านค่าจาก Ultrasonic ----
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2; // แปลงเป็น cm

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  // ---- ตรวจจับวัตถุระยะ 10-15 cm ----
  if (distance >= 10 && distance <= 15) {
    Serial.println("📦 พบวัตถุในระยะ 10-15 cm!");
  } else {
    Serial.println("ไม่มีวัตถุในระยะที่กำหนด");
  }

  delay(1000); // อ่านค่าทุก 1 วินาที
}
