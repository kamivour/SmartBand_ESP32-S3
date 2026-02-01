#ifndef MAX30102_H
#define MAX30102_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// MAX30102 I2C Address
#define MAX30102_ADDR 0x57

// Function prototypes
esp_err_t MAX30102_Init(void);
esp_err_t MAX30102_ReadRawData(uint32_t *red, uint32_t *ir);
esp_err_t MAX30102_ReadData(int *heart_rate, int *spo2);

#endif // MAX30102_H
