#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>

#include "app_config.h"
#include "device_config.h"
#include "tuya_uart.h"
#include "tuya_dp.h"
#include "tion_model.h"

// ESP-01 hardware UART uses GPIO1 (TX) and GPIO3 (RX).
// Never print debug messages to Serial: it is connected to the TION UART.

namespace {
TuyaUart tion_uart(Serial);
TionModel tion_model;
DeviceConfig device_config = {};
WiFiClient wifi_client;
PubSubClient mqtt(wifi_client);
ESP8266WebServer web(80);
DNSServer dns;

constexpr uint32_t kMqttRetryMs = 5000;
constexpr uint32_t kWifiAttemptTimeoutMs = 15000;
constexpr uint32_t kWifiRetryAfterPortalMs = 30000;
constexpr uint8_t kMaxWifiAttempts = 5;
constexpr uint8_t kDnsPort = 53;
constexpr char kSetupApPassword[] = "tionbiox01";

uint32_t last_mqtt_attempt = 0;
uint32_t last_diagnostics_publish = 0;
uint32_t wifi_attempt_started = 0;
uint32_t last_wifi_retry = 0;
uint8_t wifi_attempts = 0;
bool discovery_sent = false;
bool ap_active = false;

String baseTopic() {
  return String(TION_MQTT_PREFIX) + "/" + TION_DEVICE_ID + "/";
}

String stateTopic(uint8_t id) {
  return baseTopic() + "state/dp_" + String(id);
}

String htmlEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char ch = input[i];
    if (ch == '&') output += F("&amp;");
    else if (ch == '<') output += F("&lt;");
    else if (ch == '>') output += F("&gt;");
    else if (ch == '"') output += F("&quot;");
    else if (ch == '\'') output += F("&#39;");
    else output += ch;
  }
  return output;
}

struct DatapointInfo {
  const char *name;
  const char *domain;
  const char *unit;
  const char *device_class;
  const char *state_class;
  const char *icon;
};

const char *entityCategory(uint8_t id) {
  // Home Assistant supports only the built-in config and diagnostic categories.
  // Control entities stay uncategorized (the normal controls section).
  switch (id) {
    case 103: case 104: case 109: case 110: case 111: case 118: case 128:
    case 129: case 130: case 132: case 133: case 134: case 135: case 136:
      return "config";
    case 106: case 108: case 112: case 113: case 115: case 116:
    case 122: case 123: case 124: case 127:
      return "diagnostic";
    default: return nullptr;
  }
}

const DatapointInfo *datapointInfo(uint8_t id) {
  // Command-only reset datapoints are represented as buttons, not state sensors.
  if (id == 109 || id == 110) return nullptr;
  // These datapoints were explicitly excluded from the published entity set.
  if (id == 117 || id == 120 || id == 125 || id == 126) return nullptr;
  static const DatapointInfo errorInfo =
    { "Ошибка устройства", "sensor", "", "", "", "mdi:alert-circle" };
  if (id == 127) return &errorInfo;
  static const DatapointInfo values[] = {
    { "Питание", "binary_sensor", "", "", "", "mdi:power" },
    { "Скорость вентиляции", "sensor", "", "", "measurement", "mdi:fan" },
    { "Звук", "switch", "", "", "", "mdi:volume-high" },
    { "Подсветка", "select", "", "", "", "mdi:brightness-6" },
    { "Подогрев", "binary_sensor", "", "", "", "mdi:heat-wave" },
    { "Режим вентиляции", "switch", "", "", "", "mdi:air-filter" },
    { "Внутренняя температура", "sensor", "°C", "temperature", "measurement", "mdi:thermometer" },
    { "Сброс заводских настроек", "binary_sensor", "", "", "", "mdi:restart-alert" },
    { "Сброс счётчика фильтра", "binary_sensor", "", "", "", "mdi:air-filter" },
    { "Детский замок", "switch", "", "", "", "mdi:lock" },
    { "Внешняя температура", "sensor", "°C", "temperature", "measurement", "mdi:thermometer" },
    { "Время работы", "sensor", "s", "duration", "total_increasing", "mdi:timer-outline" },
    { "Заданная температура подогрева", "sensor", "°C", "temperature", "measurement", "mdi:thermostat" },
    { "Ресурс фильтра", "sensor", "%", "", "measurement", "mdi:air-filter" },
    { "Уровень CO₂", "sensor", "ppm", "carbon_dioxide", "measurement", "mdi:molecule-co2" },
    { "PM2.5", "sensor", "µg/m³", "pm25", "measurement", "mdi:blur" },
    { "Состояние обогревателя", "binary_sensor", "", "", "", "mdi:radiator" },
    { "Мощность обогревателя", "sensor", "", "", "measurement", "mdi:heat-wave" },
    { "Влажность", "sensor", "%", "humidity", "measurement", "mdi:water-percent" },
    { "Нагрузка нагревателя", "sensor", "%", "", "measurement", "mdi:heating-coil" },
    { "Состояние датчика CO₂", "sensor", "", "", "", "mdi:molecule-co2" },
    { "Состояние датчика PM2.5", "sensor", "", "", "", "mdi:blur" },
    { "Состояние датчика температуры и влажности", "sensor", "", "", "", "mdi:thermometer-water" },
    { "Скорость голосовых подсказок", "sensor", "", "", "measurement", "mdi:account-voice" },
    { "Комнатная температура", "sensor", "°C", "temperature", "measurement", "mdi:thermometer" },
    { "Максимальная автоскорость", "sensor", "", "", "measurement", "mdi:fan-auto" },
    { "Минимальная автоскорость", "sensor", "", "", "measurement", "mdi:fan-auto" },
    { "Целевой уровень CO₂", "sensor", "ppm", "carbon_dioxide", "measurement", "mdi:molecule-co2" },
    { "Автоматический режим", "binary_sensor", "", "", "", "mdi:autorenew" },
    { "Ночной режим", "switch", "", "", "", "mdi:weather-night" },
    { "Цвет панели", "select", "", "", "", "mdi:palette" },
    { "Период замены фильтра", "number", "дн.", "", "measurement", "mdi:calendar-clock" },
    { "Калибровка CO₂", "binary_sensor", "", "", "", "mdi:tune" },
    { "Автокалибровка CO₂", "binary_sensor", "", "", "", "mdi:molecule-co2" }
  };
  switch (id) {
    case 101: return &values[0]; case 102: return &values[1];
    case 103: return &values[2]; case 104: return &values[3];
    case 105: return &values[4]; case 107: return &values[5];
    case 108: return &values[6]; case 109: return &values[7];
    case 110: return &values[8]; case 111: return &values[9];
    case 112: return &values[10]; case 113: return &values[11];
    case 114: return &values[12]; case 115: return &values[13];
    case 106: return &values[14]; case 116: return &values[15];
    case 117: return &values[16]; case 118: return &values[17];
    case 119: return &values[18]; case 120: return &values[19];
    case 122: return &values[20]; case 123: return &values[21];
    case 124: return &values[22]; case 125: return &values[23];
    case 126: return &values[24]; case 128: return &values[25];
    case 129: return &values[26]; case 130: return &values[27];
    case 131: return &values[28]; case 132: return &values[29];
    case 133: return &values[30]; case 134: return &values[31];
    case 135: return &values[32]; case 136: return &values[33];
    default: return nullptr;
  }
}

const char *enumValue(uint8_t id, int32_t value) {
  static const char *light[] = { "0", "50", "100" };
  static const char *heater[] = { "Выключен", "1 кВт", "1,4 кВт" };
  static const char *errors[] = { "Нет ошибок", "Ошибка 1", "Ошибка 2", "Ошибка 3", "Ошибка 4",
    "Ошибка 5", "Ошибка 6", "Ошибка 7", "Ошибка 8", "Ошибка 9", "Предупреждение 10", "Предупреждение 11" };
  static const char *sensor[] = { "absent", "wait", "wait_data", "avail" };
  static const char *voice[] = { "1", "2", "3", "4", "5", "6", "7" };
  static const char *colors[] = { "color0", "color1", "color2", "color3", "color4" };
  const char **items = nullptr;
  size_t count = 0;
  switch (id) {
    case 104: items = light; count = 3; break;
    case 118: items = heater; count = 3; break;
    case 127: items = errors; count = 12; break;
    case 122: case 123: case 124: items = sensor; count = 4; break;
    case 125: items = voice; count = 7; break;
    case 133: items = colors; count = 5; break;
    default: return nullptr;
  }
  return value >= 0 && static_cast<size_t>(value) < count ? items[value] : "unknown";
}

void publishDatapoint(uint8_t id) {
  if (!mqtt.connected()) return;
  const TionDpState *state = tion_model.get(id);
  if (state == nullptr) return;
  char value[32];
  if (state->type == TuyaDpType::BOOL) {
    snprintf(value, sizeof(value), "%s", state->value ? "ON" : "OFF");
  } else if (id == 104 || id == 118 || id == 122 || id == 123 ||
             id == 124 || id == 125 || id == 127 || id == 133) {
    const char *decoded = enumValue(id, state->value);
    strlcpy(value, decoded ? decoded : "unknown", sizeof(value));
  } else {
    snprintf(value, sizeof(value), "%ld", static_cast<long>(state->value));
  }
  const String topic = stateTopic(id);
  mqtt.publish(topic.c_str(), value, true);
}

void publishDiscoveryDatapoint(uint8_t id) {
  if (!mqtt.connected() || !tion_model.supports(id)) return;
  const DatapointInfo *info = datapointInfo(id);
  if (info == nullptr) return;
  char topic[160];
  char payload[768];
  snprintf(topic, sizeof(topic), "%s/%s/%s/dp_%u/config",
           TION_MQTT_DISCOVERY_PREFIX, info->domain, TION_DEVICE_ID, id);
  char category_json[48] = "";
  const char *category = entityCategory(id);
  if (category != nullptr) {
    snprintf(category_json, sizeof(category_json), ",\"entity_category\":\"%s\"", category);
  }
  if (strcmp(info->domain, "switch") == 0) {
    snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
      "\"state_topic\":\"%sstate/dp_%u\","
      "\"command_topic\":\"%scommand/dp_%u\","
      "\"availability_topic\":\"%savailability\","
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"state_on\":\"ON\",\"state_off\":\"OFF\","
      "\"icon\":\"%s\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"TION\",\"model\":\"Bio X\"}}",
      info->name, TION_DEVICE_ID, id, baseTopic().c_str(), id,
      baseTopic().c_str(), id, baseTopic().c_str(), info->icon,
      TION_DEVICE_ID, TION_DEVICE_NAME);
  } else if (strcmp(info->domain, "select") == 0) {
    const char *options = id == 104
      ? "\"options\":[\"0\",\"50\",\"100\"]"
      : "\"options\":[\"color0\",\"color1\",\"color2\",\"color3\",\"color4\"]";
    snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
      "\"state_topic\":\"%sstate/dp_%u\","
      "\"command_topic\":\"%scommand/dp_%u\","
      "\"availability_topic\":\"%savailability\","
      "\"entity_category\":\"config\",\"icon\":\"%s\",%s,"
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"TION\",\"model\":\"Bio X\"}}",
      info->name, TION_DEVICE_ID, id, baseTopic().c_str(), id,
      baseTopic().c_str(), id, baseTopic().c_str(), info->icon, options,
      TION_DEVICE_ID, TION_DEVICE_NAME);
  } else if (strcmp(info->domain, "number") == 0) {
    snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
      "\"state_topic\":\"%sstate/dp_%u\","
      "\"command_topic\":\"%scommand/dp_%u\","
      "\"availability_topic\":\"%savailability\","
      "\"entity_category\":\"config\",\"icon\":\"%s\","
      "\"unit_of_measurement\":\"%s\",\"min\":1,\"max\":365,\"step\":1,"
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"TION\",\"model\":\"Bio X\"}}",
      info->name, TION_DEVICE_ID, id, baseTopic().c_str(), id,
      baseTopic().c_str(), id, baseTopic().c_str(), info->icon, info->unit,
      TION_DEVICE_ID, TION_DEVICE_NAME);
  } else if (strcmp(info->domain, "binary_sensor") == 0) {
    snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
      "\"state_topic\":\"%sstate/dp_%u\","
      "\"availability_topic\":\"%savailability\"%s,"
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"icon\":\"%s\","
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"TION\","
      "\"model\":\"Bio X\"}}",
      info->name, TION_DEVICE_ID, id, baseTopic().c_str(), id,
      baseTopic().c_str(), category_json, info->icon, TION_DEVICE_ID, TION_DEVICE_NAME);
  } else {
    char optional[160] = "";
    if (strlen(info->device_class) > 0) {
      snprintf(optional + strlen(optional), sizeof(optional) - strlen(optional),
               ",\"device_class\":\"%s\"", info->device_class);
    }
    if (strlen(info->state_class) > 0) {
      snprintf(optional + strlen(optional), sizeof(optional) - strlen(optional),
               ",\"state_class\":\"%s\"", info->state_class);
    }
    snprintf(payload, sizeof(payload),
      "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
      "\"state_topic\":\"%sstate/dp_%u\","
      "\"availability_topic\":\"%savailability\"%s,"
      "\"icon\":\"%s\",\"unit_of_measurement\":\"%s\"%s,"
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"TION\","
      "\"model\":\"Bio X\"}}",
      info->name, TION_DEVICE_ID, id, baseTopic().c_str(), id,
      baseTopic().c_str(), category_json, info->icon, info->unit, optional,
      TION_DEVICE_ID, TION_DEVICE_NAME);
  }
  if (mqtt.publish(topic, payload, true)) publishDatapoint(id);
}

void publishClimateState() {
  if (!mqtt.connected()) return;
  const TionDpState *power = tion_model.get(101);
  const TionDpState *heater_raw = tion_model.get(105);
  const TionDpState *fan = tion_model.get(102);
  const TionDpState *target = tion_model.get(114);
  const TionDpState *current = tion_model.get(108);

  if (power && heater_raw) {
    const char *mode = !power->value ? "off" : (!heater_raw->value ? "heat" : "fan_only");
    mqtt.publish((baseTopic() + "state/climate/mode").c_str(), mode, true);
  }
  if (fan) {
    char value[8];
    snprintf(value, sizeof(value), "%ld", static_cast<long>(fan->value));
    mqtt.publish((baseTopic() + "state/climate/fan_mode").c_str(), value, true);
  }
  if (target) {
    char value[16];
    snprintf(value, sizeof(value), "%ld", static_cast<long>(target->value));
    mqtt.publish((baseTopic() + "state/climate/temperature").c_str(), value, true);
  }
  if (current) {
    char value[16];
    snprintf(value, sizeof(value), "%ld", static_cast<long>(current->value));
    mqtt.publish((baseTopic() + "state/climate/current_temperature").c_str(), value, true);
  }
}

void publishClimateDiscovery() {
  if (!mqtt.connected()) return;
  char topic[160];
  char payload[1024];
  snprintf(topic, sizeof(topic), "%s/climate/%s/climate/config",
           TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Климат TION Bio X\",\"unique_id\":\"%s_climate\","
    "\"availability_topic\":\"%savailability\","
    "\"mode_state_topic\":\"%sstate/climate/mode\","
    "\"mode_command_topic\":\"%scommand/climate/mode\","
    "\"modes\":[\"off\",\"fan_only\",\"heat\"],"
    "\"temperature_state_topic\":\"%sstate/climate/temperature\","
    "\"temperature_command_topic\":\"%scommand/climate/temperature\","
    "\"current_temperature_topic\":\"%sstate/climate/current_temperature\","
    "\"fan_mode_state_topic\":\"%sstate/climate/fan_mode\","
    "\"fan_mode_command_topic\":\"%scommand/climate/fan_mode\","
    "\"fan_modes\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\"],"
    "\"min_temp\":0,\"max_temp\":30,\"temp_step\":1,"
    "\"precision\":1,\"temperature_unit\":\"C\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
    "\"manufacturer\":\"TION\",\"model\":\"Bio X\"}}",
    TION_DEVICE_ID, baseTopic().c_str(), baseTopic().c_str(), baseTopic().c_str(),
    baseTopic().c_str(), baseTopic().c_str(), baseTopic().c_str(),
    baseTopic().c_str(), baseTopic().c_str(), TION_DEVICE_ID, TION_DEVICE_NAME);
  mqtt.publish(topic, payload, true);
}

void publishDiscoveryButton(uint8_t id, const char *name, const char *icon,
                            bool disabled_by_default) {
  if (!mqtt.connected()) return;
  char topic[160];
  char payload[768];
  snprintf(topic, sizeof(topic), "%s/button/%s/dp_%u/config",
           TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s\",\"unique_id\":\"%s_dp_%u\","
    "\"command_topic\":\"%scommand/dp_%u\","
    "\"payload_press\":\"PRESS\",\"availability_topic\":\"%savailability\","
    "\"entity_category\":\"config\",\"icon\":\"%s\","
    "\"entity_registry_enabled_default\":%s,"
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
    "\"manufacturer\":\"TION\",\"model\":\"Bio X\"}}",
    name, TION_DEVICE_ID, id, baseTopic().c_str(), id, baseTopic().c_str(),
    icon, disabled_by_default ? "false" : "true", TION_DEVICE_ID, TION_DEVICE_NAME);
  mqtt.publish(topic, payload, true);
}

void publishDiscoverySensor(const char *object_id, const char *name,
                            const char *state_suffix, const char *unit,
                            const char *icon) {
  if (!mqtt.connected()) return;
  char topic[160];
  char payload[512];
  snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config",
           TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, object_id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s\",\"unique_id\":\"%s_%s\","
    "\"state_topic\":\"%sdiagnostic/%s\","
    "\"availability_topic\":\"%savailability\"%s,"
    "\"entity_category\":\"diagnostic\","
    "\"icon\":\"%s\","
    "\"unit_of_measurement\":\"%s\","
    "\"device\":{\"identifiers\":[\"%s\"],"
    "\"name\":\"%s\",\"manufacturer\":\"TION\","
    "\"model\":\"Bio X\"}}",
    name, TION_DEVICE_ID, object_id, baseTopic().c_str(), state_suffix,
    baseTopic().c_str(), icon, unit, TION_DEVICE_ID, TION_DEVICE_NAME);
  mqtt.publish(topic, payload, true);
}

void publishDiagnostics() {
  if (!mqtt.connected()) return;
  char value[24];

  snprintf(value, sizeof(value), "%d", WiFi.RSSI());
  mqtt.publish((baseTopic() + "diagnostic/wifi_rssi").c_str(), value, true);
  mqtt.publish((baseTopic() + "diagnostic/wifi_ip").c_str(),
               WiFi.localIP().toString().c_str(), true);
  mqtt.publish((baseTopic() + "diagnostic/uart_state").c_str(),
               tion_uart.initialized() ? "initialized" : "waiting", true);

  snprintf(value, sizeof(value), "%lu",
           static_cast<unsigned long>(tion_uart.validFrames()));
  mqtt.publish((baseTopic() + "diagnostic/uart_valid_frames").c_str(), value, true);
  snprintf(value, sizeof(value), "%lu",
           static_cast<unsigned long>(tion_uart.checksumErrors()));
  mqtt.publish((baseTopic() + "diagnostic/uart_checksum_errors").c_str(), value, true);
  snprintf(value, sizeof(value), "%lu",
           static_cast<unsigned long>(tion_uart.framingErrors()));
  mqtt.publish((baseTopic() + "diagnostic/uart_framing_errors").c_str(), value, true);
  snprintf(value, sizeof(value), "%u", ESP.getFreeHeap());
  mqtt.publish((baseTopic() + "diagnostic/free_heap").c_str(), value, true);
}

void publishDiscovery() {
  if (!mqtt.connected()) return;

  // Climate command topics are non-retained and are subscribed only after MQTT connects.
  mqtt.subscribe((baseTopic() + "command/climate/#").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_103").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_104").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_107").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_111").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_132").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_109").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_110").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_133").c_str());
  mqtt.subscribe((baseTopic() + "command/dp_134").c_str());

  // Publish diagnostic entities immediately; they do not depend on TION UART.
  publishDiscoverySensor("wifi_rssi", "TION ESP Wi-Fi signal",
                         "wifi_rssi", "dBm", "mdi:wifi");
  publishDiscoverySensor("wifi_ip", "TION ESP IP address",
                         "wifi_ip", "", "mdi:ip-network");
  publishDiscoverySensor("uart_state", "TION UART state",
                         "uart_state", "", "mdi:serial-port");
  publishDiscoverySensor("uart_valid_frames", "TION UART valid frames",
                         "uart_valid_frames", "", "mdi:counter");
  publishDiscoverySensor("uart_checksum_errors", "TION UART checksum errors",
                         "uart_checksum_errors", "", "mdi:alert-circle-outline");
  publishDiscoverySensor("uart_framing_errors", "TION UART framing errors",
                         "uart_framing_errors", "", "mdi:alert-outline");
  publishDiscoverySensor("free_heap", "TION ESP free heap",
                         "free_heap", "B", "mdi:memory");

  // Remove legacy discovery records for excluded datapoints and old sensor-domain
  // records for entities that are now binary_sensors. Retained empty payload deletes them.
  const uint8_t removed_ids[] = {117, 120, 125, 126};
  for (uint8_t id : removed_ids) {
    char old_topic[160];
    snprintf(old_topic, sizeof(old_topic), "%s/sensor/%s/dp_%u/config",
             TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
    mqtt.publish(old_topic, "", true);
    snprintf(old_topic, sizeof(old_topic), "%s/binary_sensor/%s/dp_%u/config",
             TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
    mqtt.publish(old_topic, "", true);
  }
  const uint8_t binary_ids[] = {101, 103, 105, 107, 109, 110, 111, 117, 131, 132, 135, 136};
  for (uint8_t id : binary_ids) {
    char old_topic[160];
    snprintf(old_topic, sizeof(old_topic), "%s/sensor/%s/dp_%u/config",
             TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
    mqtt.publish(old_topic, "", true);
  }

  const uint8_t changed_domain_ids[] = {104, 133, 134};
  for (uint8_t id : changed_domain_ids) {
    char old_topic[160];
    snprintf(old_topic, sizeof(old_topic), "%s/sensor/%s/dp_%u/config",
             TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
    mqtt.publish(old_topic, "", true);
  }
  // Publish the complete profile entity set before TION UART is connected.
  // Entities without a received state remain unknown in Home Assistant.
  for (uint8_t id = 101; id <= 136; ++id) {
    if (!tion_model.supports(id)) continue;
    publishDiscoveryDatapoint(id);
    yield();
  }
  publishDiscoveryButton(109, "Сброс к заводским настройкам", "mdi:factory", true);
  publishDiscoveryButton(110, "Сброс счётчика фильтра", "mdi:filter", false);
  publishClimateDiscovery();
  publishClimateState();
  discovery_sent = true;
  publishDiagnostics();
  last_diagnostics_publish = millis();
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  if (length == 0 || length >= 32 || !tion_uart.initialized()) return;
  char value[32];
  memcpy(value, payload, length);
  value[length] = '\0';
  const String command = String(value);
  const String topic_name(topic);

  if (topic_name == baseTopic() + "command/climate/mode") {
    if (command == "off") {
      tion_uart.sendDatapointBool(105, true);   // DP105 is inverted in ESPHome.
      tion_uart.sendDatapointBool(101, false);
    } else if (command == "fan_only") {
      tion_uart.sendDatapointBool(105, true);
      tion_uart.sendDatapointBool(101, true);
    } else if (command == "heat") {
      tion_uart.sendDatapointBool(101, true);
      tion_uart.sendDatapointBool(105, false);
    }
    return;
  }

  if (topic_name == baseTopic() + "command/climate/fan_mode") {
    const long speed = command.toInt();
    if (speed >= 1 && speed <= 7 && String(speed) == command) {
      tion_uart.sendDatapointInteger(102, static_cast<int32_t>(speed));
    }
    return;
  }

  if (topic_name == baseTopic() + "command/climate/temperature") {
    char *end = nullptr;
    const long temperature = strtol(value, &end, 10);
    if (end != value && *end == '\0' && temperature >= 0 && temperature <= 30) {
      tion_uart.sendDatapointInteger(114, static_cast<int32_t>(temperature));
    }
    return;
  }

  if (topic_name == baseTopic() + "command/dp_103" ||
      topic_name == baseTopic() + "command/dp_107" ||
      topic_name == baseTopic() + "command/dp_111" ||
      topic_name == baseTopic() + "command/dp_132") {
    bool state;
    if (command == "ON") state = true;
    else if (command == "OFF") state = false;
    else return;
    if (topic_name == baseTopic() + "command/dp_107") {
      const TionDpState *power = tion_model.get(101);
      if (state && (!power || power->value == 0)) return;
    }
    uint8_t id = topic_name.endsWith("dp_103") ? 103 :
                 topic_name.endsWith("dp_107") ? 107 :
                 topic_name.endsWith("dp_111") ? 111 : 132;
    tion_uart.sendDatapointBool(id, state);
    return;
  }

  if (topic_name == baseTopic() + "command/dp_104") {
    if (command == "0") tion_uart.sendDatapointEnum(104, 0);
    else if (command == "50") tion_uart.sendDatapointEnum(104, 1);
    else if (command == "100") tion_uart.sendDatapointEnum(104, 2);
    return;
  }
  if (topic_name == baseTopic() + "command/dp_133") {
    if (command.length() == 6 && command.startsWith("color") &&
        command[5] >= '0' && command[5] <= '4') {
      tion_uart.sendDatapointEnum(133, static_cast<uint8_t>(command[5] - '0'));
    }
    return;
  }
  if (topic_name == baseTopic() + "command/dp_134") {
    char *end = nullptr;
    const long days = strtol(value, &end, 10);
    if (end != value && *end == '\0' && days >= 1 && days <= 365) {
      tion_uart.sendDatapointInteger(134, static_cast<int32_t>(days));
    }
    return;
  }
  if (topic_name == baseTopic() + "command/dp_110" && command == "PRESS") {
    tion_uart.sendDatapointBool(110, true);
    return;
  }
  if (topic_name == baseTopic() + "command/dp_109" && command == "PRESS") {
    tion_uart.sendDatapointBool(109, true);
    return;
  }
}

void onDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                 uint16_t length) {
  TuyaDatapoint decoded;
  if (!decodeTuyaDatapoint(id, type, data, length, decoded)) return;
  if (!tion_model.update(decoded)) return;
  // Discovery for every supported datapoint was already published at MQTT connect.
  publishDatapoint(id);
  if (id == 101 || id == 102 || id == 105 || id == 108 || id == 114) {
    publishClimateState();
  }
}

void handleRoot() {
  String page;
  page.reserve(2400);
  page += F("<!doctype html><html lang='ru'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>TION Bio X</title><style>"
            "body{font:16px Arial;max-width:620px;margin:24px auto;padding:0 16px;"
            "background:#f4f6f8;color:#20252b}main{background:white;padding:20px;"
            "border-radius:12px}label{display:block;margin-top:14px;font-weight:bold}"
            "input,select{box-sizing:border-box;width:100%;padding:11px;margin-top:5px;"
            "border:1px solid #bbc3cc;border-radius:6px;font-size:16px}"
            "button{margin-top:20px;padding:12px 18px;background:#1769aa;color:white;"
            "border:0;border-radius:6px;font-size:16px}small{color:#59636e}"
            "</style><main><h2>TION Bio X — настройка</h2>");
  page += F("<p>Wi-Fi: ");
  page += (WiFi.status() == WL_CONNECTED) ? F("подключено") : F("не подключено");
  page += F("<br>IP: ");
  page += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(TION_SETUP_AP_ADDRESS);
  page += F("<br>MQTT: ");
  page += mqtt.connected() ? F("подключено") : F("не подключено");
  page += F("</p><form method='post' action='/save'>"
            "<label>Имя Wi-Fi сети (SSID)</label><input name='ssid' required "
            "maxlength='32' autocomplete='off' placeholder='Введите точное имя сети' value='");
  page += htmlEscape(String(device_config.wifi_ssid));
  page += F("'><small>Введите имя сети вручную. Регистр букв и пробелы имеют значение.</small>"
            "<label>Пароль Wi-Fi</label>"
            "<input name='wifi_password' type='password' maxlength='64' "
            "autocomplete='new-password' placeholder='Введите пароль Wi-Fi'>"
            "<small>При повторной настройке пароль нужно ввести заново. "
            "Для открытой сети оставьте поле пустым.</small>"
            "<label>MQTT сервер</label><input name='mqtt_host' required maxlength='63' value='");
  page += htmlEscape(String(device_config.mqtt_host));
  page += F("'><label>MQTT порт</label><input name='mqtt_port' type='number' "
            "min='1' max='65535' required value='");
  page += String(device_config.mqtt_port);
  page += F("'><label>MQTT пользователь</label><input name='mqtt_username' "
            "maxlength='32' value='");
  page += htmlEscape(String(device_config.mqtt_username));
  page += F("'><label>MQTT пароль</label><input name='mqtt_password' type='password' "
            "maxlength='64' autocomplete='new-password' "
            "placeholder='Оставьте пустым, чтобы не менять'>"
            "<button type='submit'>Сохранить и перезагрузить</button></form>"
            "<p><small>Настройки хранятся во Flash устройства. Пароли не шифруются.</small></p>"
            "</main></html>");
  web.send(200, "text/html; charset=utf-8", page);
}

void handleSave() {
  if (!web.hasArg("ssid") || !web.hasArg("mqtt_host") ||
      !web.hasArg("mqtt_port")) {
    web.send(400, "text/plain; charset=utf-8", "Не заполнены обязательные поля");
    return;
  }
  String ssid = web.arg("ssid");
  if (web.hasArg("ssid_manual") && web.arg("ssid_manual").length() > 0) {
    ssid = web.arg("ssid_manual");
  }
  const String wifi_password = web.arg("wifi_password");
  const String host = web.arg("mqtt_host");
  const String port_text = web.arg("mqtt_port");
  const String username = web.arg("mqtt_username");
  const String password = web.arg("mqtt_password");
  const long port = port_text.toInt();

  if (ssid.length() == 0 || ssid.length() > 32 || host.length() == 0 ||
      host.length() > 63 || port < 1 || port > 65535 ||
      wifi_password.length() > 64 || username.length() > 32 ||
      password.length() > 64) {
    web.send(400, "text/plain; charset=utf-8", "Проверьте длину полей и MQTT порт");
    return;
  }

  memset(device_config.wifi_ssid, 0, sizeof(device_config.wifi_ssid));
  memset(device_config.mqtt_host, 0, sizeof(device_config.mqtt_host));
  memset(device_config.mqtt_username, 0, sizeof(device_config.mqtt_username));
  strlcpy(device_config.wifi_ssid, ssid.c_str(), sizeof(device_config.wifi_ssid));
  if (wifi_password.length() > 0) {
    strlcpy(device_config.wifi_password, wifi_password.c_str(), sizeof(device_config.wifi_password));
  }
  strlcpy(device_config.mqtt_host, host.c_str(), sizeof(device_config.mqtt_host));
  strlcpy(device_config.mqtt_username, username.c_str(), sizeof(device_config.mqtt_username));
  if (password.length() > 0) {
    strlcpy(device_config.mqtt_password, password.c_str(), sizeof(device_config.mqtt_password));
  }
  device_config.mqtt_port = static_cast<uint16_t>(port);

  if (!ConfigStore::save(device_config)) {
    web.send(500, "text/plain; charset=utf-8", "Не удалось сохранить настройки во Flash");
    return;
  }
  web.send(200, "text/html; charset=utf-8",
           "<meta charset='utf-8'><h3>Настройки сохранены. Устройство перезагружается...</h3>");
  delay(700);
  ESP.restart();
}

void handleNotFound() {
  if (ap_active) {
    web.sendHeader("Location", String("http://") + TION_SETUP_AP_ADDRESS + "/", true);
    web.send(302, "text/plain", "");
  } else {
    web.send(404, "text/plain; charset=utf-8", "Not found");
  }
}

void startPortal() {
  if (!ap_active) {
    WiFi.mode(WIFI_AP_STA);
    IPAddress ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gateway, subnet);
    WiFi.softAP(TION_SETUP_AP_SSID, kSetupApPassword);
    dns.start(kDnsPort, "*", ip);
    ap_active = true;
  }
}

void stopPortal() {
  if (!ap_active) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap_active = false;
}

void setupWeb() {
  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.onNotFound(handleNotFound);
  web.begin();
}

void startStation() {
  if (strlen(device_config.wifi_ssid) == 0) {
    startPortal();
    return;
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(ap_active ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(device_config.wifi_ssid, device_config.wifi_password);
  wifi_attempts = 1;
  wifi_attempt_started = millis();
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifi_attempts = 0;
    if (ap_active) stopPortal();
    return;
  }
  if (strlen(device_config.wifi_ssid) == 0) {
    startPortal();
    return;
  }

  const uint32_t now = millis();

  // Once the setup AP is active, retry the saved network independently.
  // Do not let the initial five-attempt counter starve this 30-second retry.
  if (ap_active) {
    if (now - last_wifi_retry >= kWifiRetryAfterPortalMs) {
      last_wifi_retry = now;
      WiFi.disconnect();
      WiFi.begin(device_config.wifi_ssid, device_config.wifi_password);
      wifi_attempt_started = now;
    }
    return;
  }

  if (wifi_attempts == 0) {
    wifi_attempts = 1;
    WiFi.begin(device_config.wifi_ssid, device_config.wifi_password);
    wifi_attempt_started = now;
    return;
  }
  if (wifi_attempts < kMaxWifiAttempts &&
      now - wifi_attempt_started >= kWifiAttemptTimeoutMs) {
    ++wifi_attempts;
    WiFi.disconnect();
    WiFi.begin(device_config.wifi_ssid, device_config.wifi_password);
    wifi_attempt_started = now;
  } else if (wifi_attempts == kMaxWifiAttempts &&
             now - wifi_attempt_started >= kWifiAttemptTimeoutMs) {
    startPortal();
    last_wifi_retry = now;
  }
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const String client_id = String(TION_DEVICE_ID) + "-" + String(ESP.getChipId(), HEX);
  const String availability = baseTopic() + "availability";
  bool connected;

  if (strlen(device_config.mqtt_username) > 0) {
    connected = mqtt.connect(client_id.c_str(), device_config.mqtt_username,
      device_config.mqtt_password, availability.c_str(), 0, true, "offline");
  } else {
    connected = mqtt.connect(client_id.c_str(), availability.c_str(),
      0, true, "offline");
  }
  if (!connected) return;
  mqtt.publish(availability.c_str(), "online", true);
  discovery_sent = false;
}
}  // namespace

void setup() {
  ConfigStore::load(device_config);
  mqtt.setServer(device_config.mqtt_host, device_config.mqtt_port);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMqttMessage);

  setupWeb();
  startStation();

  const uint8_t wifi_status = WiFi.status() == WL_CONNECTED ? 0x03 : (ap_active ? 0x01 : 0x02);
  tion_uart.setWifiStatus(wifi_status);
  tion_uart.begin(TION_UART_BAUD, onDatapoint);
}

void loop() {
  maintainWifi();
  web.handleClient();
  if (ap_active) dns.processNextRequest();

  const uint8_t wifi_status = WiFi.status() == WL_CONNECTED ? 0x03 : (ap_active ? 0x01 : 0x02);
  tion_uart.setWifiStatus(wifi_status);
  tion_uart.loop();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected() && millis() - last_mqtt_attempt >= kMqttRetryMs) {
      last_mqtt_attempt = millis();
      connectMqtt();
    }
    if (mqtt.connected()) {
      mqtt.loop();
      if (!discovery_sent) publishDiscovery();
      if (discovery_sent &&
          millis() - last_diagnostics_publish >= 30000) {
        publishDiagnostics();
        last_diagnostics_publish = millis();
      }
    }
  }
  yield();
}
