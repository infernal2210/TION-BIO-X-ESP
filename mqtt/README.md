# TION Bio X — MQTT firmware

Independent MQTT firmware for ESP8266 ESP-01 (512 KB Flash).

## Build environments

- `tion_biox_lite` — basic TION Bio X profile.
- `tion_biox_full` — extended air-quality profile.

## Build

```sh
pio run -e tion_biox_lite
pio run -e tion_biox_full
```

## First boot setup

No Wi-Fi or MQTT credentials are compiled into the firmware. On first boot
(or when no valid settings are stored), the device starts the setup access
point:

- SSID: `TION-BIOX-01`
- Password: `tionbiox01`
- Setup page: `http://192.168.4.1/`

The page scans nearby Wi-Fi networks and also allows manual SSID entry.
Enter the Wi-Fi password and MQTT host/port/username/password, then save. On later visits, leave Wi-Fi and MQTT password fields blank to retain the saved passwords.
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
All datapoint entities supported by the selected profile are discovered immediately, even with the appliance disconnected. Until the first value arrives, their state is `unknown`. The Lite profile publishes Russian names, units, binary sensors, switches, select/number settings, diagnostic entities, reset buttons, and a Home Assistant MQTT climate entity.

Climate modes are `off`, `fan_only`, and `heat`; target temperature is DP114 (0–30 °C), fan speed is DP102 (1–7), current temperature is DP108. Writable controls include sound (DP103), inflow/recirculation (DP107), child lock (DP111), night mode (DP132), panel brightness (DP104), panel color (DP133), filter interval (DP134), and filter counter reset (DP110). Factory reset (DP109) is disabled by default.

MQTT commands are accepted only after the Tuya UART initialization receives a datapoint report. Commands are non-retained; retained command topics are cleared before subscription. State is updated from datapoint reports rather than optimistically.

## Hardware

- Board: ESP-01, 512 KB Flash
- UART: 9600 baud, 8N1
- ESP TX (GPIO1) → TION RX
- ESP RX (GPIO3) → TION TX

UART is reserved for TION communication. Do not use Serial Monitor or
ordinary serial logging while connected to the appliance.

See [hardware](../docs/hardware.md) and
[MQTT architecture](../docs/mqtt-architecture.md).
