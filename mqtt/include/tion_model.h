#pragma once

#include <Arduino.h>
#include "tuya_dp.h"

struct TionDpState {
  bool received = false;
  TuyaDpType type = TuyaDpType::RAW;
  int32_t value = 0;
  uint32_t updated_ms = 0;
};

class TionModel {
 public:
  bool update(const TuyaDatapoint &dp);
  bool has(uint8_t id) const;
  bool supports(uint8_t id) const;
  const TionDpState *get(uint8_t id) const;

 private:
  static constexpr uint8_t kMaxDpId = 136;
  TionDpState states_[kMaxDpId + 1] = {};
  bool isKnown(uint8_t id) const;
  bool typeMatches(uint8_t id, TuyaDpType type) const;
};
