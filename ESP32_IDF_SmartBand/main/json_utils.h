#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stddef.h>
#include <stdint.h>

#define JSON_BUFFER_SIZE 256

/**
 * Create JSON sensor data packet
 * Format:
 * {"ts":12345,"pitch":-5.23,"roll":2.14,"svm":1.02,"gx":0.12,"gy":-0.05,"gz":0.08,"hr":72,"spo2":98,"bat":98}
 *
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @param timestamp Current timestamp (milliseconds)
 * @param pitch Pitch angle (degrees)
 * @param roll Roll angle (degrees)
 * @param svm Signal vector magnitude (g)
 * @param gx Gyroscope X (deg/s)
 * @param gy Gyroscope Y (deg/s)
 * @param gz Gyroscope Z (deg/s)
 * @param heart_rate Heart rate (BPM)
 * @param spo2 Blood oxygen saturation (%)
 * @param battery Battery level (0-100%)
 * @return Length of JSON string, or -1 on error
 */
int JSON_CreateSensorPacket(char *buffer, size_t buffer_size,
                            uint32_t timestamp, float pitch, float roll,
                            float svm, float gx, float gy, float gz,
                            int heart_rate, int spo2, int battery);

#endif // JSON_UTILS_H
