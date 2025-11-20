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
#define LIGHT_TOO_DARK  400 //ค่าที่ถือว่า มืด

// ===================== OBJECT ======================
WiFiClient espClient;
WiFiClientSecure secureClient;
PubSubClient mqtt(espClient);
Servo feederServo;

unsigned long lastAirNotify = 0;
unsigned long lastLightNotify = 0;
bool fed = false;

// ======= ADD THIS PROTOTYPE =======
void sendDiscord(String message);

// ===================== PIN ======================
#define MQ135_PIN 34
#define LDR_PIN   35
#define SERVO_PIN 13

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

    if (String(topic) == "@msg/alias/ultrasonic") {
        ultrasonic_d = payloadStr.toFloat();
    } else if (String(topic) == "@msg/alias/weight") {
        weightVal = payloadStr.toFloat();
    } else if (String(topic) == "@msg/alias/motion") {
        motionFlag = payloadStr.toInt();
        if (motionFlag == 1) {
        sendDiscord("พบการเคลื่อนไหวของหนูแฮมสเตอร์!");
    }}
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
    if (http.begin(secureClient, url)) {
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
            mqtt.subscribe("@msg/alias/ultrasonic");
            mqtt.subscribe("@msg/alias/weight");
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

// ===================== ACTUATOR ======================
void controlFeeder() {
    // 1) เช็คว่าอาหารน้อยกว่า 15 และยังไม่ได้เติมรอบนี้
    if (weightVal < FOOD_EMPTY && !fed) {

        // 2) เช็คว่าแฮมสเตอร์เข้าใกล้หรือยัง (ultrasonic)
        if (ultrasonic_d < HAMSTER_NEAR) {

            // ปริมาณที่ต้องเติมเพื่อให้ถึง 20g
            float need = FOOD_MAX - weightVal;
            if (need < 0) need = 0;

            // แปลง "กรัม" -> "องศาที่ servo หมุน" (คุณปรับได้เอง)
            int servoPulse = map(need, 0, FOOD_MAX, 0, 90);

            feederServo.write(servoPulse);
            delay(700);
            feederServo.write(0);

            sendDiscord("🍽 เติมอาหารให้หนูแฮมสเตอร์แล้ว (" + String(need) + " g)");

            fed = true; // ล็อกไว้รอ reset
        }
    }

    // 3) Reset เมื่อมีอาหารถึงระดับปลอดภัย
    if (weightVal >= FOOD_MAX) {
        fed = false;
    }
}


// ===================== SETUP ======================
void setup() {
    Serial.begin(115200);

    setupWiFi();
    secureClient.setInsecure();

    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(callback);

    feederServo.attach(SERVO_PIN);
    feederServo.write(0);

    if (!mqtt.connected()) reconnectMQTT();
}

// ===================== LOOP ======================
void loop() {
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();

    airQuality = analogRead(MQ135_PIN);
    lightValue = analogRead(LDR_PIN);

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
    if (lightValue < LIGHT_TOO_DARK && now - lastLightNotify > 60000) {
        sendDiscord("💡 บ้านแฮมสเตอร์มืดเกินไป (" + String(lightValue) + ")");
        lastLightNotify = now;
    }

    controlFeeder();
    sendToFirebase();

    delay(1000);
}

