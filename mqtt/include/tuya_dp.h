#pragma once

#include <Arduino.h>

enum class TuyaDpType : uint8_t {
  RAW = 0,
  BOOL = 1,
  INTEGER = 2,
  STRING = 3,
  ENUM = 4,
  BITMASK = 5
};

struct TuyaDatapoint {
  uint8_t id = 0;
  TuyaDpType type = TuyaDpType::RAW;
  const uint8_t *data = nullptr;
  uint16_t length = 0;
  bool bool_value = false;
  int32_t int_value = 0;
  uint8_t enum_value = 0;
};

// Decodes the common Tuya scalar types. RAW, STRING and BITMASK remain
// available through data/length and are not copied or modified.
bool decodeTuyaDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                         uint16_t length, TuyaDatapoint &out);
