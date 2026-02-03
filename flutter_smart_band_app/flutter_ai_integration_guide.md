# Flutter Integration Guide: Fall Detection AI

## 🚨 Overview
The ESP32-S3 firmware has been updated with an edge AI model for **Fall Detection**.
The device now broadcasts a new `fall` status field in the JSON data stream via BLE and UDP.

This guide outlines how to handle this new data in the Flutter app to trigger emergency alerts.

---

## 1. Updated Data Format
The JSON packet sent by the band (at ~50Hz) now includes a `fall` integer field.

**Previous Format:**
```json
{"ts":12345, "pitch":-5.23, "hr":72, "spo2":98, ...}
```

**New Format:**
```json
{
  "ts": 12345,
  "pitch": -5.23,
  "roll": 2.14,
  "svm": 1.02,
  "gx": 0.12,
  "gy": -0.05,
  "gz": 0.08,
  "hr": 72,      // Heart Rate
  "spo2": 98,    // Blood Oxygen
  "bat": 98,     // Battery %
  "fall": 1      // ⚠️ 1 = Fall Detected, 0 = Normal
}
```

> [!IMPORTANT]
> The `fall` field will be `0` normally. It will switch to `1` immediately after a fall is detected by the AI model. It may only stay `1` for a short period (1-2 seconds) depending on the next inference cycle, so your app implies **latching** the alert state until dismissed by the user.

---

## 2. Implementation Steps

### Step 1: Update Data Model
Add the `fall` field to your sensor data class.

```dart
class SensorData {
  final int timestamp;
  final double pitch;
  final double roll;
  final int heartRate;
  final int spo2;
  final bool isFallDetected; // NEW

  SensorData({
    required this.timestamp,
    required this.pitch,
    required this.roll,
    required this.heartRate,
    required this.spo2,
    required this.isFallDetected,
  });

  factory SensorData.fromJson(Map<String, dynamic> json) {
    return SensorData(
      timestamp: json['ts'] ?? 0,
      pitch: (json['pitch'] ?? 0).toDouble(),
      roll: (json['roll'] ?? 0).toDouble(),
      heartRate: json['hr'] ?? 0,
      spo2: json['spo2'] ?? 0,
      isFallDetected: (json['fall'] ?? 0) == 1, // Map 1 to true
    );
  }
}
```

### Step 2: Handle Alert Logic
In your state management (Provider/Bloc/GetX), listen for `isFallDetected == true`.

**Recommendation:**
When a fall is detected, trigger a persistent **Emergency Overlay** or **Dialog** that requires user interaction to dismiss. Do not just show a toast message, as it might be missed during an accident.

```dart
void onDataReceived(SensorData data) {
  // Update UI graphs...
  
  // Check for Fall
  if (data.isFallDetected) {
    _triggerEmergencyAlert();
  }
}

void _triggerEmergencyAlert() {
  // 1. Vibrate phone
  Vibration.vibrate(pattern: [500, 1000, 500, 2000]);
  
  // 2. Show critical alert dialog
  showDialog(
    context: context,
    barrierDismissible: false, // User MUST tap button
    builder: (context) => AlertDialog(
      title: Row(children: [
        Icon(Icons.warning_amber_rounded, color: Colors.red, size: 40),
        SizedBox(width: 10),
        Text("FALL DETECTED!")
      ]),
      content: Text("Are you okay? Sending emergency SMS in 30 seconds..."),
      backgroundColor: Colors.red[50],
      actions: [
        TextButton(
          onPressed: () { 
            // Cancel timer
            Navigator.pop(context); 
          },
          child: Text("I'M OKAY", style: TextStyle(fontSize: 20)),
        ),
      ],
    ),
  );
  
  // 3. Start countdown timer for SOS (optional feature)
}
```

---

## 3. Testing
To verify your Flutter implementation without hurting yourself:
1.  Connect the Smart Band to the app.
2.  Hold the band in your hand.
3.  Simulate a shock by **hitting the band sharply** against your palm or a soft surface (like a mattress).
4.  Wait ~1-2 seconds.
5.  The app should trigger the red "FALL DETECTED" alert.
