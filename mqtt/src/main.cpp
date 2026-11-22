#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "app_config.h"
#include "tuya_uart.h"
#include "tuya_dp.h"
#include "tion_model.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// ESP-01 hardware UART uses GPIO1 (TX) and GPIO3 (RX).
// Never print debug messages to Serial: it is connected to the TION UART.

namespace {
TuyaUart tion_uart(Serial);
TionModel tion_model;
WiFiClient wifi_client;
PubSubClient mqtt(wifi_client);

constexpr uint32_t kMqttRetryMs = 5000;
uint32_t last_mqtt_attempt = 0;
bool discovery_sent = false;

String baseTopic() {
  return String(TION_MQTT_PREFIX) + "/" + TION_DEVICE_ID + "/";
}

String stateTopic(uint8_t id) {
  return baseTopic() + "state/dp_" + String(id);
}

void publishDatapoint(uint8_t id) {
  if (!mqtt.connected()) return;
  const TionDpState *state = tion_model.get(id);
  if (state == nullptr) return;
  char value[16];
  snprintf(value, sizeof(value), "%ld", static_cast<long>(state->value));
  const String topic = stateTopic(id);
  mqtt.publish(topic.c_str(), value, true);
}

void publishDiscoveryDatapoint(uint8_t id) {
  if (!mqtt.connected() || !tion_model.has(id)) return;

  char topic[160];
  char payload[512];
  snprintf(topic, sizeof(topic), "%s/sensor/%s/dp_%u/config",
           TION_MQTT_DISCOVERY_PREFIX, TION_DEVICE_ID, id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"TION DP %u\",\"unique_id\":\"%s_dp_%u\","
    "\"state_topic\":\"%sstate/dp_%u\","
    "\"availability_topic\":\"%savailability\","
    "\"device\":{\"identifiers\":[\"%s\"],"
    "\"name\":\"%s\",\"manufacturer\":\"TION\","
    "\"model\":\"Bio X\"}}",
    id, TION_DEVICE_ID, id, baseTopic().c_str(), id,
    baseTopic().c_str(), TION_DEVICE_ID, TION_DEVICE_NAME);

  if (mqtt.publish(topic, payload, true)) publishDatapoint(id);
}

void publishDiscovery() {
  if (!mqtt.connected()) return;
  for (uint8_t id = 101; id <= 136; ++id) {
    if (!tion_model.has(id)) continue;
    publishDiscoveryDatapoint(id);
    yield();
  }
  discovery_sent = true;
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  // Intentionally read-only in this firmware stage. No TION commands
  // are accepted from MQTT until datapoint mappings are verified on hardware.
  (void) topic;
  (void) payload;
  (void) length;
}

void onDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                 uint16_t length) {
  TuyaDatapoint decoded;
  if (!decodeTuyaDatapoint(id, type, data, length, decoded)) return;
  const bool was_known = tion_model.has(id);
  if (!tion_model.update(decoded)) return;
  if (mqtt.connected() && discovery_sent && !was_known) {
    publishDiscoveryDatapoint(id);
  } else {
    publishDatapoint(id);
  }
}

void startWifi() {
  if (strlen(TION_WIFI_SSID) == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(TION_WIFI_SSID, TION_WIFI_PASSWORD);
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const String clientId = String(TION_DEVICE_ID) + "-" + String(ESP.getChipId(), HEX);
  const String availability = baseTopic() + "availability";
  bool connected;

  if (strlen(TION_MQTT_USERNAME) > 0) {
    connected = mqtt.connect(clientId.c_str(), TION_MQTT_USERNAME,
      TION_MQTT_PASSWORD, availability.c_str(), 0, true, "offline");
  } else {
    connected = mqtt.connect(clientId.c_str(), availability.c_str(),
      0, true, "offline");
  }

  if (!connected) return;
  mqtt.publish(availability.c_str(), "online", true);
  discovery_sent = false;
}
}  // namespace

void setup() {
  startWifi();
  tion_uart.setWifiStatus(WiFi.status() == WL_CONNECTED ? 0x03 : 0x02);
  tion_uart.begin(TION_UART_BAUD, onDatapoint);
  mqtt.setServer(TION_MQTT_DEFAULT_HOST, TION_MQTT_DEFAULT_PORT);
  mqtt.setBufferSize(768);
  mqtt.setCallback(onMqttMessage);
}

void loop() {
  tion_uart.setWifiStatus(WiFi.status() == WL_CONNECTED ? 0x03 : 0x02);
  tion_uart.loop();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected() && millis() - last_mqtt_attempt >= kMqttRetryMs) {
      last_mqtt_attempt = millis();
      connectMqtt();
    }
    if (mqtt.connected()) {
      mqtt.loop();
      if (!discovery_sent && tion_uart.initialized()) publishDiscovery();
    }
  }
  yield();
}
