#include <WiFi.h>
#include <WiFiClientSecure.h>

// 🟢 ตั้งค่า WiFi และ LINE TOKEN
const char* ssid = "jung";
const char* password = "12345678";
#define LINE_TOKEN_ULTRA "3d78a029dbbd99173fb918765cf65e0f"      

// 🟢 กำหนดขา Ultrasonic
#define TRIG_PIN 5
#define ECHO_PIN 18

long duration;
float distance;
bool notified = false;           // สถานะแจ้งเตือน (กันซ้ำ)
unsigned long lastNotifyTime = 0; // เวลาที่ส่งแจ้งเตือนล่าสุด (หน่วยมิลลิวินาที)
const unsigned long notifyDelay = 10000; // หน่วงเวลา 10 วินาที (10,000 ms)

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // เชื่อมต่อ WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  delay(2000);
  Serial.println("Ultrasonic Ready!");
}

void loop() {
  // อ่านค่าระยะจาก Ultrasonic
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2; // แปลงเป็น cm

  Serial.print("Distance: ");
  Serial.println(distance);

  // 🟢 ตรวจเงื่อนไขระยะ + หน่วงเวลา
  unsigned long currentTime = millis();
  if (distance >= 10 && distance <= 20) {
    if (!notified && (currentTime - lastNotifyTime > notifyDelay)) {
      String msg = "📦 พบพัสดุในระยะ " + String(distance, 2) + " cm";
      Line_Notify(msg);
      notified = true;
      lastNotifyTime = currentTime; // บันทึกเวลาแจ้งล่าสุด
    }
  } else {
    notified = false; // ถ้าออกนอกระยะให้รีเซ็ตสถานะ
  }

  delay(1000);
}

void Line_Notify(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // ข้ามการตรวจสอบใบรับรอง SSL

  if (!client.connect("notify-api.line.me", 443)) {
    Serial.println("Connection to LINE failed!");
    return;
  }

  String req = "";
  req += "POST /api/notify HTTP/1.1\r\n";
  req += "Host: notify-api.line.me\r\n";
  req += "Authorization: Bearer " + String(LINE_TOKEN_ULTRA) + "\r\n";
  req += "Content-Type: application/x-www-form-urlencoded\r\n";
  req += "User-Agent: ESP32\r\n";

  String payload = "%E0%B8%9E%E0%B8%B1%E0%B8%AA%E0%B8%94%E0%B8%B8%E0%B9%84%E0%B8%94%E0%B9%89%E0%B8%97%E0%B8%B3%E0%B8%81%E0%B8%B2%E0%B8%A3%E0%B8%AA%E0%B9%88%E0%B8%87%E0%B9%81%E0%B8%A5%E0%B9%89%E0%B8%A7";
  req += "Content-Length: " + String(payload.length()) + "\r\n";
  req += "\r\n";
  req += payload;

  client.print(req);
  Serial.println("ส่งข้อความไป LINE แล้ว: " + message);

  delay(100);
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  client.stop();
}
