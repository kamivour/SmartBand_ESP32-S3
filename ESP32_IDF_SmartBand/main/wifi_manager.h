#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// WiFi Access Point Configuration
#define WIFI_AP_SSID "ESP32_SmartBand"
#define WIFI_AP_PASS "smartband123"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4

// AP Network Configuration
#define AP_IP_ADDR "192.168.4.1"
#define AP_GATEWAY "192.168.4.1"
#define AP_NETMASK "255.255.255.0"

// UDP Configuration
#define UDP_BROADCAST_ADDR "192.168.4.255"  // AP subnet broadcast
#define UDP_PORT 4210

/**
 * @brief Initialize WiFi and connect to AP
 */
void WiFi_Init(void);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool WiFi_IsConnected(void);

/**
 * @brief Get WiFi IP address as string
 *
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 */
void WiFi_GetIPString(char *buffer, size_t buffer_size);

/**
 * @brief Send UDP broadcast packet
 *
 * @param data Data to send
 * @param len Data length
 * @return true if sent successfully, false otherwise
 */
bool UDP_SendBroadcast(const char *data, size_t len);

#endif // WIFI_MANAGER_H
