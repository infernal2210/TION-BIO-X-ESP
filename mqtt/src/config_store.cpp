#include "device_config.h"

#include <EEPROM.h>
#include <stddef.h>
#include <string.h>

#include "app_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
constexpr uint32_t kMagic = 0x54494F4E;  // "TION"
constexpr uint8_t kVersion = 1;
constexpr size_t kEepromSize = 512;

uint32_t checksum(const DeviceConfig &config) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&config);
  uint32_t value = 0xFFFFFFFFUL;
  for (size_t i = 0; i < offsetof(DeviceConfig, checksum); ++i) {
    value ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      value = (value >> 1) ^ (0xEDB88320UL & (0UL - (value & 1UL)));
    }
  }
  return ~value;
}

bool terminated(const char *value, size_t size) {
  return memchr(value, '\\0', size) != nullptr;
}

bool valid(const DeviceConfig &config) {
  return config.magic == kMagic &&
         config.version == kVersion &&
         config.mqtt_port > 0 &&
         terminated(config.wifi_ssid, sizeof(config.wifi_ssid)) &&
         terminated(config.wifi_password, sizeof(config.wifi_password)) &&
         terminated(config.mqtt_host, sizeof(config.mqtt_host)) &&
         terminated(config.mqtt_username, sizeof(config.mqtt_username)) &&
         terminated(config.mqtt_password, sizeof(config.mqtt_password)) &&
         config.checksum == checksum(config);
}
}  // namespace

namespace ConfigStore {
void setDefaults(DeviceConfig &config) {
  memset(&config, 0, sizeof(config));
  config.magic = kMagic;
  config.version = kVersion;
  strlcpy(config.wifi_ssid, TION_WIFI_SSID, sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password, TION_WIFI_PASSWORD, sizeof(config.wifi_password));
  strlcpy(config.mqtt_host, TION_MQTT_DEFAULT_HOST, sizeof(config.mqtt_host));
  config.mqtt_port = TION_MQTT_DEFAULT_PORT;
  strlcpy(config.mqtt_username, TION_MQTT_USERNAME, sizeof(config.mqtt_username));
  strlcpy(config.mqtt_password, TION_MQTT_PASSWORD, sizeof(config.mqtt_password));
  config.checksum = checksum(config);
}

bool load(DeviceConfig &config) {
  if (!EEPROM.begin(kEepromSize)) return false;
  EEPROM.get(0, config);
  if (valid(config)) return true;
  setDefaults(config);
  return false;
}

bool save(const DeviceConfig &config) {
  if (!EEPROM.begin(kEepromSize)) return false;
  DeviceConfig stored = config;
  stored.magic = kMagic;
  stored.version = kVersion;
  stored.checksum = checksum(stored);
  EEPROM.put(0, stored);
  return EEPROM.commit();
}
}  // namespace ConfigStore
