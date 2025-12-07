#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <lwip/dns.h>
#include <lwip/ip_addr.h>

// ===================== THRESHOLD ======================
#define FOOD_EMPTY      15 //ค่าน้ำหนักอาหารต่ำสุดที่ถือว่า อาหารหมด หรือ ใกล้หมด
#define FOOD_MAX 20
#define HAMSTER_NEAR 8//หนูเข้าใกล้ชามอาหารน้อยกว่า 8 cm
#define AIR_WARNING     3200 //ค่าที่มี กลิ่น/อากาศไม่ดี
#define AIR_BAD         3500 //ค่าที่มี อากาศแย่มาก/อันตราย
#define LIGHT_TOO_MUCH  500 //ค่าที่ถือว่า สว่างเกินไป
#define STILL_TIMEOUT   300000

// ===================== OBJECT ======================
WiFiClient client;
PubSubClient mqtt(client);
Servo feederServo;

unsigned long lastFirebaseSend = 0;

unsigned long lastAirNotify = 0;
unsigned long lastLightNotify = 0;
bool fed = false;

unsigned long lastMotionTime = 0;
bool stillAlertSent = false;

// ======= ADD THIS PROTOTYPE =======
void sendDiscord(String message);

// ===================== PIN ======================
#define MQ135_PIN 34
#define LDR_PIN   35
#define SERVO_PIN 14 //

// ค่าเซนเซอร์ Gateway
int airQuality = 0;
int lightValue = 0;

// ค่าที่ Sensor Node ส่งมา
float ultrasonic_d = 0;
float weightVal = 0;
int motionFlag = 0;

// ===================== MQTT CALLBACK ======================
void callback(char* topic, byte* payload, unsigned int length) {
    String payloadStr = "";
    for (int i = 0; i < length; i++)
        payloadStr += (char)payload[i];

    Serial.print("MQTT >>> ");
    Serial.print(topic);
    Serial.print(" = ");
    Serial.println(payloadStr);

    if (String(topic) == "@msg/sensor_node/ultrasonic"){
        ultrasonic_d = payloadStr.toFloat();
    } else if (String(topic) == "@msg/sensor_node/weight") {
        weightVal = payloadStr.toFloat();
    } else if (String(topic) == "@msg/alias/motion") {
        motionFlag = payloadStr.toInt();
         // ถ้ามีการเคลื่อนไหว
        if (motionFlag == 1) {
            lastMotionTime = millis();   // รีเซ็ตเวลา
            stillAlertSent = false;      // เคยแจ้งเตือนนิ่งก่อนหน้าไหม
            sendDiscord("🐹 พบการเคลื่อนไหวของหนูแฮมสเตอร์!");
        }
    }
}

// ===================== SEND TO FIREBASE ======================
void sendToFirebase() {
    if (WiFi.status() != WL_CONNECTED) return;

    String url = String(FIREBASE_URL) + "/hamster_log.json";

    StaticJsonDocument<256> doc;
    doc["ultrasonic"] = ultrasonic_d;
    doc["weight"] = weightVal;
    doc["air"] = airQuality;
    doc["light"] = lightValue;
    doc["motion"] = motionFlag;
    doc["timestamp"] = millis();

    String jsonStr;
    serializeJson(doc, jsonStr);

    HTTPClient http;
    if (http.begin(client, url)) {
        http.addHeader("Content-Type", "application/json");
        http.POST(jsonStr);
        http.end();
        Serial.println("Firebase updated");
    } else {
        Serial.println("Firebase connect failed!");
    }
}

// ===================== WIFI ======================
void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("WiFi Connecting to hotspot");
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 60) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Connected IP: ");
        Serial.println(WiFi.localIP());

        // ====== ทดสอบ DNS ======
        IPAddress ip;
        Serial.print("Ping Google.com ... ");
        if (WiFi.hostByName("google.com", ip)) {
            Serial.print("OK, IP = ");
            Serial.println(ip);
        } else {
            Serial.println("Failed!");
        }

    } else {
        Serial.println("WiFi Connection Failed!");
    }
}

// ===================== MQTT CONNECT ======================
void reconnectMQTT() {
    while (!mqtt.connected()) {
        Serial.println("Connecting NETPIE...");
        if (mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET)) {
            Serial.println("NETPIE Connected");
            mqtt.subscribe("@msg/sensor_node/ultrasonic");  // ★ แก้
            mqtt.subscribe("@msg/sensor_node/weight");      // ★ แก้
            mqtt.subscribe("@msg/alias/motion");
        } else {
            Serial.println("Retry NETPIE…");
            delay(2000);
        }
    }
}
// ===================== Discord ======================
void sendDiscord(String message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot send Discord.");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();   

    HTTPClient http;
    http.begin(client, DISCORD_WEBHOOK);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"username\":\"" DISCORD_USERNAME "\",\"content\":\"" + message + "\"}";

    int httpResponseCode = http.POST(payload);
    Serial.print("Discord response: ");
    Serial.println(httpResponseCode);

    http.end();
}

void controlFeeder() {
    // ======= เงื่อนไขเริ่มเติมอาหาร =======
    // หากน้ำหนักต่ำกว่า 20 g → ต้องเติม
    if (weightVal < FOOD_MAX) {

        // เช็คว่าหนูเข้าใกล้ชามหรือยัง
        if (ultrasonic_d < HAMSTER_NEAR) {

            // ★ แก้ใหม่: หมุน servo แบบค้าง (ไม่ใช้ map/need)
            feederServo.write(60);    // หมุนตามอัตราอาหารไหล
            Serial.println("Feeding... servo rotating");

            // ส่ง Discord เฉพาะครั้งแรกที่เริ่มหมุน
            if (!fed) {
                sendDiscord("🍽 เริ่มเติมอาหารให้หนูแฮมสเตอร์...");
                fed = true;  // ล็อกว่าเริ่มรอบนี้แล้ว
            }
        }

    } 
    // ======= หยุดเติมเมื่อถึง ≥ 20 g =======
    else {
        feederServo.write(0);     // ★ แก้ใหม่: หยุดทันทีเมื่อได้ 20g
        Serial.println("Feeding completed. Servo stopped.");

        if (fed) {
            sendDiscord("✅ อาหารครบ 20g แล้ว หยุดเติมเรียบร้อย!");
            fed = false;  // รีเซ็ตรอบใหม่
        }
    }
}


// ===================== SETUP ======================
void setup() {
    Serial.begin(115200);

    setupWiFi();
    

    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(callback);

    feederServo.attach(SERVO_PIN);
    feederServo.write(0);

    lastMotionTime = millis();

    if (!mqtt.connected()) reconnectMQTT();
}

// ===================== LOOP ======================
void loop() {
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();

    airQuality = analogRead(MQ135_PIN);
    lightValue = analogRead(LDR_PIN);

       // ======== PUBLISH GATEWAY SENSOR TO NETPIE (SEPARATE TOPICS) ========
    mqtt.publish("@msg/gateway/air", String(airQuality).c_str());
    mqtt.publish("@msg/gateway/light", String(lightValue).c_str());
    mqtt.publish("@msg/gateway/fed", String(fed).c_str());
    // ===================================================================

    unsigned long now = millis();

    // ======= แจ้งเตือนคุณภาพอากาศ =======
    if (airQuality > AIR_WARNING && airQuality <= AIR_BAD && now - lastAirNotify > 60000) {
        sendDiscord("⚠️ คุณภาพอากาศในกรงเริ่มมีกลิ่น (" + String(airQuality) + ")");
        lastAirNotify = now;
    }

    if (airQuality > AIR_BAD && now - lastAirNotify > 60000) {
        sendDiscord("🚨 อากาศแย่มาก! ควรทำความสะอาดกรงด่วน (" + String(airQuality) + ")");
        lastAirNotify = now;
    }

    // ======= แจ้งเตือนแสง =========
    if (lightValue > LIGHT_TOO_MUCH && now - lastLightNotify > 60000) {
        sendDiscord("💡 บ้านแฮมสเตอร์สว่างเกินไป (" + String(lightValue) + ")");
        lastLightNotify = now;
    }
    // ======= แจ้งเตือนว่าหนูอยู่นิ่งนานเกินไป=====================================
    if (motionFlag == 0) {
        if (!stillAlertSent && (now - lastMotionTime > STILL_TIMEOUT)) {
            sendDiscord("⚠️ หนูแฮมสเตอร์นิ่งนานเกินไปแล้ว อาจกำลังพัก ตรวจสอบด้วยนะ!");
            stillAlertSent = true;
        }
    }
    controlFeeder();
    // ส่ง Firebase ทุก 10 วินาที
if (millis() - lastFirebaseSend > 10000) {
    sendToFirebase();
    lastFirebaseSend = millis();
}
    delay(1000);
}
