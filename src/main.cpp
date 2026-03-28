#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

/* * NOKIA 5110 PINS: RST:4, CE:5, DC:2, DIN:23, CLK:18, VCC:3V3, BL:15, GND:GND
 * BUZZER MODULE PINS: S:13, Middle:3V3, -:GND
 * LED PIN: 12
 */

// --- HARDWARE SETUP ---
Adafruit_PCD8544 display = Adafruit_PCD8544(18, 23, 2, 5, 4);

const int backlightPin = 15;
const int buzzerPin = 13;    
const int pwmChannel = 0;    
const int resolution = 8;    
const int ledpin = 12;       

// --- ESP-NOW SETUP ---
// Structure to receive data (Must match the sender!)
typedef struct struct_message {
  int value; 
} struct_message;

struct_message myData;

// This flag tells the main loop when to sound the alarm
volatile bool alertTriggered = false; 

// --- FUNCTIONS ---

// Function to play a quick tone
void playTone(int freq, int ms) {
  ledcWriteTone(pwmChannel, freq);
  delay(ms);
  ledcWriteTone(pwmChannel, 0); 
}

// Simple helper to refresh screen text
void updateScreen(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(" STATUS:");
  display.println("------------");
  display.println(msg);
  display.display();
}

// The Warning Sequence
void triggerSchoolBusWarning() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("  WARNING!");
  display.println("------------");
  display.println(" SCHOOL BUS");
  display.println("   AHEAD!");
  unsigned long startTime = millis();
  while (millis() - startTime < 2000) { 

    display.display();// Run for 2 seconds
    digitalWrite(ledpin, LOW); // LED ON (Active-LOW assumed)
    playTone(1200, 300); 
    delay(50);
    playTone(900, 300);  
    delay(50);
    digitalWrite(ledpin, HIGH);  // LED OFF
    delay(200);
  }

  digitalWrite(ledpin, LOW);  // LED OFF
  updateScreen("MONITORING...");
}

// Callback function that executes when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Received Value: ");
  Serial.println(myData.value);
  
  // If the sender sends a "1" (or any specific value), trigger the alert!
  if (myData.value == 1010) {
    alertTriggered = true; 
  }
}

void setup() {
  Serial.begin(115200);
  
  // --- INIT HARDWARE ---
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

  // --- INIT ESP-NOW ---
  WiFi.mode(WIFI_STA); // Set device as a Wi-Fi Station
  Serial.print("Receiver MAC Address: ");
  Serial.println(WiFi.macAddress()); // Note this down for your sender!

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register the receive callback
  esp_now_register_recv_cb(OnDataRecv);

  // Startup Beep & Screen
  updateScreen(" INITIALIZING");
  playTone(880, 100); delay(50);
  playTone(1318, 200); delay(1000);
  
  updateScreen("MONITORING...");
  Serial.println("System Ready. Waiting for ESP-NOW alerts...");
}

void loop() {
  // Check if the ESP-NOW callback flipped our alert flag
  if (alertTriggered == true) {
    triggerSchoolBusWarning(); // Run the lights and sounds
    alertTriggered = false;    // Reset the flag so it doesn't loop forever
  }
  
  // Let the ESP32 breathe
  delay(10); 
}