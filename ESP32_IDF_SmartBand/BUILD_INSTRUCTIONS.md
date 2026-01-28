# ESP32-S3 Smart Band Build Instructions

## Quick Start (Using VS Code ESP-IDF Extension)

Since you have the ESP-IDF extension installed, follow these steps to build the project:

### Method 1: Using VS Code Extension (Recommended)

1. **Open the project folder in VS Code:**
   - Open `c:\_kamivour\Code\University\microprocessor\ESP32_IDF_SmartBand` in VS Code

2. **Set ESP-IDF Target:**
   - Press `F1` (or `Ctrl+Shift+P`)
   - Type: `ESP-IDF: Set Espressif device target`
   - Select: `esp32s3`

3. **Build the project:**
   - Press `F1`
   - Type: `ESP-IDF: Build your project`
   - Wait for build to complete (first build takes 5-10 minutes)

4. **Flash to device** (when you have hardware):
   - Connect ESP32-S3 via USB
   - Press `F1`
   - Type: `ESP-IDF: Select port to use`
   - Select your COM port
   - Press `F1`
   - Type: `ESP-IDF: Flash your project`

5. **Monitor Serial Output:**
   - Press `F1`
   - Type: `ESP-IDF: Monitor device`

### Method 2: Using idf.py Command (if VS Code extension doesn't work)

If the VS Code extension has issues, you can build from PowerShell:

1. **Open PowerShell in the project folder**
2. **Set environment variables** (you need to find your ESP-IDF installation path):
   ```powershell
   $env:IDF_PATH="C:\Users\mjnhp\.espressif\dist\<esp-idf-folder>"
   ```

3. **Run build:**
   ```powershell
   idf.py set-target esp32s3
   idf.py build
   ```

## Project Status

✅ **All files created and code reviewed:**
- MPU6050 I2C driver
- Kalman filter algorithms
- WiFi/UDP manager
- BLE GATT server (NimBLE)
- JSON serialization
- Main application with FreeRTOS tasks

✅ **Code issues fixed:**
- Added missing `string.h` include in BLE server
- Removed unused `parse_uuid128` function
- All components verified for correctness

## Build Output

After successful build, you'll see:
```
Project build complete. To flash, run:
 idf.py -p (PORT) flash
or run:
 idf.py -p (PORT) flash monitor
```

## Sending to Your Friend

After building, you need to send the following file to your friend:

**Firmware Binary Location:**
```
ESP32_IDF_SmartBand\build\esp32_smartband.bin
```

Or entire build folder:
```
ESP32_IDF_SmartBand\build\
```

Your friend will need to flash it using one of these methods:

### Option A: Using esptool.py (simplest for friend)
```bash
esptool.py --chip esp32s3 --port COM<X> write_flash 0x0 esp32_smartband.bin
```

### Option B: Using Flash Download Tool
- Download ESP Flash Download Tool from Espressif
- Load the binary file
- Flash to address 0x0

## WiFi Credentials

**IMPORTANT:** WiFi credentials are hardcoded in the firmware:
- SSID: `DnMinh`
- Password: `mat khau`

Your friend will need to use this WiFi network, OR you need to rebuild with different credentials in:
- File: `main/wifi_manager.h` lines 8-9

## Hardware Setup Checklist for Your Friend

```
ESP32-S3 SuperMini Wiring:
├── MPU6050 Sensor
│   ├── VCC  → 3.3V
│   ├── GND  → GND
│   ├── SDA  → GPIO 8
│   └── SCL  → GPIO 9
└── USB Cable (for power and serial monitor)
```

## Expected Behavior

When powered on, serial output should show:
1. "Connecting to WiFi: DnMinh"
2. "✅ WiFi OK | IP: x.x.x.x"
3. "✅ BLE OK"
4. "✅ MPU6050 initialized"
5. "✅ READY"
6. JSON packets every ~1 second

## Testing Checklist

- [ ] WiFi connects successfully
- [ ] BLE advertising (visible as "ESP32 SmartBand")
- [ ] MPU6050 reads sensor data
- [ ] UDP broadcasts to port 4210
- [ ] BLE notifications work
- [ ] JSON data format is correct

## Troubleshooting

### Build Errors
- Make sure ESP-IDF extension is fully installed
- Try "ESP-IDF: Doctor command" in VS Code
- Check ESP-IDF version (v5.2 or v5.3 required)

### Can't Find COM Port
- Install USB-Serial drivers for ESP32-S3
- Check Device Manager
- Try different USB cable/port

### MPU6050 Not Responding
- Check wiring (especially I2C pullups)
- Verify MPU6050 address is 0x68
- Check 3.3V power supply

## Next Steps

1. Build the project using VS Code ESP-IDF extension
2. Send the compiled binary to your friend
3. Friend flashes and tests with hardware
4. If issues arise, check serial output for error messages
