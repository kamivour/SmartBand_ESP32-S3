#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>

// I2C Configuration
#define I2C_MASTER_SDA_IO       8
#define I2C_MASTER_SCL_IO       9
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_NUM          I2C_NUM_0
#define MPU6050_ADDR            0x68

// MPU6050 Register Addresses
#define MPU6050_PWR_MGMT_1      0x6B
#define MPU6050_ACCEL_XOUT_H    0x3B

/**
 * @brief Initialize MPU6050 sensor
 * 
 * @return true if initialization successful, false otherwise
 */
bool MPU6050_Init(void);

/**
 * @brief Read raw sensor data from MPU6050
 * 
 * @param ax Pointer to store accelerometer X (g)
 * @param ay Pointer to store accelerometer Y (g)
 * @param az Pointer to store accelerometer Z (g)
 * @param gx Pointer to store gyroscope X (deg/s)
 * @param gy Pointer to store gyroscope Y (deg/s)
 * @param gz Pointer to store gyroscope Z (deg/s)
 * @return true if read successful, false otherwise
 */
bool MPU6050_ReadRaw(float *ax, float *ay, float *az, float *gx, float *gy, float *gz);

#endif // MPU6050_H
