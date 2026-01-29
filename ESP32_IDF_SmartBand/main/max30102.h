#ifndef MAX30102_H
#define MAX30102_H

#include <stdbool.h>
#include <stdint.h>

// MAX30102 I2C Address
#define MAX30102_ADDR 0x57

// Function prototypes
bool MAX30102_Init(void);
bool MAX30102_ReadData(int *heart_rate, int *spo2);

#endif // MAX30102_H
