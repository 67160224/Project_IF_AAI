#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <esp_system.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WebServer.h>

WebServer server(80);

// --------- WiFi / Telegram config (เปลี่ยนตามของคุณ) ----------
const char* ssid = "jung"; 
const char* password = "12345678"; 
String BOTtoken = "8351327796:AAH4HErPl67KCM-BUqRcoEK-N2_lpwfzbFs"; 
String CHAT_ID = "8498711569"; 

WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);

// --------- App variables (Modified for conditional capture flow) ----------
#define STATE_IDLE 0
#define STATE_INITIATE_CAPTURE 1 // ใช้สำหรับเริ่ม Timed Flash Sequence (ถ้า flashState เป็น HIGH)
#define STATE_FLASH_ON_DELAY 2
#define STATE_CAPTURE 3        // ใช้เป็น Common state สำหรับ Capture/Send (ทั้งแบบมี/ไม่มีแฟลช)

volatile int captureState = STATE_IDLE; // ตัวแปรสถานะ
unsigned long lastTriggerTime = 0;
const unsigned long cooldown = 10000;  // 10s cooldown
const unsigned long flashDelayMs = 2500; // หน่วงเวลาเปิดแฟลช (2.5 วินาที)
unsigned long flashOnStartTime = 0;
String statusMessage = ""; // ข้อความแสดงผลลัพธ์บนหน้าเว็บ

#define FLASH_LED_PIN 4
bool flashState = LOW; // สถานะแฟลชที่ควบคุมโดยผู้ใช้/คำสั่ง
// Flag ใหม่: ใช้เพื่อระบุว่าแฟลชถูกเปิดโดย State Machine และต้องปิดเองหลังถ่ายเสร็จหรือไม่
bool shouldTurnFlashOffAfterCapture = false; 

int botRequestDelay = 1000;
unsigned long lastTimeBotRan = 0;

// --------- CAMERA_MODEL_AI_THINKER (ใช้ pinout เดิมของคุณ) ----------
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ================= Camera init with retry and fallback =================
void configInitCameraWithRetry() {
  Serial.printf("configInitCameraWithRetry(): psramFound=%d freeHeap=%u\n", psramFound(), esp_get_free_heap_size());
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= 3; ++attempt) {
    Serial.printf("Camera init attempt %d (xclk=%u frame=%d) ...\n", attempt, (unsigned)config.xclk_freq_hz, config.frame_size);
    err = esp_camera_init(&config);
    if (err == ESP_OK) {
      Serial.println("Camera init OK");
      // configure some defaults
      sensor_t* s = esp_camera_sensor_get();
      if (s) {
        s->set_brightness(s, 0); 
        s->set_contrast(s, 2); 
        s->set_saturation(s, -2); 
        s->set_whitebal(s, 1); 
        s->set_awb_gain(s, 1); 
        s->set_wb_mode(s, 0); 
        s->set_exposure_ctrl(s, 1); 
        s->set_aec2(s, 0); 
        s->set_ae_level(s, 0); 
        s->set_aec_value(s, 300); 
        s->set_gain_ctrl(s, 1); 
        s->set_agc_gain(s, 0); 
        s->set_gainceiling(s, (gainceiling_t)0); 
        s->set_bpc(s, 0); 
        s->set_wpc(s, 1); 
        s->set_raw_gma(s, 1); 
        s->set_lenc(s, 1); 
        s->set_hmirror(s, 0); 
        s->set_vflip(s, 0); 
        s->set_dcw(s, 1); 
        s->set_colorbar(s, 0); 
        s->set_framesize(s, FRAMESIZE_VGA);
      }
      return;
    }
    Serial.printf("Camera init failed (0x%x)\n", err);
    esp_camera_deinit();
    delay(200);
    // Fallback adjustments for next attempt
    if (attempt == 1) {
      // reduce frame and xclk
      config.frame_size = FRAMESIZE_QVGA;
      config.xclk_freq_hz = 10000000;
    } else if (attempt == 2) {
      // further reduce
      config.frame_size = FRAMESIZE_QQVGA;
      config.xclk_freq_hz = 8000000;
    }
    // loop continue
  }

  Serial.printf("Camera init permanently failed with 0x%x\n", err);
}

// ================= Handle incoming Telegram messages =================
void handleNewMessages(int numNewMessages) {
  Serial.printf("Handle New Messages: %d\n", numNewMessages);
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;
    Serial.printf("From %s: %s\n", bot.messages[i].from_name.c_str(), text.c_str());
    
    // แปลงข้อความภาษาไทยเป็นคำสั่ง (ต้องบันทึกไฟล์เป็น UTF-8)
    if (text == "ถ่ายรูป") text = "/photo";
    
    if (text == "/camera") {
      String welcome = "📦 ยินดีต้อนรับคุณ " + bot.messages[i].from_name + "\n";
      welcome += "นี่คือกล้องของ Smart Parcel Box คุณสามารถใช้คำสั่งต่อไปนี้ \n\n";
      welcome += "📷 /photo : ถ่ายภาพทันที\n";
      welcome += "💡 /flash : เปิด/ปิด ไฟแฟลช\n";
      bot.sendMessage(CHAT_ID, welcome, "");
    } else if (text == "/flash") {
      flashState = !flashState;
      digitalWrite(FLASH_LED_PIN, flashState);
      Serial.println("Flash toggled");
      // ส่งสถานะกลับ Telegram
      bot.sendMessage(CHAT_ID, "Flash ถูก " + String(flashState ? "เปิด 💡" : "ปิด"), "");
    } else if (text == "/photo") {
      // Telegram capture is always immediate capture (no timed flash sequence)
      // and it respects the current flashState.
      if (flashState == HIGH) {
        Serial.println("Photo requested via Telegram (Immediate capture with flash ON).");
      } else {
        Serial.println("Photo requested via Telegram (Immediate capture with flash OFF).");
      }
      // Note: We don't set shouldTurnFlashOffAfterCapture here, as manual toggle implies manual control.
      shouldTurnFlashOffAfterCapture = false; 
      captureState = STATE_CAPTURE; 
    }
  }
}

// ================= Send photo to Telegram (multipart HTTPS) =================
String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getBody = "";

  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb); 
  delay(50);
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }

  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("Connected to api.telegram.org");
    String head = "--theinfoflux\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + CHAT_ID + "\r\n--theinfoflux\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--theinfoflux--\r\n";
    uint32_t imageLen = fb->len;
    uint32_t totalLen = imageLen + head.length() + tail.length();

    clientTCP.println("POST /bot" + BOTtoken + "/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=theinfoflux");
    clientTCP.println();
    clientTCP.print(head);
    
    // send image buffer in chunks
    uint8_t* buf = fb->buf;
    size_t remaining = fb->len;
    while (remaining > 0) {
      size_t chunk = remaining > 1024 ? 1024 : remaining;
      clientTCP.write(buf, chunk);
      buf += chunk;
      remaining -= chunk;
    }

    clientTCP.print(tail);
    esp_camera_fb_return(fb);
    
    // wait and read response (with timeout)
    long start = millis();
    while (millis() - start < 10000) {
      while (clientTCP.available()) {
        char c = clientTCP.read();
        getBody += c;
      }
      if (getBody.length() > 0) break;
      delay(10);
    }
    clientTCP.stop();
    Serial.println("Telegram response:");
    Serial.println(getBody);
  } else {
    Serial.println("Connection to api.telegram.org failed.");
    getBody = "Connect failed";
  }
  return getBody;
}

// ================= Web server endpoints and handlers =================

// Handler สำหรับถ่ายภาพ (เริ่มกระบวนการ State Machine แบบมีเงื่อนไข)
void handleCapture() {
  unsigned long now = millis();
  if (captureState == STATE_IDLE && now - lastTriggerTime > cooldown) {
    lastTriggerTime = now;
    
    // *** NEW LOGIC: Check flashState to decide flow ***
    if (flashState == HIGH) {
        // แฟลชถูกเปิดอยู่ -> รันกระบวนการ Timed Flash Sequence
        captureState = STATE_INITIATE_CAPTURE; // State 1: Start flash ON/Delay
        shouldTurnFlashOffAfterCapture = true; // ตั้งค่าให้ต้องปิดแฟลชหลังจบ
        // อัปเดตสถานะเริ่มต้น (แจ้งว่าได้รับคำสั่งแล้ว)
        statusMessage = "✅ คำสั่งรับทราบ! กำลังเริ่มขั้นตอนถ่ายภาพอัตโนมัติ (จะเปิดแฟลชชั่วคราว) กรุณารอประมาณ 15 วินาที..."; 
        server.send(200, "text/plain", "กำลังเริ่มขั้นตอนถ่ายภาพ...");
    } else {
        // แฟลชถูกปิดอยู่ -> รันกระบวนการ Immediate Capture
        captureState = STATE_CAPTURE; // State 3: Jump directly to capture/send
        shouldTurnFlashOffAfterCapture = false; // ไม่ต้องปิดแฟลชหลังจบ
        // อัปเดตสถานะเริ่มต้น (แจ้งว่าได้รับคำสั่งแล้ว)
        statusMessage = "✅ คำสั่งรับทราบ! กำลังถ่ายภาพและส่งทันที (ไม่ใช้แฟลช) กรุณารอประมาณ 10 วินาที...";
        server.send(200, "text/plain", "กำลังเริ่มขั้นตอนถ่ายภาพทันที...");
    }
    // *** END NEW LOGIC ***
    
  } else if (captureState != STATE_IDLE) {
      server.send(200, "text/plain", "ขั้นตอนถ่ายภาพกำลังดำเนินอยู่... กรุณารอสักครู่");
  } else {
    String message = "กรุณารอสักครู่ (Cool-down " + String((cooldown - (now - lastTriggerTime)) / 1000.0, 1) + " วินาที)";
    server.send(200, "text/plain", message);
  }
}

// Handler สำหรับเปิด/ปิดแฟลช
void handleFlashToggle() {
  String mode = server.arg("mode");
  if (mode == "on") {
    flashState = HIGH;
    digitalWrite(FLASH_LED_PIN, HIGH);
    server.send(200, "text/plain", "เปิดแฟลชสำเร็จ 💡");
  } else if (mode == "off") {
    flashState = LOW;
    digitalWrite(FLASH_LED_PIN, LOW);
    server.send(200, "text/plain", "ปิดแฟลชสำเร็จ");
  } else {
    server.send(400, "text/plain", "คำสั่งไม่ถูกต้อง");
  }
}


// Handler สำหรับหน้าแรก (แสดงสถานะและข้อความ)
void handleRoot() {
  String html = "";
  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<meta charset='UTF-8'>"; // <<<-- สำคัญมากสำหรับภาษาไทยในเว็บ
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Box Camera Control</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #f0f4f8; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; }";
  html += ".container { background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0, 0, 0, 0.1); text-align: center; width: 90%; max-width: 400px; }";
  html += "h1 { color: #007bff; margin-bottom: 25px; font-size: 1.8em; }";
  html += "p { color: #555; margin-bottom: 10px; font-size: 1em; }";
  html += "strong { color: #333; }";
  html += ".status-box { background: " + String(WiFi.status() == WL_CONNECTED ? "#e6ffe6" : "#ffe6e6") + "; padding: 10px; border-radius: 8px; margin-bottom: 20px; border: 1px solid " + String(WiFi.status() == WL_CONNECTED ? "#00cc00" : "#cc0000") + "; }";
  html += ".flash-state { color: " + String(flashState ? "#ffc107" : "#6c757d") + "; font-weight: bold; }";
  html += ".btn-group { margin-top: 15px; display: flex; justify-content: center; gap: 10px; margin-bottom: 25px; }";
  html += ".btn { border: none; padding: 12px 25px; border-radius: 8px; font-size: 1.1em; cursor: pointer; transition: background-color 0.3s, transform 0.1s; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); }";
  html += ".btn:active { transform: translateY(1px); box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1); }";
  // เพิ่มสไตล์สำหรับปุ่มที่ถูกปิดใช้งาน (Disable)
  html += ".btn:disabled { background-color: #cccccc !important; color: #666666 !important; cursor: not-allowed; box-shadow: none; transform: none; }";
  html += ".btn-flash-on { background-color: #ffc107; color: #333; }";
  html += ".btn-flash-off { background-color: #f8f9fa; color: #333; border: 1px solid #ccc; }";
  html += ".btn-capture { background-color: #28a745; color: white; width: 100%; margin-top: 10px; }";
  html += "#message-area { margin-bottom: 20px; padding: 15px; border-radius: 10px; min-height: 50px; }";
  html += ".success { background-color: #e6ffe6; border: 1px solid #00cc00; color: #008000; }";
  html += ".progress { background-color: #fffbe6; border: 1px solid #ffd700; color: #ccaa00; }";
  html += ".error { background-color: #ffebe6; border: 1px solid #cc0000; color: #cc0000; }"; 
  html += "</style>";
  html += "<script>";
  // ฟังก์ชันใหม่: เพื่อควบคุมสถานะการเปิด/ปิดปุ่ม
  html += "function disableButtons(disable) {";
  html += "  document.querySelectorAll('.btn').forEach(btn => {";
  html += "    btn.disabled = disable;";
  html += "  });";
  html += "}";

  // ใช้การอัปเดต Element แทน alert()
  html += "function sendAction(url) {";
  html += "  disableButtons(true);"; // ปิดใช้งานปุ่มทั้งหมดทันทีที่กด
  html += "  const msgArea = document.getElementById('message-area');";
  html += " msgArea.className = 'progress';";
  html += " msgArea.innerHTML = '<p>กำลังส่งคำสั่งไป ESP32...</p>';";
  
  html += "  fetch(url).then(response => response.text()).then(data => {";
  // แสดงข้อความตอบกลับจาก server
  html += "    msgArea.className = 'progress';";
  // ไม่ได้ใช้ 'data' โดยตรง แต่รอให้ reload เพื่อดึง statusMessage ล่าสุด
  html += "    msgArea.innerHTML = '<p>ได้รับคำสั่งแล้ว: โปรดรอการอัปเดตสถานะถัดไป...</p>';"; 
  
  // หากเป็นการสั่ง Capture หรือ Flash ให้รีโหลดหน้าจอเพื่อดึงสถานะล่าสุด (STATE_IDLE/flashState)
  html += "    if (url.includes('/capture') || url.includes('/flash')) {";
  html += "      setTimeout(() => { window.location.reload(); }, 1500);"; 
  html += "    }";
  html += "  }).catch(error => {";
  html += "    msgArea.className = 'error';";
  html += "    msgArea.innerHTML = '<p>เกิดข้อผิดพลาดในการเชื่อมต่อ: ไม่พบ ESP32 หรือ Wi-Fi หลุด</p>';";
  html += "    console.error('Fetch error:', error);";
  html += "    disableButtons(false);"; // เปิดใช้งานปุ่มอีกครั้งหากเกิดข้อผิดพลาด
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<h1>Smart Box Camera Control</h1>";

  html += "<div class='status-box'>";
  // ข้อความภาษาไทยในโค้ดต้องบันทึกเป็น UTF-8
  html += "<p><strong>สถานะ Wi-Fi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "เชื่อมต่อ ✅" : "ตัดการเชื่อมต่อ ❌") + "</p>";
  // IP Address ถูกนำออกไปแล้วตามคำขอ
  html += "</div>";

  // ส่วนแสดงข้อความสถานะการดำเนินการและผลลัพธ์
  html += "<div id='message-area' class='" + String(captureState != STATE_IDLE ? "progress" : (statusMessage.indexOf("✅") != -1 ? "success" : (statusMessage.indexOf("❌") != -1 ? "error" : ""))) + "'>";
  if (captureState != STATE_IDLE) {
      // แสดงสถานะระหว่างดำเนินงาน
      html += "<p style='font-weight:bold;'>ขั้นตอนการถ่ายภาพกำลังดำเนินอยู่ (State: " + String(captureState) + ")<br>" + statusMessage + "</p>";
  } else if (statusMessage.length() > 0) {
      // แสดงข้อความสำเร็จที่ร้องขอ
      html += "<p>" + statusMessage + "</p>";
  } else {
      html += "<p>พร้อมสำหรับการดำเนินการ</p>";
  }
  html += "</div>";
  
  // ข้อความใหม่ตามที่ร้องขอ
  html += "<p>จะทำการถ่ายเมื่อมีเจ้าของพัสดุ **ขออนุญาตถ่ายภาพนะคะ**</p>";
  html += "<p>คุณสามารถเปิดและปิดแฟลชได้ 💡</p>";
  
  html += "<p>สถานะแฟลชปัจจุบัน: <span class='flash-state'>" + String(flashState ? "เปิด" : "ปิด") + "</span></p>";
  
  html += "<div class='btn-group'>";
  html += "<button class='btn btn-flash-on' onclick=\"sendAction('/flash?mode=on')\">เปิด</button>";
  html += "<button class='btn btn-flash-off' onclick=\"sendAction('/flash?mode=off')\">ปิด</button>";
  html += "</div>";

  html += "<p>กดปุ่มนี้เพื่อเริ่มขั้นตอนอัตโนมัติ 📸</p>";
  html += "<button class='btn btn-capture' onclick=\"sendAction('/capture')\">ถ่ายภาพ / Capture</button>";
  
  html += "</div>";
  html += "</body>";
  html += "</html>";

  server.send(200, "text/html", html);
}

// ================= SETUP =================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200); 
  delay(50); 
  pinMode(FLASH_LED_PIN, OUTPUT); 
  digitalWrite(FLASH_LED_PIN, flashState); // ใช้สถานะ flashState เริ่มต้น

  Serial.println(); 
  Serial.println("Starting..."); 
  WiFi.mode(WIFI_STA); 
  // การใช้ String literal ภาษาไทยในโค้ดต้องบันทึกไฟล์เป็น UTF-8
  Serial.println("กำลังเชื่อมต่อ Wi-Fi..."); 
  WiFi.begin(ssid, password); 
  
  unsigned long start = millis(); 
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { 
    Serial.print("."); 
    delay(500); 
  }
  Serial.println(); 

  if (WiFi.status() == WL_CONNECTED) { 
    Serial.print("WiFi connected. IP: "); 
    Serial.println(WiFi.localIP()); 
  } else {
    Serial.println("WiFi connect failed or timeout. Continuing (camera init will still try)."); 
  } 

  // กำหนด Root Certificate สำหรับ Telegram HTTPS
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); 

  configInitCameraWithRetry();

  // start web server route
  server.on("/flash", HTTP_GET, handleFlashToggle); 
  server.on("/capture", handleCapture);
  server.on("/", handleRoot); 
  server.begin();
  Serial.println("HTTP server started");
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  // State Machine for Timed Capture
  if (captureState == STATE_INITIATE_CAPTURE) { // State 1: เริ่ม Timed Flash (ถ้า flashState เป็น HIGH ใน handleCapture)
    // 1. เริ่ม: เปิดแฟลช และกำหนดเวลาเริ่มต้น
    Serial.println("State 1: Initiating flash ON.");
    digitalWrite(FLASH_LED_PIN, HIGH);
    flashState = HIGH; // ยืนยันสถานะแฟลช (ถึงแม้จะ HIGH อยู่แล้วก็ตาม)
    flashOnStartTime = millis();
    
    // *** NEW: Update status message after turning flash ON ***
    statusMessage = "💡 กำลังเปิดไฟแฟลช... ภาพจะถูกถ่ายในอีก " + String(flashDelayMs / 1000.0, 1) + " วินาที";
    
    captureState = STATE_FLASH_ON_DELAY; // Next is State 2
  } else if (captureState == STATE_FLASH_ON_DELAY) { // State 2: Delay
    // 2. รอ: รอให้ครบตามเวลาที่กำหนด
    if (millis() - flashOnStartTime >= flashDelayMs) {
      Serial.println("State 2: Delay complete, moving to capture.");
      captureState = STATE_CAPTURE; // Next is State 3 (Capture/Send)
    }
  } 
  
  if (captureState == STATE_CAPTURE) { // State 3: Common Capture/Send point
    // 3. ถ่าย: ถ่ายภาพ ส่งภาพ ปิดแฟลช (ถ้าจำเป็น) และกลับสู่ IDLE
    Serial.println("State 3: Capturing and sending photo.");
    
    // *** NEW: Update status message before sending ***
    statusMessage = "📸 ถ่ายภาพเสร็จแล้ว... กำลังส่งภาพไปที่ผู้รับพัสดุ กรุณารอสักครู่ค่ะ";
    
    String resp = sendPhotoTelegram();
    
    // *** Conditional Flash OFF based on shouldTurnFlashOffAfterCapture flag ***
    if (shouldTurnFlashOffAfterCapture) {
      Serial.println("Flash OFF (Timed Capture Sequence End).");
      digitalWrite(FLASH_LED_PIN, LOW);
      flashState = LOW; // Update global state
      shouldTurnFlashOffAfterCapture = false; // Reset flag
    } else {
      Serial.println("Flash state preserved (Immediate Capture).");
    }
    // *** End Conditional Flash OFF ***

    // กำหนดข้อความสถานะ (อัปเดตข้อความขอบคุณ)
    if (resp.indexOf("ok") != -1) {
       statusMessage = "✅ ส่งภาพสำเร็จ! ภาพถ่ายนี้ส่งถึงเจ้าของพัสดุเรียบร้อยค่ะ<br><br>ขอบคุณที่มาส่งพัสดุนะคะ ขอให้เดินทางปลอดภัยค่ะ";
    } else {
       statusMessage = "❌ เกิดข้อผิดพลาดในการส่งภาพไป Telegram! กรุณาลองใหม่.";
    }
    
    Serial.println("sendPhotoTelegram done.");
    
    // เปลี่ยนสถานะกลับเป็น IDLE ทันที
    captureState = STATE_IDLE; 
  }

  // Telegram Bot Handler (เดิม)
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) Serial.printf("Got %d new messages\n", numNewMessages);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}
