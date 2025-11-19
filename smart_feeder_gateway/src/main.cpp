#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <lwip/dns.h>
#include <lwip/ip_addr.h>

// ===================== OBJECT ======================
WiFiClient espClient;
WiFiClientSecure client;
PubSubClient mqtt(espClient);
HTTPClient http;
Servo feederServo;
unsigned long lastLineNotify = 0;
bool fed = false;

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
void sendLineNotify(String msg);

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
    }
    else if (String(topic) == "@msg/alias/weight") {
        weightVal = payloadStr.toFloat();
    }
    else if (String(topic) == "@msg/alias/motion") {
        motionFlag = payloadStr.toInt();
        if (motionFlag == 1)
            sendLineNotify("พบการเคลื่อนไหวของหนูแฮมสเตอร์!");
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

    String json;
    serializeJson(doc, json);

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();

    Serial.println("Firebase updated");
}

// ===================== LINE NOTIFY ======================
void sendLineNotify(String msg) {
    if (WiFi.status() != WL_CONNECTED) return;

    client.setInsecure();  // ต้องมี
    http.begin(client, "https://notify-api.line.me/api/notify");

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Bearer " + String(LINE_TOKEN));

    int httpCode = http.POST("message=" + msg);
    Serial.print("LINE HTTP code = ");
    Serial.println(httpCode);

    http.end();
}

// ===================== WIFI ======================
void setupWiFi() {

    // ตั้ง DNS ให้ ESP32 (แก้ปัญหา DNS Failed)
    ip_addr_t dns1, dns2;
    ipaddr_aton("8.8.8.8", &dns1);   // Google DNS
    ipaddr_aton("1.1.1.1", &dns2);   // Cloudflare DNS
    dns_setserver(0, &dns1);
    dns_setserver(1, &dns2);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi Connecting");

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 40) {
        delay(300);
        Serial.print(".");
        retry++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Connected IP: ");
        Serial.println(WiFi.localIP());
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

// ===================== ACTUATOR ======================
void controlFeeder() {
    if (weightVal < 50 && !fed) {  
        feederServo.write(90);
        sendLineNotify("เติมอาหารให้หนูแฮมสเตอร์แล้ว!");
        fed = true;
    } else if (weightVal >= 50) {
        feederServo.write(0);
        fed = false;
    }
}

// ===================== SETUP ======================
void setup() {
    Serial.begin(115200);
    
    setupWiFi();
    client.setInsecure(); 

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

    // ถ้าอากาศแย่ แจ้งเตือน LINE ทุก 1 นาที
    if (airQuality > 3500 && now - lastLineNotify > 60000) {
        sendLineNotify("ค่าอากาศแฮมสเตอร์สูงผิดปกติ!");
        lastLineNotify = now;
    }

    // บ้านมืดเกินไป
    if (lightValue < 500 && now - lastLineNotify > 60000) {
        sendLineNotify("ความสว่างบ้านแฮมสเตอร์ต่ำเกินไป!");
        lastLineNotify = now;
    }

    controlFeeder();
    sendToFirebase();

    delay(1000);
}
