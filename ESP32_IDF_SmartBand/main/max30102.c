#include "max30102.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mpu6050.h" // Reuse I2C handle
#include <math.h>
#include <string.h>

static const char *TAG = "MAX30102";

// MAX30102 Register Addresses
#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR 0x04
#define REG_OVERFLOW_CTR 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA 0x0C
#define REG_LED2_PA 0x0D
#define REG_PILOT_PA 0x10
#define REG_MULTI_LED_CTRL1 0x11
#define REG_MULTI_LED_CTRL2 0x12
#define REG_TEMP_INTR 0x1F
#define REG_TEMP_FRAC 0x20
#define REG_TEMP_CONFIG 0x21
#define REG_REV_ID 0xFE
#define REG_PART_ID 0xFF

// Buffer for peak detection
#define BUFFER_SIZE 100
static uint32_t ir_buffer[BUFFER_SIZE];
static uint32_t red_buffer[BUFFER_SIZE];
static int buffer_idx = 0;
static bool buffer_filled = false;

// Simple peak detection state
static uint32_t last_peak_value = 0;
static uint32_t last_peak_time = 0;
static int peak_count = 0;

// Write to MAX30102 register
static bool max30102_write_reg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2] = {reg, value};
  return i2c_master_write_to_device(I2C_NUM_0, MAX30102_ADDR, write_buf, 2,
                                    1000 / portTICK_PERIOD_MS) == ESP_OK;
}

// Read from MAX30102 register
static bool max30102_read_reg(uint8_t reg, uint8_t *data, size_t len) {
  return i2c_master_write_read_device(I2C_NUM_0, MAX30102_ADDR, &reg, 1, data,
                                      len, 1000 / portTICK_PERIOD_MS) == ESP_OK;
}

bool MAX30102_Init(void) {
  ESP_LOGI(TAG, "Initializing MAX30102...");

  // Check Part ID
  uint8_t part_id;
  if (!max30102_read_reg(REG_PART_ID, &part_id, 1)) {
    ESP_LOGE(TAG, "Failed to read Part ID");
    return false;
  }

  if (part_id != 0x15) {
    ESP_LOGE(TAG, "Invalid Part ID: 0x%02X (expected 0x15)", part_id);
    return false;
  }

  // Reset
  max30102_write_reg(REG_MODE_CONFIG, 0x40);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Configure FIFO: Sample averaging = 4, FIFO rollover = enabled
  max30102_write_reg(REG_FIFO_CONFIG, 0x4F);

  // Mode: SpO2 mode (RED and IR LEDs)
  max30102_write_reg(REG_MODE_CONFIG, 0x03);

  // SpO2 Config: 100 samples/sec, 18-bit ADC resolution
  max30102_write_reg(REG_SPO2_CONFIG, 0x27);

  // LED Pulse Amplitude:
  // LED1 (Red) = 0x24 (~7mA)
  // LED2 (IR) = 0x24 (~7mA)
  max30102_write_reg(REG_LED1_PA, 0x24);
  max30102_write_reg(REG_LED2_PA, 0x24);

  // Clear FIFO pointers
  max30102_write_reg(REG_FIFO_WR_PTR, 0x00);
  max30102_write_reg(REG_FIFO_RD_PTR, 0x00);
  max30102_write_reg(REG_OVERFLOW_CTR, 0x00);

  ESP_LOGI(TAG, "✅ MAX30102 initialized");
  return true;
}

// Simple heart rate calculation using peak detection
static int calculate_heart_rate(void) {
  if (!buffer_filled)
    return 0;

  // Find peaks in IR signal (better for HR than red)
  int peaks = 0;
  uint32_t sum_intervals = 0;
  uint32_t last_peak_idx = 0;

  for (int i = 5; i < BUFFER_SIZE - 5; i++) {
    // Check if this is a local maximum
    bool is_peak = true;
    for (int j = -5; j <= 5; j++) {
      if (j != 0 && ir_buffer[i] < ir_buffer[i + j]) {
        is_peak = false;
        break;
      }
    }

    // Also check if peak is above threshold (30% of max value)
    if (is_peak && ir_buffer[i] > 50000) {
      if (peaks > 0) {
        sum_intervals += (i - last_peak_idx);
      }
      last_peak_idx = i;
      peaks++;
    }
  }

  if (peaks < 2)
    return 0;

  // Calculate average interval between peaks
  uint32_t avg_interval = sum_intervals / (peaks - 1);

  // Convert to BPM (assuming 100 samples/sec)
  // BPM = (60 seconds * 100 samples/sec) / samples_per_beat
  int bpm = (60 * 100) / avg_interval;

  // Sanity check: typical heart rate is 40-200 BPM
  if (bpm < 40 || bpm > 200)
    return 0;

  return bpm;
}

// Simple SpO2 calculation using R-value
static int calculate_spo2(void) {
  if (!buffer_filled)
    return 0;

  // Calculate AC and DC components for RED and IR
  uint32_t red_ac = 0, red_dc = 0;
  uint32_t ir_ac = 0, ir_dc = 0;

  // Find min/max for AC calculation
  uint32_t red_min = UINT32_MAX, red_max = 0;
  uint32_t ir_min = UINT32_MAX, ir_max = 0;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    if (red_buffer[i] < red_min)
      red_min = red_buffer[i];
    if (red_buffer[i] > red_max)
      red_max = red_buffer[i];
    if (ir_buffer[i] < ir_min)
      ir_min = ir_buffer[i];
    if (ir_buffer[i] > ir_max)
      ir_max = ir_buffer[i];

    red_dc += red_buffer[i];
    ir_dc += ir_buffer[i];
  }

  red_ac = red_max - red_min;
  ir_ac = ir_max - ir_min;
  red_dc /= BUFFER_SIZE;
  ir_dc /= BUFFER_SIZE;

  // Avoid division by zero
  if (ir_dc == 0 || ir_ac == 0)
    return 0;

  // Calculate R-value: (AC_red / DC_red) / (AC_ir / DC_ir)
  float r = ((float)red_ac / red_dc) / ((float)ir_ac / ir_dc);

  // SpO2 estimation using empirical formula: SpO2 = 110 - 25*R
  int spo2 = (int)(110.0 - 25.0 * r);

  // Sanity check: typical SpO2 is 70-100%
  if (spo2 < 70 || spo2 > 100)
    return 0;

  return spo2;
}

bool MAX30102_ReadData(int *heart_rate, int *spo2) {
  // Read number of available samples
  uint8_t wr_ptr, rd_ptr;
  max30102_read_reg(REG_FIFO_WR_PTR, &wr_ptr, 1);
  max30102_read_reg(REG_FIFO_RD_PTR, &rd_ptr, 1);

  int num_available = (wr_ptr - rd_ptr) & 0x1F; // 32 sample FIFO

  if (num_available > 0) {
    // Read one sample (6 bytes: 3 bytes RED + 3 bytes IR)
    uint8_t fifo_data[6];
    if (!max30102_read_reg(REG_FIFO_DATA, fifo_data, 6)) {
      return false;
    }

    // Parse RED and IR values (18-bit)
    uint32_t red = ((uint32_t)fifo_data[0] << 16) |
                   ((uint32_t)fifo_data[1] << 8) | fifo_data[2];
    uint32_t ir = ((uint32_t)fifo_data[3] << 16) |
                  ((uint32_t)fifo_data[4] << 8) | fifo_data[5];

    // Mask to 18 bits
    red &= 0x3FFFF;
    ir &= 0x3FFFF;

    // Store in circular buffer
    red_buffer[buffer_idx] = red;
    ir_buffer[buffer_idx] = ir;
    buffer_idx = (buffer_idx + 1) % BUFFER_SIZE;

    if (buffer_idx == 0) {
      buffer_filled = true;
    }
  }

  // Calculate HR and SpO2 if buffer is filled
  if (buffer_filled) {
    *heart_rate = calculate_heart_rate();
    *spo2 = calculate_spo2();
  } else {
    *heart_rate = 0;
    *spo2 = 0;
  }

  return true;
}
