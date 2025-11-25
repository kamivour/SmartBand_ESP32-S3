#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH WIFI ---
// Bạn hãy thay đổi tên và pass wifi của bạn ở đây
const char* ssid = "DnMinh";
const char* password = "mat khau";

// Địa chỉ IP của máy tính chạy App (hoặc tool test)
// QUAN TRỌNG: Cần thay đổi IP này trùng với IP máy tính của bạn (Dùng lệnh ipconfig trên windows để xem)
const char* udpAddress = "192.168.1.170"; 
const int udpPort = 4210;

WiFiUDP udp;

// --- CẤU HÌNH DATA ---
const int SAMPLE_RATE_HZ = 50; // Tần số lấy mẫu 50Hz
const int INTERVAL_MS = 1000 / SAMPLE_RATE_HZ; // 20ms
unsigned long previousMillis = 0;

// Biến giả lập data
float accX, accY, accZ;
float gyroX, gyroY, gyroZ;
float batteryLevel = 100.0;
float t = 0; // Biến thời gian cho hàm sin

void setup() {
  Serial.begin(115200);
  // Đợi một chút để Serial khởi động (chỉ cần thiết với ESP32-S3 native USB)
  delay(2000); 

  Serial.println("--- ESP32-S3 FALL DETECTION SYSTEM ---");

  // 1. Kết nối WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // 2. Khởi động UDP
  udp.begin(udpPort);
}

void loop() {
  unsigned long currentMillis = millis();

  // Cơ chế Throttle: Chỉ chạy mỗi 20ms (50Hz)
  if (currentMillis - previousMillis >= INTERVAL_MS) {
    previousMillis = currentMillis;

    // --- BƯỚC 1: GEN DUMMY DATA ---
    // Giả lập chuyển động lắc tay nhẹ bằng hàm sin
    t += 0.1;
    accX = sin(t) + (random(-10, 10) / 100.0); // Sin wave + noise
    accY = cos(t) * 0.5 + (random(-5, 5) / 100.0);
    accZ = 9.8 + (random(-20, 20) / 100.0); // Trọng lực ~9.8 m/s2

    gyroX = (random(-50, 50) / 100.0);
    gyroY = (random(-50, 50) / 100.0);
    gyroZ = sin(t * 0.5) + (random(-10, 10) / 100.0);
    
    // Giả lập pin tụt dần
    if (batteryLevel > 0) batteryLevel -= 0.0001;

    // --- BƯỚC 2: ĐÓNG GÓI JSON ---
    // Sử dụng StaticJsonDocument để tối ưu bộ nhớ RAM (không dùng Heap)
    StaticJsonDocument<256> doc;
    
    doc["ts"] = currentMillis; // Timestamp
    doc["ax"] = round(accX * 100) / 100.0; // Làm tròn 2 số thập phân
    doc["ay"] = round(accY * 100) / 100.0;
    doc["az"] = round(accZ * 100) / 100.0;
    doc["gx"] = round(gyroX * 100) / 100.0;
    doc["gy"] = round(gyroY * 100) / 100.0;
    doc["gz"] = round(gyroZ * 100) / 100.0;
    doc["bat"] = (int)batteryLevel;

    // Chuyển JSON thành chuỗi String
    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    // --- BƯỚC 3: GỬI UDP ---
    udp.beginPacket(udpAddress, udpPort);
    udp.printf("%s", jsonBuffer); // Gửi chuỗi JSON
    udp.endPacket();

    // In ra Serial để debug (Optional - tắt đi nếu muốn nhanh hơn)
    // Serial.println(jsonBuffer); 
  }
}