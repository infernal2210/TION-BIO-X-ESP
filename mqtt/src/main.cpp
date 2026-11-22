#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

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
bool ota_started = false;
String ota_password;

void startOtaIfConfigured() {
  if (ota_started || WiFi.status() != WL_CONNECTED) return;
  if (!ConfigStore::loadOtaPassword(ota_password)) return;
  ArduinoOTA.setHostname("tion-biox-01");
  ArduinoOTA.setPassword(ota_password.c_str());
  ArduinoOTA.begin();
  ota_started = true;
}

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
    "\"availability_topic\":\"%savailability\","
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

  // TION datapoint entities are added as soon as the appliance reports them.
  for (uint8_t id = 101; id <= 136; ++id) {
    if (!tion_model.has(id)) continue;
    publishDiscoveryDatapoint(id);
    yield();
  }
  discovery_sent = true;
  publishDiagnostics();
  last_diagnostics_publish = millis();
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  // Read-only stage: commands are intentionally disabled until datapoints
  // and their physical effects have been verified on the appliance.
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
            "<label>Пароль OTA (обновление по Wi-Fi)</label>"
            "<input name='ota_password' type='password' minlength='8' maxlength='32' "
            "autocomplete='new-password' placeholder='Задайте пароль от 8 до 32 символов'>"
            "<small>Пустое поле сохраняет прежний пароль. Если пароль ещё не задан, OTA выключено.</small>""
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
  const String new_ota_password = web.arg("ota_password");
  const long port = port_text.toInt();

  if (ssid.length() == 0 || ssid.length() > 32 || host.length() == 0 ||
      host.length() > 63 || port < 1 || port > 65535 ||
      wifi_password.length() > 64 || username.length() > 32 ||
      password.length() > 64 ||
      (new_ota_password.length() > 0 &&
       (new_ota_password.length() < 8 || new_ota_password.length() > 32))) {
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

  if (new_ota_password.length() > 0 &&
      !ConfigStore::saveOtaPassword(new_ota_password)) {
    web.send(500, "text/plain; charset=utf-8", "Не удалось сохранить пароль OTA");
    return;
  }
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
  mqtt.setBufferSize(768);
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
    startOtaIfConfigured();
    if (ota_started) ArduinoOTA.handle();
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
