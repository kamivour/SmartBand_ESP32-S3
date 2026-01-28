#include "mpu6050.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MPU6050";

bool MPU6050_Init(void) {
    // Configure I2C master
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(err));
        return false;
    }
    
    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return false;
    }
    
    // Wake up MPU6050
    uint8_t data = 0x00;
    err = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, 
                                     (uint8_t[]){MPU6050_PWR_MGMT_1, data}, 2,
                                     1000 / portTICK_PERIOD_MS);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 wake up failed: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "   ✅ MPU6050 initialized");
    return true;
}

bool MPU6050_ReadRaw(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
    uint8_t data[14];
    
    // Read 14 bytes starting from ACCEL_XOUT_H
    esp_err_t err = i2c_master_write_read_device(
        I2C_MASTER_NUM, 
        MPU6050_ADDR,
        (uint8_t[]){MPU6050_ACCEL_XOUT_H}, 
        1,
        data, 
        14,
        1000 / portTICK_PERIOD_MS
    );
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 read failed: %s", esp_err_to_name(err));
        return false;
    }
    
    // Parse raw values (big-endian)
    int16_t ax_raw = (data[0] << 8) | data[1];
    int16_t ay_raw = (data[2] << 8) | data[3];
    int16_t az_raw = (data[4] << 8) | data[5];
    // Skip temperature (data[6], data[7])
    int16_t gx_raw = (data[8] << 8) | data[9];
    int16_t gy_raw = (data[10] << 8) | data[11];
    int16_t gz_raw = (data[12] << 8) | data[13];
    
    // Convert to g (accel) and deg/s (gyro)
    // MPU6050 default: ±2g (16384 LSB/g), ±250°/s (131 LSB/°/s)
    *ax = ax_raw / 16384.0f;
    *ay = ay_raw / 16384.0f;
    *az = az_raw / 16384.0f;
    *gx = gx_raw / 131.0f;
    *gy = gy_raw / 131.0f;
    *gz = gz_raw / 131.0f;
    
    return true;
}
