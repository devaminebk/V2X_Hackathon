#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_wifi.h>   // for esp_wifi_set_max_tx_power()

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
const float EMA_ALPHA = 0.2;

// --- PROTOCOL CONSTANTS ---
#define MSG_STEP_1 1
#define MSG_STEP_2 2
#define MSG_STEP_3 3

const uint32_t SECRET_MESSAGE = 0x5AFE7199;
uint32_t current_Kb = 0;

// --- ESP-NOW Sender Structure ---
typedef struct struct_message {
    uint8_t  msg_type;
    uint32_t payload;
} struct_message;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ═══════════════════════════════════════════════════════════════════
//  AI / CLOUD INTEGRATION
// ═══════════════════════════════════════════════════════════════════

// 10 fictional locations embedded in firmware (mirrors training data)
struct LocationEntry {
    const char* id;
    float x;
    float y;
};

const LocationEntry LOCATIONS[10] = {
    {"Loc_0",  0.250f,  7.694f},
    {"Loc_1", 96.519f, 35.471f},
    {"Loc_2", 15.601f, 15.602f},
    {"Loc_3", 35.227f, 96.469f},
    {"Loc_4", 55.950f, 45.227f},
    {"Loc_5", 84.712f, 55.845f},
    {"Loc_6", 60.795f, 25.087f},
    {"Loc_7", 22.364f, 70.131f},
    {"Loc_8", 46.986f, 82.824f},
    {"Loc_9", 71.041f, 14.872f},
};

// TX power levels mapped to risk score (unit: 0.25 dBm steps, ESP-IDF range 8–84)
//   Risk 25 →  8  (  2 dBm)  – quiet neighbourhood
//   Risk 60 → 52  ( 13 dBm)  – moderate road
//   Risk 90 → 84  ( 21 dBm)  – maximum, high-risk zone
int riskToTxPower(int risk) {
    if (risk >= 90) return 84;
    if (risk >= 60) return 52;
    return 8;
}

// Cloud query state machine
enum CloudState { CLOUD_IDLE, CLOUD_WAITING };
CloudState cloudState = CLOUD_IDLE;

int         currentRiskScore = 60;   // default until cloud replies
int         pendingTxPower   = 52;
const char* pendingLocationId = nullptr;
unsigned long cloudQueryTime  = 0;
const unsigned long CLOUD_TIMEOUT_MS = 4000;

// Pick a random location and query the PC cloud server over Serial
void queryCloud() {
    int idx = random(0, 10);
    pendingLocationId = LOCATIONS[idx].id;

    // "QUERY:<id>" is the cellular-over-USB protocol line
    Serial.print("QUERY:");
    Serial.println(pendingLocationId);

    cloudState    = CLOUD_WAITING;
    cloudQueryTime = millis();
}

// Call every loop() tick; returns true once a valid RISK: reply is parsed
bool pollCloudReply() {
    if (cloudState != CLOUD_WAITING) return false;

    // Timeout guard – proceed with default score if cloud is silent
    if (millis() - cloudQueryTime > CLOUD_TIMEOUT_MS) {
        Serial.println("[AI] Cloud timeout – using default risk 60%");
        currentRiskScore = 60;
        pendingTxPower   = riskToTxPower(60);
        cloudState       = CLOUD_IDLE;
        return true;
    }

    // Non-blocking Serial read
    if (!Serial.available()) return false;

    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.startsWith("RISK:")) {
        int score = line.substring(5).toInt();
        currentRiskScore = score;
        pendingTxPower   = riskToTxPower(score);
        cloudState       = CLOUD_IDLE;

        Serial.printf("[AI] Location='%s'  Risk=%d%%  TX-power step=%d (%.1f dBm)\n",
                      pendingLocationId, score, pendingTxPower, pendingTxPower * 0.25f);
        return true;
    }
    return false;
}

// Apply TX power and fire Step-1 of the handshake protocol
void initiateProtocol() {
    esp_wifi_set_max_tx_power(pendingTxPower);

    current_Kb = esp_random();

    struct_message step1Msg;
    step1Msg.msg_type = MSG_STEP_1;
    step1Msg.payload  = SECRET_MESSAGE ^ current_Kb;
    esp_now_send(broadcastAddress, (uint8_t*)&step1Msg, sizeof(step1Msg));

    Serial.printf("ALERT TRIGGERED! Location='%s'  Risk=%d%%  "
                  "TX-power step=%d → broadcasting Step-1\n",
                  pendingLocationId, currentRiskScore, pendingTxPower);
}

// ═══════════════════════════════════════════════════════════════════

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

    // Step 3: Bus receives (M ^ Kb ^ Kc), strips Kb, sends back (M ^ Kc)
    if (incoming.msg_type == MSG_STEP_2) {
        uint32_t step3_payload = incoming.payload ^ current_Kb;

        addPeer(mac);

        struct_message step3Msg;
        step3Msg.msg_type = MSG_STEP_3;
        step3Msg.payload  = step3_payload;
        esp_now_send(mac, (uint8_t*)&step3Msg, sizeof(step3Msg));
        Serial.println("Bus: Verification Step 3 sent to Car!");
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(TRIG_PIN,   OUTPUT);
    pinMode(ECHO_PIN,   INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN,    OUTPUT);

    randomSeed(esp_random());

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
    return (abs(filteredAccelX) < MOVEMENT_THRESHOLD_X);
}

// ── State machine for the button-press → cloud query → broadcast flow ──
enum BusState { BUS_IDLE, BUS_QUERYING_CLOUD, BUS_READY_TO_SEND };
BusState busState = BUS_IDLE;

void loop() {
    bool isButtonPressed = (digitalRead(BUTTON_PIN) == LOW);
    digitalWrite(LED_PIN, isButtonPressed ? HIGH : LOW);

    // ── Step A: Conditions met – kick off cloud query ────────────────
    if (busState == BUS_IDLE &&
        isButtonPressed &&
        isBusStationary() &&
        (getDistance() < DISTANCE_THRESHOLD_CM) &&
        (millis() - lastSendTime > SEND_COOLDOWN_MS))
    {
        Serial.println("[AI] Conditions met – querying cloud for risk score …");
        queryCloud();
        busState = BUS_QUERYING_CLOUD;
    }

    // ── Step B: Wait for cloud reply ─────────────────────────────────
    if (busState == BUS_QUERYING_CLOUD) {
        if (pollCloudReply()) {
            busState = BUS_READY_TO_SEND;
        }
    }

    // ── Step C: Risk score received – run the ESP-NOW protocol ───────
    if (busState == BUS_READY_TO_SEND) {
        initiateProtocol();
        lastSendTime = millis();
        busState     = BUS_IDLE;
    }

    if (!isButtonPressed) {
        filteredAccelX = 0.0;
        if (busState == BUS_QUERYING_CLOUD) {   // button released mid-query
            cloudState = CLOUD_IDLE;
            busState   = BUS_IDLE;
        }
    }

    delay(50);
}