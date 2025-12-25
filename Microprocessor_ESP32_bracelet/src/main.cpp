#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>

// --- CẤU HÌNH I2C & MPU6050 ---
#define I2C_SDA 8
#define I2C_SCL 9
#define MPU6050_ADDR 0x68

// --- KALMAN FILTER STRUCTURES ---
typedef struct {
    double Q_angle, Q_bias, R_measure;
    double angle, bias;
    double P[2][2];
} Kalman_t;

typedef struct {
    double err_measure, err_estimate, q;
    double current_estimate, last_estimate, kalman_gain;
} SimpleKalman_t;

// --- CẤU HÌNH WIFI ---
const char* ssid = "IoT_Automation_Lab";     // <--- ĐIỀN WIFI CỦA BẠN
const char* password = "Edabk@408"; // <--- ĐIỀN PASS WIFI
const char* udpAddress = "255.255.255.255"; // Broadcast cho mọi thiết bị đều nhận được
const int udpPort = 4210;
WiFiUDP udp;

// --- CẤU HÌNH BLE ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// Callback kiểm soát kết nối BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println(">>> BLE CLIENT CONNECTED <<<");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println(">>> BLE CLIENT DISCONNECTED <<<");
      pServer->getAdvertising()->start(); // Quảng cáo lại để App tìm thấy
    }
};

// --- DATA ---
const int SAMPLE_RATE_HZ = 50; 
const int INTERVAL_MS = 1000 / SAMPLE_RATE_HZ;
unsigned long previousMillis = 0;
float batLevel = 100.0;

// --- KALMAN FILTER INSTANCES ---
Kalman_t kPitch, kRoll;  // For angle fusion
SimpleKalman_t kSVM;      // For SVM smoothing
SimpleKalman_t kalmanGx, kalmanGy, kalmanGz;  // For gyro smoothing

// --- KALMAN FILTER FUNCTIONS ---
void Kalman_Init(Kalman_t *k) {
    k->Q_angle = 0.001;
    k->Q_bias = 0.003;
    k->R_measure = 0.3;  // Increased from 0.03 to reduce accelerometer noise
    k->angle = 0.0;
    k->bias = 0.0;
    k->P[0][0] = 0.0; k->P[0][1] = 0.0;
    k->P[1][0] = 0.0; k->P[1][1] = 0.0;
}

double Kalman_GetAngle(Kalman_t *k, double newAngle, double newRate, double dt) {
    double rate = newRate - k->bias;
    k->angle += rate * dt;
    k->P[0][0] += dt * (dt*k->P[1][1] - k->P[0][1] - k->P[1][0] + k->Q_angle);
    k->P[0][1] -= dt * k->P[1][1];
    k->P[1][0] -= dt * k->P[1][1];
    k->P[1][1] += k->Q_bias * dt;
    double S = k->P[0][0] + k->R_measure;
    double K[2];
    K[0] = k->P[0][0] / S;
    K[1] = k->P[1][0] / S;
    double y = newAngle - k->angle;
    k->angle += K[0] * y;
    k->bias += K[1] * y;
    double P00_temp = k->P[0][0];
    double P01_temp = k->P[0][1];
    k->P[0][0] -= K[0] * P00_temp;
    k->P[0][1] -= K[0] * P01_temp;
    k->P[1][0] -= K[1] * P00_temp;
    k->P[1][1] -= K[1] * P01_temp;
    return k->angle;
}

void SimpleKalman_Init(SimpleKalman_t *k, double mea_e, double est_e, double q) {
    k->err_measure = mea_e;
    k->err_estimate = est_e;
    k->q = q;
    k->current_estimate = 0;
    k->last_estimate = 0;
}

double SimpleKalman_Update(SimpleKalman_t *k, double mea) {
    k->kalman_gain = k->err_estimate / (k->err_estimate + k->err_measure);
    k->current_estimate = k->last_estimate + k->kalman_gain * (mea - k->last_estimate);
    k->err_estimate = (1.0 - k->kalman_gain) * k->err_estimate + k->q * fabs(k->last_estimate - k->current_estimate);
    k->last_estimate = k->current_estimate;
    return k->current_estimate;
}

// --- MPU6050 FUNCTIONS ---
void MPU6050_Init() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000); // 400kHz
    
    // Wake up MPU6050
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x6B); // PWR_MGMT_1 register
    Wire.write(0x00); // Wake up
    Wire.endTransmission(true);
    
    Serial.println("   ✅ MPU6050 initialized");
}

bool MPU6050_ReadRaw(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x3B); // Starting register for accel data
    Wire.endTransmission(false);
    Wire.requestFrom(MPU6050_ADDR, 14, true);
    
    if (Wire.available() == 14) {
        int16_t axRaw = (Wire.read() << 8) | Wire.read();
        int16_t ayRaw = (Wire.read() << 8) | Wire.read();
        int16_t azRaw = (Wire.read() << 8) | Wire.read();
        Wire.read(); Wire.read(); // Skip temperature
        int16_t gxRaw = (Wire.read() << 8) | Wire.read();
        int16_t gyRaw = (Wire.read() << 8) | Wire.read();
        int16_t gzRaw = (Wire.read() << 8) | Wire.read();
        
        // Convert to g (accel) and deg/s (gyro)
        ax = axRaw / 16384.0;
        ay = ayRaw / 16384.0;
        az = azRaw / 16384.0;
        gx = gxRaw / 131.0;
        gy = gyRaw / 131.0;
        gz = gzRaw / 131.0;
        
        return true;
    }
    return false;
}

void setup() {
  Serial.begin(115200);

  // 1. Setup WiFi
  Serial.printf("\nConnecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  
  // Đợi WiFi kết nối (timeout 10 giây)
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n✅ WiFi OK | IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠️ WiFi FAILED");
  }
  
  // 2. Setup BLE
  Serial.println("\n📱 BLE Setup...");
  BLEDevice::init("ESP32 SmartBand");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("✅ BLE OK");
  
  udp.begin(udpPort);
  
  // 3. Setup MPU6050 & Kalman Filters
  Serial.println("\n🔧 MPU6050...");
  delay(100);
  MPU6050_Init();
  delay(50);
  Kalman_Init(&kPitch);
  Kalman_Init(&kRoll);
  // SVM Kalman: lower err_measure = trust measurement more, higher q = faster response
  SimpleKalman_Init(&kSVM, 0.1, 0.1, 0.5);  // Much more responsive
  SimpleKalman_Init(&kalmanGx, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGy, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGz, 0.3, 0.3, 0.1);
  Serial.println("✅ READY");
}

void loop() {
  unsigned long currentMillis = millis();
  static unsigned long lastTime = 0;
  static int errorCount = 0;
  static float last_ax = 0, last_ay = 0, last_az = 1.0; // Default to 1g on Z
  static float last_gx = 0, last_gy = 0, last_gz = 0;

  if (currentMillis - previousMillis >= INTERVAL_MS) {
    previousMillis = currentMillis;

    // --- READ REAL SENSOR DATA ---
    float ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    MPU6050_ReadRaw(ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw);
    
    // Calculate dt for Kalman filter
    double dt = (currentMillis - lastTime) / 1000.0;
    if (lastTime == 0) dt = 0.02;
    lastTime = currentMillis;
    
    // Calculate pitch and roll angles from accelerometer
    double pitch_raw = atan2(-ax_raw, sqrt(ay_raw*ay_raw + az_raw*az_raw)) * 57.2958;
    double roll_raw = atan2(ay_raw, az_raw) * 57.2958;
    
    // Apply Kalman filtering to fuse accelerometer angles with gyroscope rates
    float pitch = Kalman_GetAngle(&kPitch, pitch_raw, gy_raw, dt);
    float roll = Kalman_GetAngle(&kRoll, roll_raw, gx_raw, dt);
    
    // Calculate Signal Vector Magnitude (SVM)
    // SVM = total acceleration magnitude = sqrt(ax^2 + ay^2 + az^2)
    // At rest: SVM ≈ 1g. During motion/impact: SVM changes significantly
    double svm_raw = sqrt(ax_raw*ax_raw + ay_raw*ay_raw + az_raw*az_raw);
    
    // Option 1: Use raw SVM directly (shows absolute acceleration)
    // float svm = svm_raw;
    
    // Option 2: Use deviation from 1g (shows motion intensity)
    // double svm_deviation = fabs(svm_raw - 1.0);
    // float svm = SimpleKalman_Update(&kSVM, svm_deviation);
    
    // Option 3: Light smoothing on raw SVM (best for visualization)
    float svm = SimpleKalman_Update(&kSVM, svm_raw);
    
    // Debug output every 500ms
    static unsigned long lastDebug = 0;
    if (currentMillis - lastDebug > 500) {
      lastDebug = currentMillis;
      Serial.printf("DEBUG: ax=%.2f ay=%.2f az=%.2f | SVM_raw=%.3f SVM_filtered=%.3f\n", 
                    ax_raw, ay_raw, az_raw, svm_raw, svm);
    }
    
    // Smooth gyroscope values
    float gx = SimpleKalman_Update(&kalmanGx, gx_raw);
    float gy = SimpleKalman_Update(&kalmanGy, gy_raw);
    float gz = SimpleKalman_Update(&kalmanGz, gz_raw);
    
    // Battery simulation (will decrease over time)
    if (batLevel > 0) batLevel -= 0.001;

    // --- JSON PACKING ---
    StaticJsonDocument<256> doc;
    doc["ts"] = currentMillis;
    doc["pitch"] = round(pitch * 100) / 100.0;   // Pitch angle (degrees)
    doc["roll"] = round(roll * 100) / 100.0;     // Roll angle (degrees)
    doc["svm"] = round(svm * 100) / 100.0;       // Signal Vector Magnitude (g)
    doc["gx"] = round(gx * 100) / 100.0;         // Gyro X (deg/s)
    doc["gy"] = round(gy * 100) / 100.0;         // Gyro Y (deg/s)
    doc["gz"] = round(gz * 100) / 100.0;         // Gyro Z (deg/s)
    doc["bat"] = (int)batLevel;

    char jsonBuffer[256];
    size_t len = serializeJson(doc, jsonBuffer);

    // --- GỬI & LOG RA SERIAL ---
    
    // 1. Gửi WiFi
    if (WiFi.status() == WL_CONNECTED) {
       int result = udp.beginPacket(udpAddress, udpPort);
       if (result == 1) {
         udp.write((const uint8_t*)jsonBuffer, len);
         int endResult = udp.endPacket();
         
         // Simplified debug output
         if (currentMillis % 1000 < 25) {
           Serial.print("📡 UDP: ");
           Serial.println(jsonBuffer);
         }
       } else {
         Serial.println("❌ UDP beginPacket FAILED!");
       }
    } else {
      // Thử kết nối lại WiFi nếu mất kết nối
      static unsigned long lastReconnect = 0;
      if (currentMillis - lastReconnect > 5000) {
        lastReconnect = currentMillis;
        Serial.print("⚠️ WiFi disconnected (Status: ");
        Serial.print(WiFi.status());
        Serial.println("), attempting reconnect...");
        WiFi.reconnect();
      }
    }

    // 2. Gửi BLE
    if (deviceConnected) {
      pCharacteristic->setValue((uint8_t*)jsonBuffer, len);
      pCharacteristic->notify();
    }
  }
}