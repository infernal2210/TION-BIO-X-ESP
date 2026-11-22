#pragma once

#include <Arduino.h>

struct DeviceConfig {
  uint32_t magic;
  uint8_t version;
  char wifi_ssid[33];
  char wifi_password[65];
  char mqtt_host[64];
  uint16_t mqtt_port;
  char mqtt_username[33];
  char mqtt_password[65];
  uint32_t checksum;
};

namespace ConfigStore {
  bool load(DeviceConfig &config);
  bool save(const DeviceConfig &config);
  void setDefaults(DeviceConfig &config);
}
