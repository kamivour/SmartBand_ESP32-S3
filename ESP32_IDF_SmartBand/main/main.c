#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#include "ble_server.h"
#include "json_utils.h"
#include "kalman_filter.h"
#include "mpu6050.h"
#include "wifi_manager.h"

static const char *TAG = "MAIN";

// Sample rate configuration
#define SAMPLE_RATE_HZ 50
#define INTERVAL_MS (1000 / SAMPLE_RATE_HZ)

// Kalman filter instances
static Kalman_t kPitch, kRoll;                      // For angle fusion
static SimpleKalman_t kSVM;                         // For SVM smoothing
static SimpleKalman_t kalmanGx, kalmanGy, kalmanGz; // For gyro smoothing

// Battery simulation
static float batLevel = 100.0f;

// Sensor reading task
static void sensor_task(void *pvParameters) {
  uint32_t last_time = 0;
  uint32_t last_log_time = 0;
  uint32_t last_debug_time = 0; // For debug output every 500ms

  ESP_LOGI(TAG, "✅ READY\n");

  while (1) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Read MPU6050 sensor data
    float ax_raw, ay_raw, az_raw, gx_raw, gy_raw, gz_raw;
    if (!MPU6050_ReadRaw(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw,
                         &gz_raw)) {
      ESP_LOGE(TAG, "Failed to read MPU6050");
      vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
      continue;
    }

    // Calculate dt for Kalman filter
    double dt = (current_time - last_time) / 1000.0;
    if (last_time == 0)
      dt = 0.02; // First iteration
    last_time = current_time;

    // Calculate pitch and roll angles from accelerometer
    double pitch_raw =
        atan2(-ax_raw, sqrt(ay_raw * ay_raw + az_raw * az_raw)) * 57.2958;
    double roll_raw = atan2(ay_raw, az_raw) * 57.2958;

    // Apply Kalman filtering to fuse accelerometer angles with gyroscope rates
    float pitch = Kalman_GetAngle(&kPitch, pitch_raw, gy_raw, dt);
    float roll = Kalman_GetAngle(&kRoll, roll_raw, gx_raw, dt);

    // Calculate Signal Vector Magnitude (SVM)
    // SVM = total acceleration magnitude = sqrt(ax^2 + ay^2 + az^2)
    // At rest: SVM ≈ 1g. During motion/impact: SVM changes significantly
    double svm_raw = sqrt(ax_raw * ax_raw + ay_raw * ay_raw + az_raw * az_raw);

    // Light smoothing on raw SVM (best for visualization)
    float svm = SimpleKalman_Update(&kSVM, svm_raw);

    // Debug output every 500ms
    if (current_time - last_debug_time > 500) {
      last_debug_time = current_time;
      ESP_LOGI(
          TAG,
          "DEBUG: ax=%.2f ay=%.2f az=%.2f | SVM_raw=%.3f SVM_filtered=%.3f",
          ax_raw, ay_raw, az_raw, svm_raw, svm);
    }

    // Smooth gyroscope values
    float gx = SimpleKalman_Update(&kalmanGx, gx_raw);
    float gy = SimpleKalman_Update(&kalmanGy, gy_raw);
    float gz = SimpleKalman_Update(&kalmanGz, gz_raw);

    // Battery simulation (will decrease over time)
    if (batLevel > 0)
      batLevel -= 0.001f;

    // Create JSON packet
    char json_buffer[JSON_BUFFER_SIZE];
    int json_len =
        JSON_CreateSensorPacket(json_buffer, sizeof(json_buffer), current_time,
                                pitch, roll, svm, gx, gy, gz, (int)batLevel);

    if (json_len < 0) {
      ESP_LOGE(TAG, "Failed to create JSON packet");
      vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
      continue;
    }

    // Send via WiFi UDP
    if (WiFi_IsConnected()) {
      if (UDP_SendBroadcast(json_buffer, json_len)) {
        // Log occasionally (every 1 second)
        if (current_time - last_log_time >= 1000) {
          ESP_LOGI(TAG, "📡 UDP: %s", json_buffer);
          last_log_time = current_time;
        }
      } else {
        ESP_LOGW(TAG, "❌ UDP send failed");
      }
    }

    // Send via BLE
    if (BLE_IsConnected()) {
      BLE_SendNotification((uint8_t *)json_buffer, json_len);
    }

    // Wait for next sample
    vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "\n=== ESP32-S3 Smart Band (ESP-IDF) ===\n");

  // 1. Initialize WiFi
  WiFi_Init();

  // 2. Initialize BLE
  BLE_Init();

  // 3. Initialize MPU6050
  ESP_LOGI(TAG, "\n🔧 MPU6050...");
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!MPU6050_Init()) {
    ESP_LOGE(TAG, "❌ MPU6050 init failed!");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(50));

  // 4. Initialize Kalman filters with tuned parameters (v0.2.2 improvements)
  Kalman_Init(&kPitch);
  Kalman_Init(&kRoll);
  // SVM Kalman: lower err_measure = trust measurement more, higher q = faster
  // response
  SimpleKalman_Init(&kSVM, 0.1, 0.1, 0.5); // Much more responsive
  SimpleKalman_Init(&kalmanGx, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGy, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGz, 0.3, 0.3, 0.1);

  // 5. Create sensor reading task
  xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}