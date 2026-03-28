#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// --- Pin Definitions ---
const int TRIG_PIN = 23;
const int ECHO_PIN = 19;
const int BUTTON_PIN = 5;
const int LED_PIN = 2;

// --- Thresholds & Timers ---
const int DISTANCE_THRESHOLD_CM = 5; 
const float MOVEMENT_THRESHOLD_X = 0.5;  
unsigned long lastSendTime = 0;
const int SEND_COOLDOWN_MS = 2000;     

// --- MPU6050 Object ---
Adafruit_MPU6050 mpu;
float filteredAccelX = 0.0;
float filteredAccelY = 0.0;
const float EMA_ALPHA = 0.2; 

// --- PROTOCOL CONSTANTS ---
#define MSG_STEP_1 1
#define MSG_STEP_2 2
#define MSG_STEP_3 3

// The constant payload known by both Bus and Car
const uint32_t SECRET_MESSAGE = 0x5AFE7199; 
uint32_t current_Kb = 0; // Bus's unique secret token (regenerated per session)

// --- ESP-NOW Sender Structure ---
typedef struct struct_message {
    uint8_t msg_type;
    uint32_t payload;
} struct_message;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- Helper: Add Peer Dynamically ---
void addPeer(const uint8_t* mac) {
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, mac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
}

// --- ESP-NOW Callbacks ---
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // Hidden to keep terminal clean
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    struct_message incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));

    // Step 3: Bus receives (M ^ Kb ^ Kc) from Car.
    // Bus strips its own key (Kb) and sends back (M ^ Kc).
    if (incoming.msg_type == MSG_STEP_2) {
        uint32_t step3_payload = incoming.payload ^ current_Kb; 
        
        addPeer(mac); 

        struct_message step3Msg;
        step3Msg.msg_type = MSG_STEP_3;
        step3Msg.payload = step3_payload;
        
        esp_now_send(mac, (uint8_t *) &step3Msg, sizeof(step3Msg));
        Serial.println("Bus: Verification Step 3 sent to Car!");
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);

    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip!");
        delay(10);  
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv); 

    addPeer(broadcastAddress); 
    Serial.println("Bus Node Ready");
}

float getDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
    if (duration == 0) return 999.0; 
    return (duration * 0.0343) / 2.0; 
}

bool isBusStationary() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    filteredAccelX = (EMA_ALPHA * a.acceleration.x) + ((1.0 - EMA_ALPHA) * filteredAccelX);
    if (abs(filteredAccelX) < MOVEMENT_THRESHOLD_X) {
        return true;
    }
    return false;
}

void loop() {
    bool isButtonPressed = (digitalRead(BUTTON_PIN) == LOW);
    digitalWrite(LED_PIN, isButtonPressed ? HIGH : LOW);

    if (isButtonPressed && isBusStationary()) {
        float distance = getDistance();
        
        if (distance < DISTANCE_THRESHOLD_CM) {
            if (millis() - lastSendTime > SEND_COOLDOWN_MS) {
                Serial.println("ALERT TRIGGERED! Initiating Protocol...");

                // Generate Bus's Secret Token (Kb) for this interaction
                current_Kb = esp_random(); 
                
                // Step 1: Send (SECRET_MESSAGE ^ Kb)
                struct_message step1Msg;
                step1Msg.msg_type = MSG_STEP_1;
                step1Msg.payload = SECRET_MESSAGE ^ current_Kb;
                esp_now_send(broadcastAddress, (uint8_t *) &step1Msg, sizeof(step1Msg));

                lastSendTime = millis();
            }
        }
    } else {
        filteredAccelX = 0.0;
    }
    delay(50); 
}