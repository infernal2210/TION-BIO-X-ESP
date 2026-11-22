# TION Bio X — MQTT firmware

Independent MQTT firmware for ESP8266 ESP-01 (1 MB Flash).

## Build environments

- `tion_biox_lite` — basic TION Bio X controls.
- `tion_biox_full` — extended controls and air-quality datapoints.

## Build

Install PlatformIO, open this directory as the PlatformIO project, then build:

```sh
pio run -e tion_biox_lite
pio run -e tion_biox_full
```

The current commit contains only the initial build scaffold. Device communication,
Wi-Fi provisioning, web setup, MQTT entities and Home Assistant Discovery are
implemented and tested in subsequent stages.

## Hardware

- Board: ESP-01, 1 MB Flash
- UART: 9600 baud, 8 data bits, no parity, 1 stop bit
- ESP TX (GPIO1) → TION RX
- ESP RX (GPIO3) → TION TX

The UART is reserved for TION communication. Do not enable ordinary serial
logging while connected to the appliance.

See [hardware](../docs/hardware.md) and
[MQTT architecture](../docs/mqtt-architecture.md).
