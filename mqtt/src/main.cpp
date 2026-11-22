#include <Arduino.h>
#include "tuya_uart.h"
#include "tuya_dp.h"
#include "tion_model.h"

// ESP-01 hardware UART uses GPIO1 (TX) and GPIO3 (RX).
// Do not use Serial for debug output: it is the TION protocol channel.

namespace {
TuyaUart tion_uart(Serial);
TionModel tion_model;

void onDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                 uint16_t length) {
  TuyaDatapoint decoded;
  if (!decodeTuyaDatapoint(id, type, data, length, decoded)) return;
  tion_model.update(decoded);
}
}  // namespace

void setup() {
  tion_uart.begin(9600, onDatapoint);
}

void loop() {
  tion_uart.loop();
  yield();
}
