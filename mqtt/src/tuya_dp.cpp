#include "tuya_dp.h"

bool decodeTuyaDatapoint(uint8_t id, uint8_t type, const uint8_t *data,
                         uint16_t length, TuyaDatapoint &out) {
  if (length > 0 && data == nullptr) return false;
  if (type > static_cast<uint8_t>(TuyaDpType::BITMASK)) return false;

  out.id = id;
  out.type = static_cast<TuyaDpType>(type);
  out.data = data;
  out.length = length;

  switch (out.type) {
    case TuyaDpType::RAW:
    case TuyaDpType::STRING:
    case TuyaDpType::BITMASK:
      return true;

    case TuyaDpType::BOOL:
      if (length != 1 || data[0] > 1) return false;
      out.bool_value = data[0] == 1;
      return true;

    case TuyaDpType::INTEGER: {
      if (length != 4) return false;
      const uint32_t value =
          (static_cast<uint32_t>(data[0]) << 24) |
          (static_cast<uint32_t>(data[1]) << 16) |
          (static_cast<uint32_t>(data[2]) << 8) |
          static_cast<uint32_t>(data[3]);
      out.int_value = static_cast<int32_t>(value);
      return true;
    }

    case TuyaDpType::ENUM:
      if (length != 1) return false;
      out.enum_value = data[0];
      return true;
  }

  return false;
}
