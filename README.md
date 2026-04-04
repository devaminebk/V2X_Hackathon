# V2X Hackathon

## Project Structure

- **Switch to CarNode branch** to see the code implemented in the Car
- **Switch to BusNode branch** to see the code implemented in the Bus
- **Switch to AINode branch** to see the AI/ML models and training code

## Hardware Used

### Car Node
- ESP32 Wroom32D DevKit V1
- Buzzer
- Red LED
- OLED Display

### Bus Node
- ESP32 Wroom32D DevKit V1
- Push button
- Gyroscope (MPU6050)
- Accelerometer (MPU6050)
- Ultrasonic Sensor (HC-SR04)

## Getting Started

Each component has its own setup:

- **CarNode & BusNode**: Use PlatformIO for C++ development
- **AINode**: Python-based ML models - install dependencies with `pip install -r requirements.txt`
