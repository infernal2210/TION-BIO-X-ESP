#include "tion_dp122_gate.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tion_dp122_gate {

static const char *const TAG = "tion_dp122_gate";

void TionDP122Gate::setup() {
  this->started_at_ = millis();
  ESP_LOGI(TAG, "Ожидание состояния DP122 перед публикацией сущностей");
}

bool TionDP122Gate::can_proceed() {
  if (this->finished_)
    return true;

  const auto ready = this->ready_();
  if (ready.has_value() && ready.value()) {
    const auto available = this->available_();
    const bool expose = available.has_value() && available.value();

    for (auto *entity : this->entities_)
      entity->set_internal(!expose);

    ESP_LOGI(TAG, "DP122 получен: сущности анализатора %s",
             expose ? "будут опубликованы" : "останутся скрыты");
    this->finished_ = true;
    return true;
  }

  if (millis() - this->started_at_ >= this->timeout_ms_) {
    // All listed entities are declared internal in YAML, so timeout is fail-safe.
    for (auto *entity : this->entities_)
      entity->set_internal(true);

    ESP_LOGW(TAG, "DP122 не получен за %u мс — сущности анализатора скрыты",
             static_cast<unsigned>(this->timeout_ms_));
    this->finished_ = true;
    return true;
  }

  return false;
}

}  // namespace tion_dp122_gate
}  // namespace esphome
