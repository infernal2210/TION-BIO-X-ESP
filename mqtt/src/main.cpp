#include <Arduino.h>
#include "tuya_uart.h"

// ESP-01 hardware UART uses GPIO1 (TX) and GPIO3 (RX).
// Do not use Serial for debug output: it is the TION protocol channel.

namespace {
TuyaUart tion_uart(Serial);

void onDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                 uint16_t length) {
  // Datapoint decoding and state publication will be added after
  // the raw UART parser is validated against real device traffic.
  (void)id;
  (void)type;
  (void)data;
  (void)length;
}
}  // namespace

void setup() {
  tion_uart.begin(9600, onDatapoint);
}

void loop() {
  tion_uart.loop();
  yield();
}
