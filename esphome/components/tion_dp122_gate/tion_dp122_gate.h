#pragma once

#include <utility>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/template_lambda.h"

namespace esphome {
namespace tion_dp122_gate {

class TionDP122Gate : public Component {
 public:
  template<typename F> void set_ready(F &&f) { this->ready_.set(std::forward<F>(f)); }
  template<typename F> void set_available(F &&f) { this->available_.set(std::forward<F>(f)); }
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }
  void add_entity(EntityBase *entity) { this->entities_.push_back(entity); }

  void setup() override;
  bool can_proceed() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

 protected:
  TemplateLambda<bool> ready_;
  TemplateLambda<bool> available_;
  std::vector<EntityBase *> entities_;
  uint32_t started_at_{0};
  uint32_t timeout_ms_{10000};
  bool finished_{false};
};

}  // namespace tion_dp122_gate
}  // namespace esphome
