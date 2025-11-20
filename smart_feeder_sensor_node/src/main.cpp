#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "esp_camera.h"
#include "HX711.h"

// HX711 pins //weight
#define LOADCELL_DOUT  33
#define LOADCELL_SCK  32
HX711 scale;

// HC-SR04 pins //ultrasonic
#define TRIG_PIN  14
#define ECHO_PIN  12

// ESP32-CAM (AI THINKER) Pin Map
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

WiFiClient espClient;
PubSubClient mqtt(espClient);

//=====================================================
// Frame buffer for AI
//=====================================================
static uint8_t prevFrame[160 * 120];
static uint8_t currFrame[160 * 120];
bool hasPrev = false;

//==================== GRAYSCALE FRAME ====================
bool getGrayscaleFrame(uint8_t *buffer) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;

  if (fb->format != PIXFORMAT_GRAYSCALE) {
    esp_camera_fb_return(fb);
    return false;
  }

  memcpy(buffer, fb->buf, 160 * 120);
  esp_camera_fb_return(fb);
  return true;
}

//-------------------------------------
// SENSOR FUNCTIONS
//-------------------------------------
void setupSensors() {
    // Loadcell
    scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
    scale.set_scale(); 
    scale.tare();

    // Ultrasonic
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
}

    float readUltrasonic() {
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);

        long duration = pulseIn(ECHO_PIN, HIGH, 25000);
        float distance_cm = duration * 0.034 / 2;
        return distance_cm;
    }

    float readWeight() {
        return scale.get_units(10); // เฉลี่ย 10 ครั้ง
    }

// ===================== Setup WiFi =====================
void setupWiFi() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int count = 0;
    while (WiFi.status() != WL_CONNECTED && count < 50) {
        delay(300);
        Serial.print(".");
        count++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi Connected! IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("Failed to connect WiFi");
    }
}

// ===================== MQTT Connect =====================
void reconnectMQTT() {
    while (!mqtt.connected()) {
        Serial.println("Connecting to NETPIE...");
        if (mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET)) {
            Serial.println("NETPIE Connected!");
        } else {
            Serial.println("Failed to connect NETPIE, retrying in 2 seconds...");
            delay(2000);
        }
    }
}


// ===================== Setup =====================
void setup() {
    Serial.begin(115200);
    delay(1000); 

     // ==========ตั้งค่ากล้อง ==========
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
    config.pixel_format = PIXFORMAT_GRAYSCALE;  
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera init failed!");
        //เปิดesp32cam
        return;
    }
    Serial.println("Camera ready!");

    setupWiFi();
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);

    // เช็ค MQTT เบื้องต้น
    if (!mqtt.connected()) {
        reconnectMQTT();
    }
}

// ===================== Loop =====================
void loop() {
    // ตรวจสอบการเชื่อมต่อ MQTT ทุก loop
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();

    //---------------------------------
    // 1) Motion Detection (AI)
    //---------------------------------
    if (!getGrayscaleFrame(currFrame)) {
        Serial.println("Frame error!");
        return;
    }

    // ครั้งแรกยังไม่มีภาพก่อนหน้า
    if (!hasPrev) {
        memcpy(prevFrame, currFrame, 160*120);
        hasPrev = true;
        return;
    }

    //---------------------------------
    // 1) Motion Detection (AI)
    //---------------------------------
    long diffCount = 0;
    for (int i = 0; i < 160*120; i++) {
        int diff = abs(currFrame[i] - prevFrame[i]);
        if (diff > 25) diffCount++;   // pixel เปลี่ยนแปลง
    }

    int motion = (diffCount > 300) ? 1 : 0;  
    memcpy(prevFrame, currFrame, 160*120);

    //---------------------------------
    // 2) Other Sensors
    //---------------------------------
    float distance = readUltrasonic();
    float weight = readWeight();

    //---------------------------------
    // 3) Publish
    //---------------------------------
    String payload = 
        "{\"ultrasonic\":" + String(distance) +
        ",\"weight\":" + String(weight) +
        ",\"motion\":" + String(motion) + "}";

    mqtt.publish("@msg/alias", payload.c_str());
    mqtt.publish("@msg/alias/ultrasonic", String(distance).c_str());
    mqtt.publish("@msg/alias/weight", String(weight).c_str());
    mqtt.publish("@msg/alias/motion", String(motion).c_str());

    Serial.println(payload);

    delay(1500);
}
