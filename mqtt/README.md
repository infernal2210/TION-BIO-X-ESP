# TION Bio X — MQTT firmware

Independent MQTT firmware for ESP8266 ESP-01 (1 MB Flash).

## Build environments

- `tion_biox_lite` — basic TION Bio X profile.
- `tion_biox_full` — extended air-quality profile.

## Build

```sh
pio run -e tion_biox_lite
pio run -e tion_biox_full
```

## Local Wi-Fi credentials

The real credentials file is intentionally excluded from Git. Create
`mqtt/include/secrets.h` locally (do not commit it):

```cpp
#pragma once
#define TION_WIFI_SSID "YOUR_WIFI_NAME"
#define TION_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define TION_MQTT_USERNAME "YOUR_MQTT_USER"
#define TION_MQTT_PASSWORD "YOUR_MQTT_PASSWORD"
```

The MQTT broker defaults to `192.168.1.10:1883`. Device identity and
Home Assistant discovery prefix are in `include/app_config.h`.
The current firmware stage uses compile-time Wi-Fi/MQTT credentials;
the captive-portal provisioning interface is a later stage.

## MQTT telemetry

- Base topic: `tion_biox/tion_biox_01/`
- Availability: `availability` (retained; Last Will = `offline`)
- State: `state/dp_<id>` (retained numeric datapoint values)
- Home Assistant Discovery prefix: `homeassistant`

At this diagnostic stage, Home Assistant entities are exposed as generic
numeric sensors named `TION DP <id>`. These are raw Tuya datapoint values,
not yet human-friendly controls. No MQTT commands are accepted and no
control datapoints are transmitted to the appliance.

## Hardware

- Board: ESP-01, 1 MB Flash
- UART: 9600 baud, 8N1
- ESP TX (GPIO1) → TION RX
- ESP RX (GPIO3) → TION TX

UART is reserved for TION communication. Do not use Serial Monitor or
ordinary serial logging while connected to the appliance.

See [hardware](../docs/hardware.md) and
[MQTT architecture](../docs/mqtt-architecture.md).
