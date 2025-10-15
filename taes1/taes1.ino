#include <WiFi.h>
#include <HTTPClient.h>

// WiFi
const char* ssid = "jung";
const char* password = "12345678";

// LINE Notify Token
String lineToken = "3d78a029dbbd99173fb918765cf65e0f";

// HC-SR04
const int trigPin = 5;
const int echoPin = 18;
long duration;
float distance;

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
}

void loop() {
  // อ่านค่า Ultrasonic
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;

  Serial.print("Distance: ");
  Serial.println(distance);

  // ถ้าพัสดุอยู่ 10-20 cm ส่ง LINE Notify
  if(distance >= 10 && distance <= 20){
    sendLineNotify("📦 พบพัสดุ! ระยะ: " + String(distance) + " ซม.");
    delay(5000); // รอ 5 วินาที ป้องกันส่งซ้ำรัว
  }

  delay(500);
}

void sendLineNotify(String message){
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;
    http.begin("https://notify-api.line.me/api/notify");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Bearer " + lineToken);

    String postData = "message=" + message;
    int httpCode = http.POST(postData);

    if(httpCode > 0){
      Serial.println("Sent to LINE!");
    } else {
      Serial.println("Error sending LINE");
    }

    http.end();
  }
}
