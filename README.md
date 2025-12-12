# Smart Band Monitoring System

A real-time wearable sensor monitoring system consisting of an ESP32-based smart bracelet with MPU6050 IMU sensor and a Flutter mobile application for data visualization and recording.

## 🎯 Project Overview

This project combines embedded systems and mobile development to create a complete activity monitoring solution:

- **ESP32 Firmware**: Collects motion data from MPU6050 sensor, applies Kalman filtering for smooth angle calculations, and transmits data via WiFi (UDP) or Bluetooth LE
- **Flutter App**: Visualizes real-time sensor data with dual graphs (angles and gyroscope), supports activity labeling, and exports data to CSV for analysis

### Key Features

- Real-time pitch, roll, and SVM (Signal Vector Magnitude) angle tracking
- Kalman filter fusion of accelerometer and gyroscope data
- Dual connectivity: WiFi UDP (high-speed) and BLE (low-energy)
- Responsive dual-graph layout (adapts to portrait/landscape orientation)
- Activity labeling and CSV data recording
- Dark-themed UI with coordinate axes

## 📋 Prerequisites

### Hardware
- **ESP32-S3 SuperMini** (or compatible ESP32 board)
- **MPU6050** 6-axis IMU sensor
- USB cable for programming
- I2C connections: GPIO8 (SDA), GPIO9 (SCL)

### Software

**For ESP32 Firmware:**
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3.7+ (for PlatformIO)

**For Flutter App:**
- [Flutter SDK](https://flutter.dev/docs/get-started/install) 3.0+
- [Dart](https://dart.dev/get-dart) 3.10+
- Android Studio or VS Code with Flutter extension
- Android device or emulator (target platform)

## 🚀 Installation

### 1. ESP32 Firmware Setup

```bash
cd Microprocessor_ESP32_bracelet

# Install dependencies (PlatformIO will auto-install)
pio pkg install

# Configure WiFi credentials (edit src/main.cpp)
# Replace SSID and PASSWORD with your network credentials
```

**Configure WiFi in `src/main.cpp`:**
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 2. Flutter App Setup

```bash
cd flutter_smart_band_app

# Install Flutter dependencies
flutter pub get

# Check for any issues
flutter doctor
```

## ▶️ How to Run

### Running the ESP32 Firmware

1. **Connect ESP32** to your computer via USB

2. **Upload firmware:**
   ```bash
   cd Microprocessor_ESP32_bracelet
   pio run --target upload
   ```

3. **Monitor serial output** (optional):
   ```bash
   pio device monitor
   ```

4. **Note the IP address** displayed in serial monitor after WiFi connection

### Running the Flutter App

1. **Connect your Android device** via USB with USB debugging enabled, or start an emulator

2. **Verify device connection:**
   ```bash
   flutter devices
   ```

3. **Run the app:**
   ```bash
   cd flutter_smart_band_app
   flutter run
   ```

4. **Connect to ESP32:**
   - Tap **CONNECT** button
   - Choose **WiFi (UDP)** for high-speed connection
   - Or choose **Bluetooth LE** to scan and pair

## 📊 Usage

### Data Visualization
- **Portrait mode**: Angles graph (top) and gyroscope graph (bottom)
- **Landscape mode**: Angles graph (left) and gyroscope graph (right)
- Toggle between graph and raw data view using the chart/list icon

### Recording Data
1. Select activity label from dropdown (Normal, Walking, Running, Falling, Lying Down)
2. Tap **RECORD** button to start recording
3. Perform activity
4. Tap **STOP & SAVE** to export CSV file
5. CSV files saved to: `Android/data/com.example.smart_band_app/files/`

### Sensor Data
- **Pitch/Roll**: Orientation angles (-180° to +180°)
- **SVM**: Signal Vector Magnitude (total acceleration)
- **Gyroscope**: Angular velocity in °/s (-500 to +500)

## 🛠️ Technical Details

### ESP32 Configuration
- **Flash**: 4MB (99.8% utilized: 1,307,913 bytes)
- **I2C**: 400kHz clock speed
- **Sampling**: 50Hz (20ms intervals)
- **Kalman Parameters**: Q_angle=0.001, Q_bias=0.003, R_measure=0.3

### Communication Protocols
- **WiFi UDP**: Broadcast to 255.255.255.255:4210
- **BLE GATT**: Notify characteristic with 512-byte MTU
- **Data Format**: JSON with fields: `ts`, `bat`, `pitch`, `roll`, `svm`, `gx`, `gy`, `gz`

### Flutter App
- **Chart Buffer**: 100 data points
- **Event-driven**: Updates only on new data (no polling timer)
- **Dependencies**: `fl_chart`, `flutter_blue_plus`, `csv`, `path_provider`, `permission_handler`

## 📁 Project Structure

```
microprocessor/
├── Microprocessor_ESP32_bracelet/    # ESP32 firmware
│   ├── src/
│   │   └── main.cpp                   # Main firmware code
│   ├── platformio.ini                 # PlatformIO configuration
│   └── include/                       # Header files
│
└── flutter_smart_band_app/           # Flutter mobile app
    ├── lib/
    │   ├── main.dart                  # Main app UI and logic
    │   └── sensor_data.dart           # Data model for CSV
    ├── pubspec.yaml                   # Flutter dependencies
    └── android/                       # Android-specific files
```

## 🔧 Troubleshooting

**ESP32 won't connect to WiFi:**
- Verify SSID and password in `main.cpp`
- Check WiFi signal strength
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)

**Flutter app shows "No devices found" on BLE scan:**
- Enable Bluetooth on Android device
- Grant location and Bluetooth permissions
- Ensure ESP32 is powered and running

**Graph shows no data:**
- Check connection status indicator
- Verify ESP32 is sending data (check serial monitor)
- For WiFi: Ensure devices are on same network

**Build errors on Flutter:**
- Run `flutter clean && flutter pub get`
- Update Flutter: `flutter upgrade`
- Check `flutter doctor` for missing dependencies

## 📄 License

This project is for educational purposes as part of a microprocessor university course.

## 👥 Author

Developed as a university microprocessor project demonstrating embedded systems, sensor fusion, and mobile application development.
