#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "WiFi";
static bool s_wifi_ready = false;
static esp_netif_t *s_netif = NULL;
static int s_udp_socket = -1;

// Event handler for WiFi AP events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "📱 Client connected: " MACSTR, MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "📱 Client disconnected: " MACSTR, MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "✅ WiFi AP Started | SSID: %s | IP: %s", WIFI_AP_SSID, AP_IP_ADDR);
        s_wifi_ready = true;
    }
}

void WiFi_Init(void) {
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create AP network interface
    s_netif = esp_netif_create_default_wifi_ap();
    
    // Configure static IP for AP
    esp_netif_dhcps_stop(s_netif);
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    ip_info.ip.addr = ipaddr_addr(AP_IP_ADDR);
    ip_info.gw.addr = ipaddr_addr(AP_GATEWAY);
    ip_info.netmask.addr = ipaddr_addr(AP_NETMASK);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_netif));
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &wifi_event_handler, NULL));
    
    // Configure WiFi Access Point
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    // If no password, use open AP
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Starting WiFi AP: SSID=%s, Password=%s", WIFI_AP_SSID, WIFI_AP_PASS);
    
    // Create UDP socket for broadcasting
    s_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return;
    }
    
    // Enable broadcast
    int broadcast_enable = 1;
    setsockopt(s_udp_socket, SOL_SOCKET, SO_BROADCAST, 
               &broadcast_enable, sizeof(broadcast_enable));
}

bool WiFi_IsConnected(void) {
    return s_wifi_ready;
}

void WiFi_GetIPString(char *buffer, size_t buffer_size) {
    if (s_netif && s_wifi_ready) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_netif, &ip_info);
        snprintf(buffer, buffer_size, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(buffer, buffer_size, "AP not started");
    }
}

bool UDP_SendBroadcast(const char *data, size_t len) {
    if (!s_wifi_ready || s_udp_socket < 0) {
        return false;
    }
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(UDP_BROADCAST_ADDR);
    
    int sent = sendto(s_udp_socket, data, len, 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    return (sent == len);
}
