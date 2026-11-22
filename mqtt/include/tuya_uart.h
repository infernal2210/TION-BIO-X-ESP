#pragma once

#include <Arduino.h>

class TuyaUart {
 public:
  using DatapointCallback = void (*)(uint8_t id, uint8_t type,
                                     const uint8_t *data, uint16_t length);

  explicit TuyaUart(HardwareSerial &serial);
  void begin(uint32_t baud, DatapointCallback callback);
  void loop();

  uint32_t validFrames() const { return valid_frames_; }
  uint32_t checksumErrors() const { return checksum_errors_; }
  uint32_t framingErrors() const { return framing_errors_; }

 private:
  static constexpr uint16_t kMaxPayload = 240;
  static constexpr uint16_t kFrameSize = 7 + kMaxPayload;

  HardwareSerial &serial_;
  DatapointCallback callback_ = nullptr;
  uint8_t frame_[kFrameSize] = {};
  uint16_t used_ = 0;
  uint16_t expected_ = 0;
  uint32_t valid_frames_ = 0;
  uint32_t checksum_errors_ = 0;
  uint32_t framing_errors_ = 0;

  void consume(uint8_t byte);
  void processFrame();
  void processDatapoints(const uint8_t *payload, uint16_t length);
  void reset();
};
