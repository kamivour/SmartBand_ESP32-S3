#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stddef.h>
#include <stdint.h>


#define JSON_BUFFER_SIZE 256

/**
 * @brief Serialize sensor data to JSON string
 *
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @param timestamp Current timestamp (ms)
 * @param pitch Pitch angle (degrees)
 * @param roll Roll angle (degrees)
 * @param svm Signal vector magnitude (g)
 * @param gx Gyro X (deg/s)
 * @param gy Gyro Y (deg/s)
 * @param gz Gyro Z (deg/s)
 * @param battery Battery level (%)
 * @return Length of JSON string, or -1 on error
 */
int JSON_CreateSensorPacket(char *buffer, size_t buffer_size,
                            uint32_t timestamp, float pitch, float roll,
                            float svm, float gx, float gy, float gz,
                            int battery);

#endif // JSON_UTILS_H
