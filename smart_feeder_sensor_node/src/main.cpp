#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "esp_camera.h"
#include "HX711.h"

// HX711 pins //weight
#define LOADCELL_DOUT  32 //
#define LOADCELL_SCK  33 //
HX711 scale;

float CALIBRATION_FACTOR = 235.0;

// HC-SR04 pins //ultrasonic
#define TRIG_PIN  13 //
#define ECHO_PIN  12 //

WiFiClient espClient;
PubSubClient mqtt(espClient);

//-------------------------------------
// SENSOR FUNCTIONS
//-------------------------------------
void setupSensors() {
    // Loadcell
    scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
    delay(500);          // ★ ให้ HX711 stabilize
    scale.set_scale(CALIBRATION_FACTOR);
    scale.tare();
    delay(500);          // ★ ให้ผล tare คงที่

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
    if (distance_cm <= 0 || isnan(distance_cm)) distance_cm = 0;
    Serial.print("Ultrasonic (cm) = ");
    Serial.println(distance_cm);
    return distance_cm;
}

float lastWeight = 0;

float readWeight() {
    float w = scale.get_units(3);

    // Low-pass filter (กันกระเด้ง)
    lastWeight = (lastWeight * 0.7) + (w * 0.3);

    Serial.print("Filtered weight = ");
    Serial.println(lastWeight);

    return lastWeight;
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
        // DEBUG INFO
        Serial.print("ClientID: "); Serial.println(NETPIE_CLIENT_ID);
        Serial.print("Username: "); Serial.println(NETPIE_TOKEN); // <-- NETPIE token เป็น username
        Serial.print("Password: "); Serial.println(NETPIE_SECRET); // <-- NETPIE secret เป็น password
        Serial.print("Server: "); Serial.println(MQTT_SERVER);
        Serial.print("Port: "); Serial.println(MQTT_PORT);

        if (mqtt.connect(NETPIE_CLIENT_ID, NETPIE_TOKEN, NETPIE_SECRET)) {
            Serial.println("NETPIE Connected!");
            delay(200);
        } else {
            // แสดง error code เพื่อ debug
            Serial.print("Failed to connect NETPIE, state = ");
            Serial.println(mqtt.state());
            Serial.println("Failed to connect NETPIE, retrying in 2 seconds...");
            delay(2000);
        }
    }
}

// ===================== Setup =====================
void setup() {
    Serial.begin(115200);
    delay(1000); 

    setupWiFi();
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);

    // setup sensors
    setupSensors();  // <-- เพิ่มบรรทัดนี้

    // เช็ค MQTT เบื้องต้น
    if (!mqtt.connected()) {
        reconnectMQTT();
    }
}

// ================= SENSOR NODE =================
// ⭐ แก้เพิ่ม: ให้ publish เร็วขึ้นเพื่อให้ gateway ควบคุม servo ได้แม่นยำ

unsigned long lastPublish = 0;
// แก้: 1500 -> 600 ms
const unsigned long interval = 600;   // ★ แก้เพื่อให้ gateway อัปเดตน้ำหนักเร็วขึ้น

void loop() {
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();

    unsigned long now = millis();
    if (now - lastPublish >= interval) {
        lastPublish = now;

        float distance = readUltrasonic();
        float weight = readWeight();

        if (distance < 0) distance = 0;
        if (isnan(weight)) weight = 0;
        if (weight < 0) weight = 0;  

        // JSON payload
        char payload[64];
        snprintf(payload, sizeof(payload),
                 "{\"ultrasonic\":%.2f,\"weight\":%.2f}", distance, weight);

        mqtt.publish("@shadow/sensor_node", payload);

        mqtt.publish("@msg/sensor_node/ultrasonic", String(distance, 2).c_str());
        mqtt.publish("@msg/sensor_node/weight", String(weight, 2).c_str());

        Serial.print("Payload JSON: ");
        Serial.println(payload);
    }
}