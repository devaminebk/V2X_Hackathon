#include <Arduino.h>
  #include <esp_now.h>
  #include <WiFi.h>
  #include <Wire.h>
  #include <Adafruit_MPU6050.h>
  #include <Adafruit_Sensor.h>

  // --- Pin Definitions ---
  const int TRIG_PIN = 23;
  const int ECHO_PIN = 19;
  const int BUTTON_PIN = 5; // Connect to GND, uses internal pull-up
  const int LED_PIN = 2;

  // --- Thresholds & Timers ---
  const int DISTANCE_THRESHOLD_CM = 5; // Kid detected if closer than 100cm
  const float MOVEMENT_THRESHOLD_X = 0.5;  // Acceleration threshold (m/s^2) for X axis
  unsigned long lastSendTime = 0;
  const int SEND_COOLDOWN_MS = 500;     // Prevent spamming ESP-NOW messages (2 seconds)

  // --- MPU6050 Object ---
  Adafruit_MPU6050 mpu;

  // --- IMU Software Filter Variables ---
  float filteredAccelX = 0.0;
  float filteredAccelY = 0.0;
  // Smoothing factor for EMA filter (0.0 to 1.0). 
  // Lower value = smoother (more filtering), Higher value = more responsive (less filtering)
  const float EMA_ALPHA = 0.2; 

  // --- ESP-NOW Sender Structure ---
  // 1. MUST MATCH THE RECEIVER'S STRUCTURE
  typedef struct struct_message {
      int value;
  } struct_message;

  struct_message myData;
  uint32_t packetCounter = 0;
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  // --- ESP-NOW Callback ---
  void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
      Serial.print("Packet ID: ");
      Serial.print(packetCounter);
      Serial.println(status == ESP_NOW_SEND_SUCCESS ? " -> Delivered" : " -> Fail");
      packetCounter++;
  }

  void setup() {
      Serial.begin(115200);

      // Initialize Pins
      pinMode(TRIG_PIN, OUTPUT);
      pinMode(ECHO_PIN, INPUT);
      pinMode(BUTTON_PIN, INPUT_PULLUP); // Button reads LOW when pressed
      pinMode(LED_PIN, OUTPUT);

      // Initialize MPU6050
      if (!mpu.begin()) {
          Serial.println("Failed to find MPU6050 chip! Check wiring.");
          delay(10);  
      }
      mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
      mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // Hardware filtering for bus engine vibrations
      Serial.println("MPU6050 initialized.");

      // Initialize WiFi & ESP-NOW (Using your exact setup logic)
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
      
      Serial.println("ESP-NOW Sender Initialized & System Ready");
  }

  // --- Helper: Get Ultrasonic Distance ---
  float getDistance() {
      digitalWrite(TRIG_PIN, LOW);
      delayMicroseconds(2);
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);
      
      long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
      if (duration == 0) return 999.0; // No echo
      return (duration * 0.0343) / 2.0; 
  }

  // --- Helper: Check if Bus is Stationary ---
  bool isBusStationary() {
  
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);
      
      // Apply Exponential Moving Average (EMA) Filter to X and Y axes
      filteredAccelX = (EMA_ALPHA * a.acceleration.x) + ((1.0 - EMA_ALPHA) * filteredAccelX);


      // Check if FILTERED X and Y acceleration are within the quiet threshold
      if (abs(filteredAccelX) < MOVEMENT_THRESHOLD_X) {
          return true;
      }
      return false;
  }

  void loop() {

      // 1. Check Button State
      bool isButtonPressed = (digitalRead(BUTTON_PIN) == LOW);
      
      // 2. Control LED indicator
      digitalWrite(LED_PIN, isButtonPressed ? HIGH : LOW);
      // 3. Scenario Logic: If Button is pressed AND Bus is stopped
      if (isButtonPressed && isBusStationary()) {
          
          // Check for kids exiting via Ultrasonic
          float distance = getDistance();
          
          if (distance < DISTANCE_THRESHOLD_CM) {
              
              // Check cooldown to prevent flooding the receiver
              if (millis() - lastSendTime > SEND_COOLDOWN_MS) {
                  Serial.print("ALERT! Kid exiting detected at ");
                  Serial.print(distance);
                  Serial.println(" cm.");


                  
                  // 4. ASSIGN THE VALUE TO THE STRUCT
                  myData.value = 1010; 

                  // 5. SEND THE WHOLE STRUCTURE
                  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
                
                  if (result != ESP_OK) {
                      Serial.println("Send Error");
                  }
                  
                  lastSendTime = millis(); // Reset cooldown
              }
          }
      }
      else if(isButtonPressed && !isBusStationary()) {
          // If button is not pressed, reset filtered values to avoid stale data
          filteredAccelX = 0.0;
          filteredAccelY = 0.0;

      }
      else {
          // If button is not pressed, reset filtered values to avoid stale data
          filteredAccelX = 0.0;
          filteredAccelY = 0.0;
      }
      
      delay(50); // Small loop delay for stability
  }