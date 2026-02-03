#include "ble_server.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>


static const char *TAG = "BLE";
static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0;
static uint16_t s_characteristic_handle;

// Forward declaration
static void ble_advertise(void);

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  ESP_LOGI(TAG, "BLE GAP event: type=%d", event->type);
  
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    ESP_LOGI(TAG, "BLE_GAP_EVENT_CONNECT: status=%d", event->connect.status);
    if (event->connect.status == 0) {
      s_ble_connected = true;
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, ">>> BLE CLIENT CONNECTED <<< conn_handle=%d", s_conn_handle);
    } else {
      ESP_LOGE(TAG, "BLE connection failed; status=%d", event->connect.status);
      ble_advertise();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "BLE_GAP_EVENT_DISCONNECT: reason=%d", event->disconnect.reason);
    s_ble_connected = false;
    ESP_LOGI(TAG, ">>> BLE CLIENT DISCONNECTED <<<");
    // Restart advertising
    ble_advertise();
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG, "BLE_GAP_EVENT_SUBSCRIBE: cur_notify=%d, value_handle=%d",
             event->subscribe.cur_notify, event->subscribe.attr_handle);
    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "BLE_GAP_EVENT_MTU: mtu=%d", event->mtu.value);
    break;

  case BLE_GAP_EVENT_CONN_UPDATE:
    ESP_LOGI(TAG, "BLE_GAP_EVENT_CONN_UPDATE: status=%d", event->conn_update.status);
    break;

  default:
    ESP_LOGI(TAG, "BLE GAP event type=%d", event->type);
    break;
  }
  return 0;
}

// Characteristic access callback
static int characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  ESP_LOGI(TAG, "Characteristic access: op=%d", ctxt->op);
  
  // Handle READ request
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    ESP_LOGI(TAG, "BLE characteristic read");
    // Return empty data for reads
    return 0;
  }
  
  // Handle WRITE request
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    ESP_LOGI(TAG, "BLE characteristic write");
    return 0;
  }
  
  return BLE_ATT_ERR_UNLIKELY;
}

// Start advertising
static void ble_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields = {0};
  struct ble_hs_adv_fields rsp_fields = {0};
  int rc;

  // Service UUID to advertise
  static ble_uuid128_t service_uuid = 
    BLE_UUID128_INIT(0x4b, 0x91, 0x33, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

  // Set device name
  const char *device_name = "ESP32 SmartBand";
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  // Set flags
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set advertising fields; rc=%d", rc);
    return;
  }

  // Add service UUID to scan response data
  rsp_fields.uuids128 = (ble_uuid128_t[]){service_uuid};
  rsp_fields.num_uuids128 = 1;
  rsp_fields.uuids128_is_complete = 1;
  
  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc != 0) {
    ESP_LOGW(TAG, "Failed to set scan response fields; rc=%d", rc);
  }

  // Configure advertising parameters
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Undirected connectable
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // General discoverable
  adv_params.itvl_min = 0x0020;  // 20ms (units of 0.625ms)
  adv_params.itvl_max = 0x0040;  // 40ms (units of 0.625ms)

  // Start advertising
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                         ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to start advertising; rc=%d", rc);
  } else {
    ESP_LOGI(TAG, "BLE advertising started with service UUID");
  }
}

// NimBLE host task
static void ble_host_task(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

// When NimBLE host is synced
static void ble_on_sync(void) {
  ble_advertise();
  ESP_LOGI(TAG, "✅ BLE OK");
}

// GATT service definition
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid =
            BLE_UUID128_DECLARE(0x4b, 0x91, 0x33, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                                0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = BLE_UUID128_DECLARE(
                        0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88,
                        0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe),
                    .access_cb = characteristic_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_characteristic_handle,
                },
                {0} // End of characteristics
            },
    },
    {0} // End of services
};

void BLE_Init(void) {
  ESP_LOGI(TAG, "\n📱 BLE Setup...");
  esp_err_t ret;

  // Release BT controller memory for classic BT (NimBLE doesn't need it)
  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "BT controller mem release failed: %s", esp_err_to_name(ret));
  }

  // Initialize NimBLE
  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "❌ NimBLE port init failed: %s", esp_err_to_name(ret));
    return;
  }
  ESP_LOGI(TAG, "NimBLE port initialized");

  // Configure GAP device name
  ret = ble_svc_gap_device_name_set("ESP32 SmartBand");
  if (ret != 0) {
    ESP_LOGE(TAG, "Failed to set device name; rc=%d", ret);
  }

  // Configure GATT
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ESP_LOGI(TAG, "GAP and GATT services initialized");

  // Register GATT services
  ret = ble_gatts_count_cfg(gatt_svcs);
  if (ret != 0) {
    ESP_LOGE(TAG, "Failed to count GATT services; rc=%d", ret);
  }
  
  ret = ble_gatts_add_svcs(gatt_svcs);
  if (ret != 0) {
    ESP_LOGE(TAG, "Failed to add GATT services; rc=%d", ret);
  } else {
    ESP_LOGI(TAG, "GATT services registered");
  }

  // Set sync callback
  ble_hs_cfg.sync_cb = ble_on_sync;

  // Start NimBLE host task
  nimble_port_freertos_init(ble_host_task);
  ESP_LOGI(TAG, "NimBLE host task started");
}

bool BLE_IsConnected(void) { return s_ble_connected; }

bool BLE_SendNotification(const uint8_t *data, size_t len) {
  if (!s_ble_connected) {
    return false;
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
  if (om == NULL) {
    return false;
  }

  int rc = ble_gattc_notify_custom(s_conn_handle, s_characteristic_handle, om);
  return (rc == 0);
}
