#include "tuya_uart.h"

namespace {
constexpr uint8_t kHeader1 = 0x55;
constexpr uint8_t kHeader2 = 0xAA;
constexpr uint8_t kDpReportAsync = 0x07;
constexpr uint8_t kDpReportSync = 0x22;
}

TuyaUart::TuyaUart(HardwareSerial &serial) : serial_(serial) {}

void TuyaUart::begin(uint32_t baud, DatapointCallback callback) {
  callback_ = callback;
  serial_.begin(baud, SERIAL_8N1);
  reset();
}

void TuyaUart::loop() {
  while (serial_.available() > 0) {
    consume(static_cast<uint8_t>(serial_.read()));
  }
}

void TuyaUart::reset() {
  used_ = 0;
  expected_ = 0;
}

void TuyaUart::consume(uint8_t byte) {
  if (used_ == 0) {
    if (byte == kHeader1) frame_[used_++] = byte;
    return;
  }

  if (used_ == 1) {
    if (byte == kHeader2) {
      frame_[used_++] = byte;
    } else if (byte != kHeader1) {
      reset();
    }
    return;
  }

  if (used_ >= kFrameSize) {
    ++framing_errors_;
    reset();
    return;
  }

  frame_[used_++] = byte;

  if (used_ == 6) {
    const uint16_t payload_length =
        (static_cast<uint16_t>(frame_[4]) << 8) | frame_[5];
    if (payload_length > kMaxPayload) {
      ++framing_errors_;
      reset();
      return;
    }
    expected_ = static_cast<uint16_t>(7 + payload_length);
  }

  if (expected_ != 0 && used_ == expected_) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < expected_ - 1; ++i) checksum += frame_[i];

    if (checksum == frame_[expected_ - 1]) {
      ++valid_frames_;
      processFrame();
    } else {
      ++checksum_errors_;
    }
    reset();
  }
}

void TuyaUart::processFrame() {
  const uint8_t command = frame_[3];
  const uint16_t length =
      (static_cast<uint16_t>(frame_[4]) << 8) | frame_[5];

  if (command == kDpReportAsync || command == kDpReportSync) {
    processDatapoints(&frame_[6], length);
  }
}

void TuyaUart::processDatapoints(const uint8_t *payload, uint16_t length) {
  uint16_t offset = 0;

  while (offset + 4 <= length) {
    const uint8_t id = payload[offset];
    const uint8_t type = payload[offset + 1];
    const uint16_t data_length =
        (static_cast<uint16_t>(payload[offset + 2]) << 8) |
        payload[offset + 3];
    offset += 4;

    if (data_length > length - offset) {
      ++framing_errors_;
      return;
    }

    if (callback_ != nullptr) {
      callback_(id, type, &payload[offset], data_length);
    }
    offset += data_length;
  }

  if (offset != length) ++framing_errors_;
}
