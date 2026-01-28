#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// BLE UUIDs (same as Arduino version)
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

/**
 * @brief Initialize BLE server
 */
void BLE_Init(void);

/**
 * @brief Check if BLE client is connected
 *
 * @return true if connected, false otherwise
 */
bool BLE_IsConnected(void);

/**
 * @brief Send notification to connected BLE client
 *
 * @param data Data to send
 * @param len Data length
 * @return true if sent successfully, false otherwise
 */
bool BLE_SendNotification(const uint8_t *data, size_t len);

#endif // BLE_SERVER_H
