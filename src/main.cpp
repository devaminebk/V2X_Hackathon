#include <esp_now.h>
#include <WiFi.h>

// 1. MUST MATCH THE RECEIVER'S STRUCTURE
typedef struct struct_message {
    int value;
} struct_message;

// Create a structured object to hold the data
struct_message myData;

uint32_t packetCounter = 0;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Packet ID: ");
    Serial.print(packetCounter);
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? " -> Delivered" : " -> Fail");
    packetCounter++;
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // Register peer
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo)); 
    
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;     
    peerInfo.encrypt = false; 

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
    
    Serial.println("ESP-NOW Sender Initialized");
}

void loop() {
    // 2. ASSIGN THE VALUE TO THE STRUCT
    myData.value = 7; // This replaces your 'datadata' variable

    // 3. SEND THE WHOLE STRUCTURE
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
   
    if (result != ESP_OK) {
        Serial.println("Send Error");
    }

    delay(1000); 
}