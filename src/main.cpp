#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// --- HARDWARE SETUP ---
Adafruit_PCD8544 display = Adafruit_PCD8544(18, 23, 2, 5, 4);

const int backlightPin = 15;
const int buzzerPin = 13;    
const int pwmChannel = 0;    
const int resolution = 8;    
const int ledpin = 12;       

// --- PROTOCOL CONSTANTS ---
#define MSG_STEP_1 1
#define MSG_STEP_2 2
#define MSG_STEP_3 3

// The constant payload known by both Bus and Car
const uint32_t SECRET_MESSAGE = 0x5AFE7199;
uint32_t current_Kc = 0; // Car's unique secret token (regenerated per interaction)

// --- ESP-NOW SETUP ---
typedef struct struct_message {
    uint8_t msg_type;
    uint32_t payload;
} struct_message;

// State tracking variables
volatile bool isVerifying = false;
volatile bool alertVerified = false; 
unsigned long verifyStartTime = 0;
uint8_t busMac[6];

// --- FUNCTIONS ---
void playTone(int freq, int ms) {
  ledcWriteTone(pwmChannel, freq);
  delay(ms);
  ledcWriteTone(pwmChannel, 0); 
}

void updateScreen(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(" STATUS:");
  display.println("------------");
  display.println(msg);
  display.display();
}

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

// Full hardware warning sequence for Legitimate Bus
void triggerLegitimateWarning() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("  WARNING!");
  display.println("------------");
  display.println(" SCHOOL BUS");
  display.println("   AHEAD!");
  display.display();

  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {  // 3-second robust warning
    digitalWrite(ledpin, LOW); 
    playTone(1200, 150); 
    delay(50);
    playTone(900, 150);  
    delay(50);
    digitalWrite(ledpin, HIGH);  
    delay(100);
  }

  digitalWrite(ledpin, HIGH);  
  updateScreen("MONITORING...");
}

// Silent visual warning for Spoofed/Failed Verification
void triggerSpoofWarning() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("   ALERT!");
  display.println("------------");
  display.println("   FALSE");
  display.println("  POSITIVE");
  display.display();

  delay(2500); // Show false positive silently for 2.5 seconds
  updateScreen("MONITORING...");
}

// --- ESP-NOW Callback ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  struct_message incoming;
  memcpy(&incoming, incomingData, sizeof(incoming));
  
  // Step 1: Bus initiates protocol. 
  if (incoming.msg_type == MSG_STEP_1 && !isVerifying) {
      isVerifying = true;
      verifyStartTime = millis();
      memcpy(busMac, mac, 6);
      addPeer(busMac); 

      // Car applies its token (Kc) to the payload and sends it back
      current_Kc = esp_random();
      uint32_t step2_payload = incoming.payload ^ current_Kc; 

      struct_message step2Msg;
      step2Msg.msg_type = MSG_STEP_2;
      step2Msg.payload = step2_payload;
      esp_now_send(busMac, (uint8_t *) &step2Msg, sizeof(step2Msg));
  }
  
  // Step 3: Bus sends back the final layer. Car evaluates it.
  else if (incoming.msg_type == MSG_STEP_3 && isVerifying) {
      if (memcmp(mac, busMac, 6) == 0) { 
          uint32_t final_check = incoming.payload ^ current_Kc; // Remove Kc
          
          if (final_check == SECRET_MESSAGE) {
              alertVerified = true; // Protocol matched!
          }
          isVerifying = false; // Close the verification window
      }
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(ledpin, OUTPUT);
  pinMode(backlightPin, OUTPUT);
  digitalWrite(backlightPin, HIGH); 

  ledcSetup(pwmChannel, 2000, resolution); 
  ledcAttachPin(buzzerPin, pwmChannel);

  if (!display.begin()) {
    Serial.println("Display failed!");
    while (1);
  }
  display.setRotation(2);
  display.setContrast(55); 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);

  updateScreen(" INITIALIZING");
  playTone(880, 100); delay(50);
  playTone(1318, 200); delay(1000);
  
  updateScreen("MONITORING...");
}

void loop() {
  // Scenario 1: Legitimate verification completed successfully
  if (alertVerified) {
    alertVerified = false; 
    triggerLegitimateWarning(); 
  }
  
  // Scenario 2: Verification window expired (Spoofer sent Step 1 but couldn't finish)
  if (isVerifying && (millis() - verifyStartTime > 400)) {
    isVerifying = false; // Close window
    triggerSpoofWarning(); // Silent OLED alert
  }

  delay(10); 
}