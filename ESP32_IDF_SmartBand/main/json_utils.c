#include "json_utils.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

// Helper function to round to 2 decimal places
static inline float round_2dp(float value) {
  return roundf(value * 100.0f) / 100.0f;
}

int JSON_CreateSensorPacket(char *buffer, size_t buffer_size,
                            uint32_t timestamp, float pitch, float roll,
                            float svm, float gx, float gy, float gz,
                            int heart_rate, int spo2, int battery) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return -1;
  }

  // Add all fields (matching Arduino version + MAX30102 data)
  cJSON_AddNumberToObject(root, "ts", timestamp);
  cJSON_AddNumberToObject(root, "pitch", round_2dp(pitch));
  cJSON_AddNumberToObject(root, "roll", round_2dp(roll));
  cJSON_AddNumberToObject(root, "svm", round_2dp(svm));
  cJSON_AddNumberToObject(root, "gx", round_2dp(gx));
  cJSON_AddNumberToObject(root, "gy", round_2dp(gy));
  cJSON_AddNumberToObject(root, "gz", round_2dp(gz));
  cJSON_AddNumberToObject(root, "hr", heart_rate); // Heart rate (BPM)
  cJSON_AddNumberToObject(root, "spo2", spo2);     // Blood oxygen (%)
  cJSON_AddNumberToObject(root, "bat", battery);

  // Print to buffer (unformatted/compact)
  char *json_str = cJSON_PrintUnformatted(root);
  if (json_str == NULL) {
    cJSON_Delete(root);
    return -1;
  }

  int len = strlen(json_str);
  if (len >= buffer_size) {
    cJSON_free(json_str);
    cJSON_Delete(root);
    return -1;
  }

  strcpy(buffer, json_str);

  cJSON_free(json_str);
  cJSON_Delete(root);

  return len;
}
