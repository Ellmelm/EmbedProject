#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"

// ==========================================
// 1. ส่วนที่เพื่อนต้องแก้ (ใส่ WiFi ที่จะใช้)
// ==========================================
// **สำคัญ:** ต้องเป็น WiFi ตัวเดียวกับที่เครื่อง Server (คอมเพื่อน) เกาะอยู่
const char* ssid = "Elm";
const char* password = "12345678";

// ==========================================
// 2. ตั้งค่า Server (ใส่ให้แล้ว ไม่ต้องแก้)
// ==========================================
// IP Address ของเครื่องคอมพิวเตอร์ Server
String serverName = "http://172.20.10.12:5001/upload";

// ==========================================
// 3. ตั้งค่าขากล้อง (สำหรับบอร์ด AI Thinker)
// ==========================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);
  
  // ตั้งค่า WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // ตั้งค่ากล้อง
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
  
  // ถ้าภาพเบลอหรือใหญ่ไป ให้ลองลดขนาดตรงนี้ (VGA = 640x480)
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; 
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if(esp_camera_init(&config) != ESP_OK){
    Serial.println("Camera init failed");
    return;
  }
  Serial.println("Camera Ready!");
}

void loop() {
  sendPhoto();
  
  // รอ 10 นาที (600,000 มิลลิวินาที)
  // **คำแนะนำ:** ตอนทดสอบ ให้แก้เลขนี้เป็น 10000 (10 วินาที) ก่อน จะได้ไม่ต้องรอนาน
  Serial.println("Waiting 10 minutes...");
  delay(10000); 
}

void sendPhoto() {
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected! Reconnecting...");
    WiFi.reconnect();
    return;
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  HTTPClient http;
  Serial.print("Sending photo to: ");
  Serial.println(serverName);

  http.begin(serverName);
  
  // สร้าง Header ของการส่งไฟล์
  String boundary = "MyBoundary";
  String contentType = "multipart/form-data; boundary=" + boundary;
  http.addHeader("Content-Type", contentType);

  String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  uint32_t imageLen = fb->len;
  uint32_t extraLen = head.length() + tail.length();
  uint32_t totalLen = imageLen + extraLen;
  
  // จองเมมโมรี่สำหรับแพ็คข้อมูลส่ง
  uint8_t *payload = (uint8_t *)malloc(totalLen);
  
  if(payload == NULL) {
     Serial.println("Not enough memory to upload!");
  } else {
     // ประกอบร่างข้อมูล (หัว + รูป + หาง)
     memcpy(payload, head.c_str(), head.length());
     memcpy(payload + head.length(), fb->buf, fb->len);
     memcpy(payload + head.length() + fb->len, tail.c_str(), tail.length());

     // ส่ง HTTP POST
     int httpResponseCode = http.POST(payload, totalLen);
     
     if (httpResponseCode > 0) {
       String response = http.getString();
       Serial.println("Server Response: " + response);
     } else {
       Serial.printf("Error occurred: %s\n", http.errorToString(httpResponseCode).c_str());
     }
     free(payload); // คืนเมมโมรี่
  }

  http.end();
  esp_camera_fb_return(fb);
}