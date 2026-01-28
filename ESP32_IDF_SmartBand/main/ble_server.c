#include "ble_server.h"
#include "esp_log.h"
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

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      s_ble_connected = true;
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, ">>> BLE CLIENT CONNECTED <<<");
    } else {
      ESP_LOGE(TAG, "BLE connection failed; status=%d", event->connect.status);
      ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                        &(struct ble_gap_adv_params){0}, NULL, NULL);
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    s_ble_connected = false;
    ESP_LOGI(TAG, ">>> BLE CLIENT DISCONNECTED <<<");
    // Restart advertising
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &(struct ble_gap_adv_params){0}, NULL, NULL);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG, "BLE subscribe event; cur_notify=%d",
             event->subscribe.cur_notify);
    break;
  }
  return 0;
}

// Characteristic access callback
static int characteristic_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  // Handle READ request
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    ESP_LOGI(TAG, "BLE characteristic read");
    // Return empty data for reads
    return 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

// Start advertising
static void ble_advertise(void) {
  struct ble_gap_adv_params adv_params = {0};
  struct ble_hs_adv_fields fields = {0};

  // Set device name
  const char *device_name = "ESP32 SmartBand";
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  // Set flags
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  ble_gap_adv_set_fields(&fields);

  // Start advertising
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                    ble_gap_event, NULL);
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

  // Initialize NimBLE
  ESP_ERROR_CHECK(nimble_port_init());

  // Configure GAP device name
  ble_svc_gap_device_name_set("ESP32 SmartBand");

  // Configure GATT
  ble_svc_gap_init();
  ble_svc_gatt_init();

  // Register GATT services
  ble_gatts_count_cfg(gatt_svcs);
  ble_gatts_add_svcs(gatt_svcs);

  // Set sync callback
  ble_hs_cfg.sync_cb = ble_on_sync;

  // Start NimBLE host task
  nimble_port_freertos_init(ble_host_task);
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
