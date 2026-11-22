#include "tion_model.h"

// Each PlatformIO environment must select exactly one device profile.
#if defined(TION_PROFILE_LITE) && defined(TION_PROFILE_FULL)
#error "Select only one TION profile: LITE or FULL"
#elif !defined(TION_PROFILE_LITE) && !defined(TION_PROFILE_FULL)
#error "No TION profile selected. Use tion_biox_lite or tion_biox_full"
#endif

bool TionModel::isKnown(uint8_t id) const {
  switch (id) {
    // Datapoints available on the Lite and Full appliance profiles.
    case 101: case 102: case 103: case 104: case 105:
    case 107: case 108: case 109: case 110: case 111:
    case 112: case 113: case 114: case 115:
    case 118: case 127: case 132: case 133: case 134:
#if defined(TION_PROFILE_FULL)
    // Additional air-quality and automatic-control datapoints on Full.
    case 106: case 116: case 117: case 119: case 120:
    case 122: case 123: case 124: case 125: case 126:
    case 128: case 129: case 130: case 131:
    case 135: case 136:
#endif
      return true;
    default:
      return false;
  }
}

bool TionModel::typeMatches(uint8_t id, TuyaDpType type) const {
  switch (id) {
    // Boolean datapoints common to Lite and Full.
    case 101: case 103: case 105: case 107: case 109:
    case 110: case 111: case 132:
      return type == TuyaDpType::BOOL;

    // Integer datapoints common to Lite and Full.
    case 102: case 108: case 112: case 113: case 114:
    case 115: case 134:
      return type == TuyaDpType::INTEGER;

    // Enumerated datapoints common to Lite and Full.
    case 104: case 118: case 127: case 133:
      return type == TuyaDpType::ENUM;

#if defined(TION_PROFILE_FULL)
    // Full-only boolean datapoints.
    case 117: case 131: case 135: case 136:
      return type == TuyaDpType::BOOL;

    // Full-only integer datapoints.
    case 106: case 116: case 119: case 120: case 126:
    case 128: case 129: case 130:
      return type == TuyaDpType::INTEGER;

    // Full-only enumerated datapoints.
    case 122: case 123: case 124: case 125:
      return type == TuyaDpType::ENUM;
#endif

    default:
      return false;
  }
}

bool TionModel::update(const TuyaDatapoint &dp) {
  if (dp.id > kMaxDpId || !isKnown(dp.id) ||
      !typeMatches(dp.id, dp.type)) {
    return false;
  }

  int32_t value = 0;
  switch (dp.type) {
    case TuyaDpType::BOOL:
      value = dp.bool_value ? 1 : 0;
      break;
    case TuyaDpType::INTEGER:
      value = dp.int_value;
      break;
    case TuyaDpType::ENUM:
      value = dp.enum_value;
      break;
    default:
      return false;
  }

  TionDpState &state = states_[dp.id];
  state.received = true;
  state.type = dp.type;
  state.value = value;
  state.updated_ms = millis();
  return true;
}

bool TionModel::has(uint8_t id) const {
  return id <= kMaxDpId && states_[id].received;
}

const TionDpState *TionModel::get(uint8_t id) const {
  if (id > kMaxDpId || !states_[id].received) return nullptr;
  return &states_[id];
}
