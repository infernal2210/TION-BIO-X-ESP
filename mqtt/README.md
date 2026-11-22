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

## OTA updates

The ESP-01 1 MB build uses `eagle.flash.1m64.ld` and an OTA-capable application layout. OTA starts automatically after Wi-Fi connects and does not require an OTA password. This is convenient for a trusted, isolated home LAN, but any device with network access to the ESP-01 may attempt a firmware update.

After installing this firmware once over USB, update it over the local network with:

```powershell
python -m platformio run -e tion_biox_lite_ota -t upload --upload-port 192.168.1.xxx
```

Replace the IP with the address shown on the device page or in the router. The regular `tion_biox_lite` environment remains the USB upload/build environment. The actual image size must be checked in the PlatformIO build output before the first OTA update.

## First boot setup

No Wi-Fi or MQTT credentials are compiled into the firmware. On first boot
(or when no valid settings are stored), the device starts the setup access
point:

- SSID: `TION-BIOX-01`
- Password: `tionbiox01`
- Setup page: `http://192.168.4.1/`

The page scans nearby Wi-Fi networks and also allows manual SSID entry.
Enter the Wi-Fi password and MQTT host/port/username/password, then save. On later visits, leave Wi-Fi and MQTT password fields blank to retain the saved passwords. Set a separate OTA password to enable wireless firmware updates.
The device restarts and connects using the stored settings. The default
broker is `192.168.1.10:1883`. If the saved network cannot be reached after
five 15-second attempts, the setup AP starts while the device continues
retrying the saved network every 30 seconds. When Wi-Fi reconnects, the setup
AP is disabled.

The same status/configuration page is available at the device's current LAN
IP while connected. Configuration is stored in EEPROM emulation with a
checksum and version marker. Passwords are stored unencrypted in Flash.
The setup AP password is a fixed initial password; change it in source
before deployment in an untrusted environment.

## MQTT telemetry

- Base topic: `tion_biox/tion_biox_01/`
- Availability: `availability` (retained; Last Will = `offline`)
- State: `state/dp_<id>` (retained numeric datapoint values)
- Home Assistant Discovery prefix: `homeassistant`

Home Assistant Discovery is published as soon as the ESP connects to
the MQTT broker; it does not wait for the TION UART handshake. The device
therefore appears in Home Assistant while the ESP is still on the USB bench.

Diagnostic entities report Wi-Fi RSSI, ESP IP address, UART initialization
state, valid UART frames, checksum errors, framing errors, and free heap.
TION datapoint entities are added when the appliance reports datapoints.
At this diagnostic stage, datapoints are exposed as generic numeric sensors
named `TION DP <id>`; these are raw Tuya values, not human-friendly controls.
No MQTT commands are accepted and no control datapoints are transmitted.

## Hardware

- Board: ESP-01, 1 MB Flash
- UART: 9600 baud, 8N1
- ESP TX (GPIO1) → TION RX
- ESP RX (GPIO3) → TION TX

UART is reserved for TION communication. Do not use Serial Monitor or
ordinary serial logging while connected to the appliance.

See [hardware](../docs/hardware.md) and
[MQTT architecture](../docs/mqtt-architecture.md).
