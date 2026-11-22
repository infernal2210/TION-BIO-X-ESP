#include <Arduino.h>
#include "tuya_uart.h"
#include "tuya_dp.h"

// ESP-01 hardware UART uses GPIO1 (TX) and GPIO3 (RX).
// Do not use Serial for debug output: it is the TION protocol channel.

namespace {
TuyaUart tion_uart(Serial);

void onDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                 uint16_t length) {
  TuyaDatapoint decoded;
  if (!decodeTuyaDatapoint(id, type, data, length, decoded)) {
    // Invalid type/length combination: ignore it; never publish malformed data.
    return;
  }

  // The decoded value will be mapped to named TION entities in the next step.
  // Keep this callback free of Serial logging because UART is shared with TION.
}
}  // namespace

void setup() {
  tion_uart.begin(9600, onDatapoint);
}

void loop() {
  tion_uart.loop();
  yield();
}
