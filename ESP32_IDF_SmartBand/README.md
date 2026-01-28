# ESP32-S3 Smart Band - ESP-IDF Version

This is the ESP-IDF framework version of the ESP32-S3 smart band project. It reads data from MPU6050 6-axis sensor, processes it through Kalman filters, and transmits via WiFi UDP and BLE.

## Features
- MPU6050 sensor reading via I2C (GPIO8=SDA, GPIO9=SCL)
- Dual Kalman filter implementation (angle fusion + signal smoothing)
- WiFi UDP broadcasting (255.255.255.255:4210)
- BLE GATT server with notifications
- JSON data serialization
- 50Hz sampling rate
- Serial debugging output

## Prerequisites

### 1. Install ESP-IDF Extension for VS Code

1. Open VS Code
2. Go to Extensions (`Ctrl+Shift+X`)
3. Search for **"Espressif IDF"** or **"ESP-IDF"**
4. Click Install on the extension by Espressif Systems

### 2. Configure ESP-IDF

1. Press `F1` or `Ctrl+Shift+P`
2. Type **"ESP-IDF: Configure ESP-IDF Extension"**
3. Select **EXPRESS** installation mode
4. Choose ESP-IDF version: **v5.3** or **v5.2** (latest stable)
5. Select Python 3.x installation path
6. Choose installation directory (e.g., `C:\esp\esp-idf`)
7. Wait for installation to complete (this may take 10-20 minutes)

### 3. Verify Installation

After installation, check if ESP-IDF is working:
```bash
idf.py --version
```

## Hardware Configuration

- **Board**: ESP32-S3 SuperMini
- **Flash**: 4MB
- **I2C Pins**:
  - SDA: GPIO 8
  - SCL: GPIO 9
- **Sensor**: MPU6050 (address 0x68)

## WiFi Configuration

Edit `main/wifi_manager.c` to set your WiFi credentials:
```c
#define WIFI_SSID "YourWiFiName"
#define WIFI_PASS "YourPassword"
```

## Build and Flash

### Using VS Code ESP-IDF Extension

1. Open this folder in VS Code
2. Press `F1` and run **"ESP-IDF: Set Espressif device target"**
   - Select: `esp32s3`
3. Press `F1` and run **"ESP-IDF: Build your project"**
4. Connect your ESP32-S3 via USB
5. Press `F1` and run **"ESP-IDF: Flash your project"**
6. Press `F1` and run **"ESP-IDF: Monitor device"**

### Using Command Line

```bash
# Set target
idf.py set-target esp32s3

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p COM<X> flash monitor
```

Replace `COM<X>` with your actual COM port (check Device Manager on Windows).

## Output Data Format

JSON packets sent via UDP and BLE:
```json
{
  "ts": 12345,      // timestamp (ms)
  "pitch": -5.23,   // pitch angle (degrees)
  "roll": 2.14,     // roll angle (degrees)
  "svm": 1.02,      // signal vector magnitude (g)
  "gx": 0.12,       // gyro X (deg/s)
  "gy": -0.05,      // gyro Y (deg/s)
  "gz": 0.08,       // gyro Z (deg/s)
  "bat": 98         // battery level (%)
}
```

## BLE Connection

- **Device Name**: ESP32 SmartBand
- **Service UUID**: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
- **Characteristic UUID**: beb5483e-36e1-4688-b7f5-ea07361b26a8
- **Properties**: READ, NOTIFY

Use the existing Flutter app or any BLE scanner (e.g., nRF Connect) to connect and receive notifications.

## Serial Monitor Output

Expected output on successful startup:
```
Connecting to WiFi: YourWiFiName
...
✅ WiFi OK | IP: 192.168.x.x

📱 BLE Setup...
✅ BLE OK

🔧 MPU6050...
   ✅ MPU6050 initialized
✅ READY

📡 UDP: {"ts":12345,"pitch":-5.23,"roll":2.14,"svm":1.02,"gx":0.12,"gy":-0.05,"gz":0.08,"bat":98}
```

## Troubleshooting

### Build Errors
- Make sure ESP-IDF is properly installed and configured
- Check that target is set to `esp32s3`
- Try `idf.py fullclean` then rebuild

### Flash Errors
- Check COM port is correct
- Hold BOOT button while connecting USB (ESP32-S3 may need manual boot mode)
- Try different USB cable or port

### MPU6050 Not Responding
- Check I2C wiring (SDA=GPIO8, SCL=GPIO9)
- Verify MPU6050 address is 0x68 (not 0x69)
- Check power supply to sensor

### WiFi Not Connecting
- Verify SSID and password in `wifi_manager.c`
- Check WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check serial output for connection status

## Project Structure

```
ESP32_IDF_SmartBand/
├── CMakeLists.txt           # Root CMake configuration
├── sdkconfig               # ESP-IDF configuration (generated)
├── main/
│   ├── CMakeLists.txt      # Main component CMake
│   ├── main.c              # Application entry point
│   ├── mpu6050.c/.h        # MPU6050 I2C driver
│   ├── kalman_filter.c/.h  # Kalman filter implementation
│   ├── wifi_manager.c/.h   # WiFi and UDP handling
│   ├── ble_server.c/.h     # BLE GATT server
│   └── json_utils.c/.h     # JSON serialization
└── README.md              # This file
```
