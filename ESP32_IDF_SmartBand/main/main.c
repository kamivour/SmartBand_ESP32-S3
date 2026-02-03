#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#include "ble_server.h"
#include "json_utils.h"
#include "kalman_filter.h"
#include "max30102.h"
#include "mpu6050.h"
#include "wifi_manager.h"

// AI Model Includes
#include "inference.h"
#include "model_config.h"

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

// =================================================================================
// AI MODEL BUFFERS & HELPERS
// =================================================================================

// Sliding Window Buffer
// We need to store MODEL_WINDOW_SIZE samples (100)
// Each sample has 6 features (Pitch, Roll, SVM, Gx, Gy, Gz)
static float window_buffer[MODEL_WINDOW_SIZE * MODEL_NUM_FEATURES];
// Circular buffer index
static int window_head = 0;
static int samples_collected = 0;

// Trigger thresholds (Match deploy.c)
#define SVM_THRESHOLD_HIGH 2.0f
#define SVM_THRESHOLD_LOW 0.6f
// 1 second @ 50Hz = 50 samples
#define SAMPLES_TO_WAIT_AFTER_TRIGGER 50

static void add_to_window(float *features) {
  // Copy 6 features to the current head position
  for (int i = 0; i < MODEL_NUM_FEATURES; i++) {
    window_buffer[window_head * MODEL_NUM_FEATURES + i] = features[i];
  }

  // Advance head, wrap around
  window_head++;
  if (window_head >= MODEL_WINDOW_SIZE) {
    window_head = 0;
  }

  if (samples_collected < MODEL_WINDOW_SIZE) {
    samples_collected++;
  }
}

// Prepare linear buffer for inference (unroll circular buffer)
static void get_window_data(float *dest) {
  if (samples_collected < MODEL_WINDOW_SIZE)
    return;

  // Head points to the *next* write position, which is effectively the *oldest*
  // data point in a full buffer. So we read from window_head to end, then from
  // 0 to window_head
  int idx = 0;

  // Part 1: Oldest -> End of array
  for (int i = window_head; i < MODEL_WINDOW_SIZE; i++) {
    for (int j = 0; j < MODEL_NUM_FEATURES; j++) {
      dest[idx++] = window_buffer[i * MODEL_NUM_FEATURES + j];
    }
  }

  // Part 2: Start of array -> Newest
  for (int i = 0; i < window_head; i++) {
    for (int j = 0; j < MODEL_NUM_FEATURES; j++) {
      dest[idx++] = window_buffer[i * MODEL_NUM_FEATURES + j];
    }
  }
}

// =================================================================================
// SENSOR TASK
// =================================================================================

static void sensor_task(void *pvParameters) {
  uint32_t last_time = 0;
  uint32_t last_log_time = 0;

  // AI State Machine variables
  int samples_after_trigger = -1; // -1: Idle, >0: Counting down
  int cooldown_counter = 0;       // prevent double triggering immediately
  static float inference_input[MODEL_WINDOW_SIZE * MODEL_NUM_FEATURES];
  float features[6];
  int fall_status = 0; // 0: Normal, 1: Fall Detected

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

    // ---------------------------------------------------------
    // 1. Data Processing for UI/Graph (Existing Logic)
    // ---------------------------------------------------------

    // Calculate pitch and roll angles from accelerometer
    double pitch_raw =
        atan2(-ax_raw, sqrt(ay_raw * ay_raw + az_raw * az_raw)) * 57.2958;
    double roll_raw = atan2(ay_raw, az_raw) * 57.2958;

    // Apply Kalman filtering (for nice UI graph)
    float pitch_filtered = Kalman_GetAngle(&kPitch, pitch_raw, gy_raw, dt);
    float roll_filtered = Kalman_GetAngle(&kRoll, roll_raw, gx_raw, dt);

    // SVM Calculation
    double svm_raw = sqrt(ax_raw * ax_raw + ay_raw * ay_raw + az_raw * az_raw);
    float svm_filtered = SimpleKalman_Update(&kSVM, svm_raw); // For UI

    // Smooth gyroscope values (for UI)
    float gx_filtered = SimpleKalman_Update(&kalmanGx, gx_raw);
    float gy_filtered = SimpleKalman_Update(&kalmanGy, gy_raw);
    float gz_filtered = SimpleKalman_Update(&kalmanGz, gz_raw);

    // ---------------------------------------------------------
    // 2. Data Processing for AI Model (Model Specific)
    // ---------------------------------------------------------
    // The model expects pitch/roll calculated exactly as in training (deploy.c)
    // Pitch: atan2(ay, sqrt(ax*ax + az*az)) * 57.29...
    // Roll:  atan2(-ax, az) * 57.29...
    // Note: deploy.c uses 'ay' as primary for pitch? Let's match it exactly.
    // deploy.c: float pitch = atan2f(raw.ay, sqrtf(raw.ax * raw.ax + raw.az *
    // raw.az)) * 57.2957795f; deploy.c: float roll = atan2f(-raw.ax, raw.az)
    // * 57.2957795f;

    float model_pitch =
        atan2f(ay_raw, sqrtf(ax_raw * ax_raw + az_raw * az_raw)) * 57.2957795f;
    float model_roll = atan2f(-ax_raw, az_raw) * 57.2957795f;
    float model_svm = (float)svm_raw; // Model checks raw SVM thresholds

    // Prepare feature vector
    features[0] = model_pitch;
    features[1] = model_roll;
    features[2] = model_svm;
    features[3] = gx_raw; // Model uses raw gyro
    features[4] = gy_raw;
    features[5] = gz_raw;

    // Add to circular buffer
    add_to_window(features);

    // ---------------------------------------------------------
    // 3. Fall Detection Logic (State Machine)
    // ---------------------------------------------------------

    // Reset fall status after being high for one packet (pulse)
    // or keep it for a while? Let's pulse it or hold it?
    // Usually for alerts, sending it once or a few times is good.
    // Here we reset it to 0 at start of loop? No, we set it when detected.
    // Let's rely on the state machine.

    if (fall_status == 1) {
      // Auto-reset fall status after 1 frame so we don't spam "Fall"
      // continuously? Or keep it 1 for a few seconds? For now, let's keep it 1
      // only for the frame it was detected.
      fall_status = 0;
    }

    if (samples_after_trigger == -1 && cooldown_counter == 0) {
      // IDLE STATE: Check for trigger
      if (model_svm > SVM_THRESHOLD_HIGH || model_svm < SVM_THRESHOLD_LOW) {
        ESP_LOGW(TAG,
                 "⚠️ Shock Detected (SVM=%.2f)! Collecting post-event data...",
                 model_svm);
        samples_after_trigger = SAMPLES_TO_WAIT_AFTER_TRIGGER;
      }
    } else if (samples_after_trigger > 0) {
      // CAPTURE STATE: Decrement counter
      samples_after_trigger--;
    } else if (samples_after_trigger == 0) {
      // PROCESS STATE: Ready to infer
      ESP_LOGI(TAG, "🧠 Running Inference...");

      get_window_data(inference_input);
      preprocess_input(inference_input, MODEL_WINDOW_SIZE * MODEL_NUM_FEATURES);

      float conf = 0;
      int pred = model_predict(inference_input, &conf);

      if (pred == 1) {
        ESP_LOGE(TAG, ">>> 🚑 FALL DETECTED! (Conf: %.2f) <<<", conf);
        fall_status = 1; // Trigger alert in JSON
      } else {
        ESP_LOGI(TAG, ">>> Normal Activity (Conf: %.2f) <<<", conf);
      }

      samples_after_trigger = -1; // Reset to IDLE
      cooldown_counter = 50;      // Wait 1s before next trigger
    }

    if (cooldown_counter > 0) {
      cooldown_counter--;
    }

    // ---------------------------------------------------------
    // 4. MAX30102 & Battery
    // ---------------------------------------------------------
    int heart_rate = 0, spo2 = 0;
    MAX30102_ReadData(&heart_rate, &spo2);

    if (batLevel > 0)
      batLevel -= 0.001f;

    // ---------------------------------------------------------
    // 5. Send Data
    // ---------------------------------------------------------

    // Create JSON packet with FALL STATUS
    char json_buffer[JSON_BUFFER_SIZE];
    int json_len = JSON_CreateSensorPacket(
        json_buffer, sizeof(json_buffer), current_time, pitch_filtered,
        roll_filtered, svm_filtered, // Us filtered vars for UI
        gx_filtered, gy_filtered, gz_filtered, heart_rate, spo2, (int)batLevel,
        fall_status);

    if (json_len > 0) {
      // UDP
      if (WiFi_IsConnected()) {
        UDP_SendBroadcast(json_buffer, json_len);
        // Log occasionally
        if (current_time - last_log_time >= 1000) {
          // ESP_LOGI(TAG, "%s", json_buffer); // Optional verbose log
          last_log_time = current_time;
        }
      }

      // BLE
      if (BLE_IsConnected()) {
        BLE_SendNotification((uint8_t *)json_buffer, json_len);
      }
    }

    // Wait for next sample (20ms target)
    vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "\n=== ESP32-S3 Smart Band (AI Enabled) ===\n");

  // 1. Initialize WiFi & BLE
  WiFi_Init();
  BLE_Init();

  // 2. Initialize MPU6050
  ESP_LOGI(TAG, "\n🔧 MPU6050...");
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!MPU6050_Init()) {
    ESP_LOGE(TAG, "❌ MPU6050 init failed!");
    return;
  }

  // 3. Initialize MAX30102
  ESP_LOGI(TAG, "🔧 MAX30102...");
  MAX30102_Init();

  // 4. Initialize AI Model
  ESP_LOGI(TAG, "🧠 AI Model...");
  model_init();

  // 5. Initialize Kalman filters
  Kalman_Init(&kPitch);
  Kalman_Init(&kRoll);
  SimpleKalman_Init(&kSVM, 0.1, 0.1, 0.5);
  SimpleKalman_Init(&kalmanGx, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGy, 0.3, 0.3, 0.1);
  SimpleKalman_Init(&kalmanGz, 0.3, 0.3, 0.1);

  // 6. Start Task
  xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5,
              NULL); // Increased stack size for AI buffers
}