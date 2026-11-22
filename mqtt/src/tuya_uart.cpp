#include "tuya_uart.h"

namespace {
constexpr uint8_t kHeader1 = 0x55;
constexpr uint8_t kHeader2 = 0xAA;

constexpr uint8_t kHeartbeat = 0x00;
constexpr uint8_t kProductQuery = 0x01;
constexpr uint8_t kConfigQuery = 0x02;
constexpr uint8_t kWifiState = 0x03;
constexpr uint8_t kDatapointDeliver = 0x06;
constexpr uint8_t kDatapointReportAsync = 0x07;
constexpr uint8_t kDatapointQuery = 0x08;
constexpr uint8_t kDatapointReportSync = 0x22;
constexpr uint8_t kDatapointReportAck = 0x23;
}

TuyaUart::TuyaUart(HardwareSerial &serial) : serial_(serial) {}

void TuyaUart::begin(uint32_t baud, DatapointCallback callback) {
  callback_ = callback;
  serial_.begin(baud, SERIAL_8N1);
  reset();
  init_state_ = InitState::WAIT_HEARTBEAT;
  initialized_ = false;
  last_heartbeat_ms_ = millis();
}

void TuyaUart::loop() {
  while (serial_.available() > 0) {
    consume(static_cast<uint8_t>(serial_.read()));
  }

  const uint32_t now = millis();
  if (now - last_heartbeat_ms_ >= kHeartbeatIntervalMs) {
    sendEmptyCommand(kHeartbeat);
    last_heartbeat_ms_ = now;
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
    if (byte == kHeader1) frame_[used_++] = byte;
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
  const uint8_t *payload = &frame_[6];

  switch (command) {
    case kHeartbeat:
      if (init_state_ == InitState::WAIT_HEARTBEAT) {
        init_state_ = InitState::WAIT_PRODUCT;
        sendEmptyCommand(kProductQuery);
      }
      break;

    case kProductQuery:
      if (init_state_ == InitState::WAIT_PRODUCT) {
        init_state_ = InitState::WAIT_CONFIG;
        sendEmptyCommand(kConfigQuery);
      }
      break;

    case kConfigQuery:
      if (init_state_ == InitState::WAIT_CONFIG) {
        init_state_ = InitState::WAIT_WIFI_STATE;
        sendCommand(kWifiState, &wifi_status_, 1);
      }
      break;

    case kWifiState:
      if (init_state_ == InitState::WAIT_WIFI_STATE) {
        init_state_ = InitState::WAIT_DATAPOINTS;
        sendEmptyCommand(kDatapointQuery);
      }
      break;

    case kDatapointReportAsync:
    case kDatapointReportSync:
      processDatapoints(payload, length);
      if (command == kDatapointReportSync) {
        const uint8_t ack = 0x01;
        sendCommand(kDatapointReportAck, &ack, 1);
      }
      if (init_state_ == InitState::WAIT_DATAPOINTS) {
        init_state_ = InitState::READY;
        initialized_ = true;
      }
      break;

    default:
      break;
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

void TuyaUart::sendEmptyCommand(uint8_t command) {
  sendCommand(command);
}

void TuyaUart::sendCommand(uint8_t command, const uint8_t *payload,
                           uint16_t length) {
  if (length > kMaxPayload || (length > 0 && payload == nullptr)) return;

  const uint8_t len_hi = static_cast<uint8_t>(length >> 8);
  const uint8_t len_lo = static_cast<uint8_t>(length & 0xFF);

  // ESPHome/Tuya TX checksum: header + command + length + payload.
  // The version byte is intentionally excluded for outgoing frames.
  uint8_t checksum = static_cast<uint8_t>(
      kHeader1 + kHeader2 + command + len_hi + len_lo);

  serial_.write(kHeader1);
  serial_.write(kHeader2);
  serial_.write(static_cast<uint8_t>(0x00));
  serial_.write(command);
  serial_.write(len_hi);
  serial_.write(len_lo);

  for (uint16_t i = 0; i < length; ++i) {
    serial_.write(payload[i]);
    checksum = static_cast<uint8_t>(checksum + payload[i]);
  }

  serial_.write(checksum);
  serial_.flush();
}
