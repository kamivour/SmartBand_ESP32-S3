# BLE Connection Issues - ESP32 to Flutter App

## Background
- **Previous setup**: Arduino framework on PlatformIO - BLE worked ✅
- **Current setup**: ESP-IDF v5.5.2 with NimBLE stack - BLE not discoverable on Android ❌
- **ESP32 hardware**: ESP32-S3 Supermini
- **ESP32 BLE status**: Fully operational and advertising (confirmed in logs)
- **Date**: February 2, 2026

---

## ESP32 Side - Current BLE Configuration (Confirmed Working)

### Device Information
- **Device Name**: `ESP32 SmartBand`
- **BLE MAC Address**: `d0:cf:13:2f:48:e2`
- **Advertising Mode**: General Discoverable (disc_mode=2)
- **Advertising Interval**: 20-40ms (fast advertising)
- **Connection Mode**: Undirected connectable
- **BLE Stack**: NimBLE (not Bluedroid)

### Service UUIDs (Custom 128-bit)
- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c333914b4b`
- **Characteristic UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **Characteristic Properties**: Read + Notify

### Verified from ESP32 Logs
```
I (340) BLE: 📱 BLE Setup...
I (341) BLE_INIT: BT controller compile version [5106725]
I (345) BLE: NimBLE port initialized
I (346) BLE: GAP and GATT services initialized
I (346) BLE: GATT services registered
I (353) NimBLE: GAP procedure initiated: advertise; 
I (353) NimBLE: disc_mode=2
I (354) BLE: BLE advertising started
I (355) BLE: ✅ BLE OK
```

---

## Key Differences: Arduino BLE vs NimBLE (ESP-IDF)

| Aspect | Arduino BLE (Old) | NimBLE (Current) |
|--------|------------------|------------------|
| **BLE Stack** | Bluedroid (classic) | NimBLE (lightweight) |
| **Address Type** | Random/Public varies | Public address (BLE_OWN_ADDR_PUBLIC) |
| **UUID Byte Order** | May differ | Little-endian in memory |
| **Service Discovery** | May auto-include in adv | NOT in advertising packet (only name + flags) |
| **MTU Size** | Default 23-185 | Default 256 (configurable) |
| **Security/Pairing** | May auto-enable | Legacy + Secure Connections enabled |
| **Advertising Data** | May include service UUIDs | Only device name + flags |

---

## Flutter App - Issues to Check

### 1. Android Permissions (Critical for Android 12+)

#### AndroidManifest.xml Required Permissions:
```xml
<!-- Bluetooth permissions for Android 12+ (API 31+) -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" 
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

<!-- Location permissions (required for BLE on Android) -->
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />

<!-- For older Android versions -->
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
```

#### Runtime Permission Request:
```dart
// Must request these at runtime
await Permission.bluetoothScan.request();
await Permission.bluetoothConnect.request();
await Permission.location.request();
```

**⚠️ CRITICAL**: Location services must be **turned ON** in phone settings (even though we're not using location).

---

### 2. BLE Scan Configuration

#### ❌ PROBLEM: Filtering by Service UUID
The Flutter app may be scanning with filters that exclude NimBLE devices:

```dart
// This will FAIL with NimBLE because service UUID is NOT in advertising packet:
await FlutterBluePlus.startScan(
  withServices: [Guid("4fafc201-1fb5-459e-8fcc-c5c333914b4b")],
);
```

#### ✅ SOLUTION: Scan Without Filters
```dart
// Scan for ALL devices
await FlutterBluePlus.startScan(
  timeout: Duration(seconds: 10),
  // Don't filter by services
  withServices: [],
  // Or remove this parameter entirely
);

// Listen for all devices
FlutterBluePlus.scanResults.listen((results) {
  for (ScanResult r in results) {
    print('Found: ${r.device.name} - ${r.device.id}');
    
    // Look for "ESP32 SmartBand" by name
    if (r.device.name == "ESP32 SmartBand") {
      print('Found our device!');
      // Connect to it
    }
  }
});
```

---

### 3. Device Name Matching

- **Old Arduino code device name**: [UNKNOWN - check old code]
- **New ESP-IDF device name**: `ESP32 SmartBand`

**Action**: Update Flutter app to search for `"ESP32 SmartBand"` (case-sensitive).

---

### 4. Service UUID Discovery Flow

#### ❌ WRONG Approach (Won't work with NimBLE):
```dart
// Trying to find device by service UUID during scan
var device = scanResults.firstWhere(
  (r) => r.advertisementData.serviceUuids.contains(myServiceUuid)
);
```

#### ✅ CORRECT Approach:
```dart
// Step 1: Find device by name during scan
var result = scanResults.firstWhere(
  (r) => r.device.name == "ESP32 SmartBand"
);

// Step 2: Connect to device
await result.device.connect();

// Step 3: Discover services AFTER connection
List<BluetoothService> services = await result.device.discoverServices();

// Step 4: Find your service and characteristic
var service = services.firstWhere(
  (s) => s.uuid == Guid("4fafc201-1fb5-459e-8fcc-c5c333914b4b")
);

var characteristic = service.characteristics.firstWhere(
  (c) => c.uuid == Guid("beb5483e-36e1-4688-b7f5-ea07361b26a8")
);

// Step 5: Enable notifications
await characteristic.setNotifyValue(true);
characteristic.value.listen((value) {
  // Handle data
});
```

---

### 5. UUID Format Verification

Ensure UUIDs in Flutter app match ESP32 **exactly**:

```dart
// Service UUID
final serviceUuid = Guid("4fafc201-1fb5-459e-8fcc-c5c333914b4b");

// Characteristic UUID  
final characteristicUuid = Guid("beb5483e-36e1-4688-b7f5-ea07361b26a8");
```

**Note**: Check if old Arduino code used different UUIDs.

---

### 6. Location Services Check

```dart
// Before scanning, check if location is enabled
import 'package:permission_handler/permission_handler.dart';

Future<bool> checkLocationEnabled() async {
  var status = await Permission.location.status;
  if (status.isDenied) {
    // Request permission
    status = await Permission.location.request();
  }
  
  // Also check if location service is enabled
  if (!await Permission.location.serviceStatus.isEnabled) {
    // Show dialog asking user to enable location in settings
    return false;
  }
  
  return status.isGranted;
}

// Use before scanning
if (await checkLocationEnabled()) {
  await FlutterBluePlus.startScan();
} else {
  print("Location services not enabled!");
}
```

---

### 7. BLE Library Compatibility

If using **flutter_blue** (deprecated), switch to:

#### Option 1: flutter_blue_plus (Recommended)
```yaml
dependencies:
  flutter_blue_plus: ^1.31.0
```

#### Option 2: flutter_reactive_ble
```yaml
dependencies:
  flutter_reactive_ble: ^5.3.0
```

**Reason**: Older libraries may not handle NimBLE advertising packets correctly.

---

### 8. Scan Mode Configuration

Some apps use low-power scan mode which may miss fast-advertising devices:

```dart
// If your library supports scan mode, use balanced or low latency
await FlutterBluePlus.startScan(
  timeout: Duration(seconds: 10),
  androidScanMode: AndroidScanMode.lowLatency, // More aggressive scanning
);
```

---

## Testing & Debugging Steps

### Step 1: Remove ALL Filters
```dart
// Simplest possible scan - print everything
await FlutterBluePlus.startScan(timeout: Duration(seconds: 10));

FlutterBluePlus.scanResults.listen((results) {
  for (ScanResult r in results) {
    print('======================');
    print('Device: ${r.device.name}');
    print('ID: ${r.device.id}');
    print('RSSI: ${r.rssi}');
    print('Service UUIDs in adv: ${r.advertisementData.serviceUuids}');
    print('======================');
  }
});
```

**Expected**: You should see `ESP32 SmartBand` in the list.

### Step 2: Use nRF Connect App
1. Install "nRF Connect" from Play Store
2. Scan for BLE devices
3. Look for "ESP32 SmartBand"
4. If visible in nRF Connect but not in your app → Flutter app issue
5. If NOT visible in nRF Connect → Phone/Android issue

### Step 3: Check Android Version
- **Android 12+ (API 31+)**: More strict permissions
- **Android 11 and below**: Simpler permission model

Test on Android 11 device first to isolate issue.

### Step 4: Verify Permissions at Runtime
```dart
void checkPermissions() async {
  var bluetooth = await Permission.bluetooth.status;
  var bluetoothScan = await Permission.bluetoothScan.status;
  var bluetoothConnect = await Permission.bluetoothConnect.status;
  var location = await Permission.location.status;
  
  print('Bluetooth: $bluetooth');
  print('Bluetooth Scan: $bluetoothScan');
  print('Bluetooth Connect: $bluetoothConnect');
  print('Location: $location');
  
  // ALL should be granted
}
```

### Step 5: Enable Verbose BLE Logging
```dart
// Add to main() or before scanning
FlutterBluePlus.setLogLevel(LogLevel.verbose);
```

---

## Common Issues & Solutions

### Issue: "No devices found"
**Solutions**:
- ✅ Remove service UUID filters from scan
- ✅ Request all permissions at runtime
- ✅ Enable Location services in phone settings
- ✅ Increase scan timeout (try 15-20 seconds)
- ✅ Use `androidScanMode: AndroidScanMode.lowLatency`

### Issue: "Permission denied"
**Solutions**:
- ✅ Add all permissions to AndroidManifest.xml
- ✅ Request permissions at runtime before scanning
- ✅ Check Android version (12+ needs new permissions)
- ✅ Ask user to grant permissions manually in app settings

### Issue: "Device found but can't connect"
**Solutions**:
- ✅ Check if trying to connect while still scanning (stop scan first)
- ✅ Increase connection timeout
- ✅ Check if device is already connected to another app

### Issue: "Service not found after connection"
**Solutions**:
- ✅ Verify UUID format matches exactly
- ✅ Call `discoverServices()` after connection
- ✅ Wait for connection state = connected before discovering
- ✅ Check if UUIDs changed from Arduino to ESP-IDF version

---

## ESP32 Confirmation

**The ESP32 is definitely working because:**
- ✅ WiFi AP "ESP32_SmartBand" is visible and working
- ✅ Serial logs show: `BLE advertising started`
- ✅ NimBLE GAP procedure initiated successfully
- ✅ No BLE errors in ESP32 logs
- ✅ Proper advertising parameters configured (disc_mode=2, 20-40ms interval)

**The issue is 100% on the Android/Flutter side** - either permissions, scan configuration, or filters.

---

## Quick Checklist for Flutter Developer

- [ ] All BLE permissions added to AndroidManifest.xml
- [ ] Runtime permissions requested and granted
- [ ] Location services enabled on phone
- [ ] Scan without service UUID filters
- [ ] Search by device name "ESP32 SmartBand"
- [ ] Using flutter_blue_plus (not deprecated flutter_blue)
- [ ] Scan timeout at least 10 seconds
- [ ] Tested with nRF Connect app
- [ ] Verbose logging enabled
- [ ] Discover services AFTER connection (not during scan)

---

## Contact Information

If issues persist after checking all above:
1. Provide scan results log showing all discovered devices
2. Share permission request code
3. Show current BLE library version
4. Confirm Android version and phone model
5. Share nRF Connect app screenshot
