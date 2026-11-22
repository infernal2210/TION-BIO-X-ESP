#pragma once

#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"

namespace esphome {
namespace tion_dp122_gate {

class TionDP122Gate : public Component {
 public:
  void set_ready(std::function<bool()> ready) { this->ready_ = ready; }
  void set_available(std::function<bool()> available) { this->available_ = available; }
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  void add_entity(EntityBase *entity) { this->entities_.push_back(entity); }

  void setup() override;
  bool can_proceed() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  std::function<bool()> ready_;
  std::function<bool()> available_;
  std::vector<EntityBase *> entities_;
  uint32_t started_at_{0};
  uint32_t timeout_ms_{10000};
  bool finished_{false};
};

}  // namespace tion_dp122_gate
}  // namespace esphome
