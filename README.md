# ESP_VenusBridge

Read and control a **Marstek Venus** plug-in battery from the **homee** smart home system – locally, without cloud – using a small ESP32/ESP8266 board as a bridge.

**Current version: 0.09** – see the [Changelog](#changelog).

---

## Disclaimer

**Use at your own risk.** This is a private, non-commercial hobby project. The software is provided "as is", without warranty of any kind. To the maximum extent permitted by applicable law, the author accepts no liability for any damage to property or persons, financial loss or any other damage arising from the use or misuse of this software, the related documentation or the hardware it is used with.

- The bridge can change the operating mode of your battery storage system. Always follow the safety instructions and operating conditions of the battery manufacturer.
- This project is **not affiliated with, endorsed or supported by** Marstek or homee. All product names and trademarks are the property of their respective owners.
- See the [LICENSE](LICENSE) file (MIT) for the full terms.

---

## Table of contents

1. [How it works](#how-it-works)
2. [Supported batteries](#supported-batteries)
3. [Prerequisites](#prerequisites)
4. [Hardware](#hardware)
5. [Flashing the firmware](#flashing-the-firmware)
6. [First setup and access point mode](#first-setup-and-access-point-mode)
7. [Settings](#settings)
8. [homee integration](#homee-integration)
9. [Web interface and HTTP API](#web-interface-and-http-api)
10. [Status LED](#status-led)
11. [Troubleshooting](#troubleshooting)
12. [Changelog](#changelog)
13. [Acknowledgements](#acknowledgements)
14. [License](#license)

---

## How it works

```
 Marstek Venus  <── UDP / JSON-RPC (Open API) ──>  ESP bridge  <── WebSocket ("homee in homee") ──>  homee
                         port 30000                    │
                                                       └── web interface (port 80) for status & settings
```

1. **Reading the battery** – The bridge polls the battery over the local network using the **Marstek Open API** (JSON-RPC over UDP, default port 30000). Only one request is in flight at a time, with a 3 s gap between requests.
   - Every polling cycle (default 30 s, min. 10 s): `ES.GetStatus` (state of charge, power values, energy counters), `ES.GetMode` (operating mode), `EM.GetStatus` (CT meter).
   - Every 10th cycle: `Marstek.GetDevice` (model, firmware) and `Bat.GetStatus` (temperature, rated capacity).
   - Missing responses are retransmitted; after 3 failed requests in a row the battery is marked as *not reachable*. While it is not reachable, only one probe request per polling interval is sent; as soon as it answers again, normal polling resumes.
2. **Providing the values to homee** – Using the [homee-api-esp32](https://github.com/Oxi75/homee-api-esp32) library, the bridge acts as a *virtual homee* ("homee in homee", port 7681). Your real homee connects to it and shows the battery as a device with the attributes listed in [homee integration](#homee-integration).
3. **Controlling the battery** – Changing the operating mode in homee (Self-consumption / AI optimization / UPS) is forwarded to the battery via `ES.SetMode` and read back for verification.
4. **Web interface** – A built-in web page shows all live values and lets you configure WiFi, the battery and homee, and update the firmware.

All settings are stored on the ESP in `/config.json` (LittleFS). The passwords (WiFi and admin password) are **not** part of this file; they are stored XOR-obfuscated in the (emulated) EEPROM. This is obfuscation, not strong encryption – it keeps the passwords out of plain sight, but cannot protect them against someone with physical access to the board. No data leaves your local network.

---

## Supported batteries

- Tested **only** with the **Marstek Venus E mini**. No other battery has been tested so far.
- Other batteries of the **Marstek Venus series** should work as well, **as long as they support the Marstek Open API**.

---

## Prerequisites

- **Marstek app** – The official Marstek app is still required for the **initial setup** of the battery (WiFi connection, firmware updates etc.). The bridge does not replace the app.
- **Open API enabled** – The **Open API must be enabled in the Marstek app** for the battery. Note the UDP port shown there (default `30000`).
- **Fixed IP address of the battery** – recommended (e.g. DHCP reservation in your router), because the bridge addresses the battery by IP.
- **homee** – a homee Brain/Cube with support for "homee in homee".
- **2.4 GHz WiFi** – the ESP boards do not support 5 GHz.
- **PlatformIO** (e.g. as VS Code extension) to build and flash the firmware.

---

## Hardware

| | ESP32-C3 SuperMini | ESP8266 ESP-12E |
|---|---|---|
| **Status** | ✅ tested | ⚠️ prepared, **not tested** |
| PlatformIO environment | `esp32c3-supermini` | `esp12e` |
| Status LED | GPIO8 (active low, on-board) | GPIO16 (active low) |
| Configuration button | BOOT button (GPIO9, on-board) | push button GPIO12 → GND (internal pull-up) |
| Button at power-on | ❌ not possible (starts the bootloader) | ✅ forces access point mode |
| Button held ≥ 3 s during operation | ✅ starts access point mode | ✅ starts access point mode |
| Flash | 4 MB | 4 MB |
| Notes | WiFi TX power reduced to 8.5 dBm (these boards are unstable at full power) | — |

No further wiring is required: the bridge only needs power (USB 5 V) and WiFi. It communicates with the battery and homee over the network.

---

## Flashing the firmware

### Via USB (first installation)

```sh
# ESP32-C3 SuperMini (default environment)
pio run -e esp32c3-supermini -t upload

# ESP8266 ESP-12E
pio run -e esp12e -t upload
```

Serial monitor: `pio device monitor` (115200 baud).

### Via WiFi (OTA from PlatformIO)

Once the bridge is connected to your WiFi:

```sh
pio run -e esp32c3-supermini-ota -t upload
pio run -e esp12e-ota -t upload
```

`upload_port` is `venusbridge.local` by default. If you changed the hostname or mDNS does not work in your network, set `upload_port` in `platformio.ini` to the hostname or IP address of the bridge.

If an **admin password** is set in the settings, OTA uploads from PlatformIO require it as well. Uncomment and fill in the line in the OTA environment of `platformio.ini`:

```ini
upload_flags = --auth=<admin password>
```

### Via the web interface (OTA)

> ⚠️ The firmware update via the web interface has **not been tested** yet.

Build the firmware (`pio run -e <env>`), open **Settings → Firmware update** in the web interface, choose `.pio/build/<env>/firmware.bin` and click **Upload**. The bridge restarts automatically. If an admin password is set, enter it in the *Current admin password* field first.

---

## First setup and access point mode

In access point (AP) mode the bridge opens its own WiFi network, in which you can configure it.

| | |
|---|---|
| WiFi name (SSID) | `VenusBridge-XXXX` (XXXX = part of the chip ID) |
| Password | `venusbridge` |
| Address | `http://192.168.4.1` |

After connecting, most devices open the settings page automatically (captive portal). Otherwise open `http://192.168.4.1` in a browser.

### When does the bridge start the access point?

**Automatically:**
- on the very first start (no WiFi configured yet),
- if the first connection to the configured WiFi fails for more than **30 seconds** after power-on,
- if an existing WiFi connection is lost for more than **5 minutes**.

In the automatic cases the bridge keeps trying to reconnect to your WiFi in the background. As soon as the connection is back and no device is connected to the access point, the access point is switched off again.

**Manually (e.g. to change settings or after moving to another WiFi):**

- **ESP32-C3 SuperMini:** Hold the **BOOT** button for **at least 3 seconds while the bridge is running**. Do **not** press it while plugging in the board – GPIO9 is a strapping pin, and the ESP32-C3 would start its bootloader instead of the firmware.
- **ESP8266 (ESP-12E):** Either keep the button on **GPIO12** pressed **while powering on**, or hold it for **at least 3 seconds while the bridge is running**.

A manually started access point stays active until the next restart (saving the settings restarts the bridge).

The status LED blinks fast while the access point is active.

### Setup steps

1. Connect to the `VenusBridge-XXXX` WiFi (password `venusbridge`).
2. On the settings page enter your WiFi SSID and password.
3. Enter the IP address of your Marstek battery (and the UDP port, if you changed it in the Marstek app).
4. Optional: adjust hostname, virtual homee name and homee device name.
5. Click **Save & restart**. The bridge connects to your WiFi.
6. Open `http://venusbridge.local` (or the IP address assigned by your router) to see the status page.
7. Add the bridge to homee (see below).

---

## Settings

| Field | Default | Description |
|---|---|---|
| **WiFi** | | |
| SSID | – | Name of your 2.4 GHz WiFi (required). |
| WiFi password | – | WiFi password. Leave empty to keep the stored password. It is never shown again. |
| Hostname | `venusbridge` | Network name of the bridge (mDNS `venusbridge.local`, OTA). |
| IP address | automatic (DHCP) | Choose *static* to enter IP, gateway, subnet mask and DNS manually. If DNS is empty, the gateway is used. |
| **Marstek Venus** | | |
| Battery IP address | – | IP address of the battery in your network. Without it, nothing is polled. |
| UDP port (Open API) | `30000` | Must match the port shown in the Marstek app. |
| Polling interval | `30` s | Time between two polling cycles (10 – 3600 s). |
| **homee** | | |
| virtual homee name | `venusbridge` | Name of the virtual homee; entered as **homee ID** when pairing with homee. |
| homee device name | `Venus E` | Name of the battery device shown in homee. |
| **Security** | | |
| Admin password | – (no protection) | Protects **Save & restart** and the **firmware update** (web interface and PlatformIO OTA). Either empty (no protection) or 5 – 32 characters. Leave empty to keep the stored password; enter it twice to change it. Tick *Remove admin password* to switch the protection off. |

### Buttons

| Button | Password required | Description |
|---|---|---|
| **Save & restart** | ✅ (if set) | Stores the settings and restarts the bridge. |
| **Restart** | ❌ | Restarts the bridge without saving. |
| **Download config** | ❌ | Downloads the saved settings as `venusbridge-config.json`. The file contains **no passwords**. |
| **Upload config** | ❌ | Loads a previously downloaded file into the form fields. Nothing is saved until you click **Save & restart**. Values missing in the file (e.g. from an older backup) are ignored and the corresponding fields keep their current value; an invalid file changes nothing. |

If an admin password is set, the settings page shows a field **Current admin password**, which must be filled in for saving and for the firmware update. After a wrong password, further attempts are rejected for 2 seconds.

**No password is required when the settings page is opened via the access point** (`http://192.168.4.1`). This way you can always regain access with the configuration button, e.g. if you forgot the admin password.

Settings are kept across firmware updates; new parameters introduced by an update are added with their default values.

---

## homee integration

The bridge appears in homee as a **"homee in homee"** connection. The bridge must be connected to the same network as your homee.

Pairing in the homee app (menu names as shown in the German app; the English app uses equivalent terms):

1. Open **"Gerät hinzufügen"** (*Add device*).
2. Select **"Verschiedene"** (*Miscellaneous*) → **"homee in homee"**.
3. Select **"homee verbinden"** (*Connect homee*).
4. Enter the **homee ID** – this is the **virtual homee name** from the bridge settings (default `venusbridge`).
5. Enter any user name and password, e.g. `test` / `test`. They are not checked by the bridge.

For background information see the [homee-api-esp32](https://github.com/Oxi75/homee-api-esp32) library.

After pairing, homee shows one device (default name *Venus E*) with the following attributes. The attribute names are in German, as shown in the homee app:

| Attribute (homee) | Meaning | Unit |
|---|---|---|
| Ladezustand | State of charge | % |
| Batterieleistung | Battery power (+ charging / − discharging) | W |
| Laden | Charging power | W |
| Entladen | Discharging power | W |
| Netzleistung | AC grid power (+ output / − input) | W |
| Energieinhalt | Remaining energy in the battery | kWh |
| Einspeisung gesamt | Total energy delivered to the grid | kWh |
| Bezug gesamt | Total energy taken from the grid | kWh |
| Temperatur | Battery temperature | °C |
| Hausverbrauch (CT) | Total power measured by the CT meter | W |
| Eigenverbrauch | Mode switch: self-consumption | on/off |
| KI-Optimierung | Mode switch: AI optimization | on/off |
| UPS | Mode switch: UPS | on/off |
| Manuell | Mode switch: manual (display only) | on/off |
| Firmware Bridge | Firmware version of the bridge (e.g. 0.07) | – |
| Firmware Speicher | Firmware version of the battery (`ver` from `Marstek.GetDevice`) | – |

### Operating mode

homee cannot show a selection list with custom names, so the operating mode is shown as **four on/off switches that work like radio buttons**. Their order matches the Marstek app:

| # | Switch (homee) | Marstek app | Open API | Can be set from homee |
|---|---|---|---|---|
| 1 | Eigenverbrauch | Self-consumption | `Auto` | ✅ |
| 2 | KI-Optimierung | AI optimization | `AI` | ✅ |
| 3 | UPS | UPS | `UPS` | ✅ |
| 4 | Manuell | Manual | `Manual` | ❌ display only |

- Exactly one switch is on – the one matching the current mode of the battery.
- Turning a switch **on** changes the mode of the battery; the other switches are turned off automatically. If the battery rejects the change, the switches return to the actual mode.
- Turning the active switch **off** is not possible (the battery always has a mode); it is switched back on immediately.
- *Manuell* is **display only**: it is shown when the battery is in manual mode, but cannot be selected from homee – switching it on is reset to the actual mode. Reason: switching to Manual via the Open API **overwrites the schedules configured in the Marstek app**. This was tested in version 0.04, where only the mode was sent without a schedule (`manual_cfg`) – the schedules were overwritten nevertheless. Activate the manual mode in the Marstek app instead.
- A mode change in the Marstek app appears in homee after the next polling cycle.
- In the API-only mode `Passive` all four switches are off.

In homeegrams, use e.g. "KI-Optimierung is switched on" as trigger or "switch on UPS" as action.

---

## Web interface and HTTP API

Open `http://venusbridge.local` or the IP address of the bridge.

- **Status** (`/`) – live values of the battery, CT meter, system information (version, IP, WiFi signal, homee connections, uptime, free memory) and the last raw response of the battery. Refreshes every 5 s.
- **Settings** (`/config`) – see [Settings](#settings) and firmware update.

HTTP endpoints. If an admin password is set, the protected endpoints expect it in the HTTP header `X-Auth` (URL-encoded) and answer `401` for a wrong password and `429` for further attempts within 2 seconds. Requests via the access point need no password. Use the bridge only in a trusted network – the connection is plain HTTP.

| Method | Path | Protected | Description |
|---|---|---|---|
| GET | `/api/status` | ❌ | All live values as JSON |
| GET | `/api/config` | ❌ | Current settings as JSON (without passwords); additionally `adminPwSet` and `authRequired` (password needed for this client) |
| POST | `/api/config` | ✅ | Save settings (form-encoded, same fields as the settings page, plus `adminPw` and `adminPwClear=1`) and restart |
| POST | `/api/restart` | ❌ | Restart the bridge |
| POST | `/update` | ✅ | Firmware upload (multipart, field `firmware`) |

---

## Status LED

| LED | Meaning |
|---|---|
| on | Everything OK – WiFi connected, battery reachable |
| slow blinking (1 s) | WiFi connected, but battery not reachable |
| fast blinking | Access point active |
| short flash every 2 s | No WiFi connection |
| flickering | OTA update in progress |

---

## Troubleshooting

- **Serial log** – Connect via USB and open the serial monitor (115200 baud). All modules log with a prefix: `[NET]` WiFi, `[MST]` Marstek, `[HOM]` homee, `[WEB]` web interface, `[CFG]` settings, `[OTA]` updates.
- **Battery not reachable** – Check that the Open API is enabled in the Marstek app, that the IP address and UDP port are correct and that bridge and battery are in the same network. The *Last response from the battery* field on the status page shows whether any answer arrives.
- **Values stay at 0 / "–"** – Some firmwares return zeros while the battery management system is not responding; the bridge then keeps the last valid values.
- **Older battery firmware** – With battery firmware below version 150 the bridge does not retransmit requests, as these versions handle repeated requests poorly.
- **WiFi unstable (ESP32-C3 SuperMini)** – Place the bridge closer to the access point; the TX power is intentionally reduced on this board.
- **Bridge cannot be found after a WiFi change** – Start the access point manually (see [access point mode](#first-setup-and-access-point-mode)) and enter the new WiFi credentials.

---

## Changelog

### 0.09 (2026-10-18)

- Marstek: while the battery is not reachable, the rest of the polling cycle is skipped, so only one probe request per polling interval is sent instead of the full cycle.
- Marstek: receive buffer for the last raw response reserved in full size (avoids a later reallocation).

### 0.08 (2026-10-18)

- Optional **admin password** protecting *Save & restart* and the firmware update (web interface and PlatformIO OTA via `--auth`). Not required when the settings page is opened via the access point.
- Passwords (WiFi and admin) are now stored XOR-obfuscated in the EEPROM instead of `/config.json`. An existing WiFi password is moved automatically on the first start after the update.
- Settings page: new buttons **Restart** (without saving), **Download config** and **Upload config** (backup/restore of the settings as JSON, without passwords).

### 0.07 (2026-10-17)

- homee: new attributes *Firmware Bridge* and *Firmware Speicher* (firmware versions of the bridge and the battery). Remove the device in homee and pair it again to see them.
- README: clarified test status (only tested with Venus E mini, firmware update via the web interface not tested).

### 0.06 (2026-10-17)

- README: added acknowledgements.

### 0.05 (2026-10-17)

- Reverted the experimental change from 0.04: *Manuell* is display only again. Test result: activating Manual via the Open API overwrites the schedules configured in the Marstek app, even without a schedule in the request. Use the Marstek app to activate the manual mode.

### 0.04 (2026-10-17)

- homee: the mode switch *Manuell* can now be switched on (experimental). The bridge sends `ES.SetMode` with mode `Manual` but without a schedule (`manual_cfg`), so the schedules from the Marstek app are kept.

### 0.03 (2026-10-17)

- homee: the operating mode is now shown as four on/off switches with radio-button behaviour (Eigenverbrauch, KI-Optimierung, UPS, Manuell) instead of the mode attribute, which homee displayed as "Überwachungsmodus" without proper value names. **Remove the device in homee and pair it again** after the update so the old attribute disappears.
- Web interface: modes are shown with the names used in the Marstek app.

### 0.02 (2026-10-17)

- Web interface: "virtual homee name" field labelled as homee ID used for pairing.
- README: step-by-step pairing instructions for the homee app.

### 0.01 (2026-10-17)

First public version.

- Polling of the Marstek Venus via the local Open API (UDP JSON-RPC): state of charge, power values, energy counters, operating mode, CT meter, battery temperature.
- Integration into homee as "homee in homee" device, operating mode Auto / AI / UPS settable from homee.
- Web interface with status page, settings and firmware upload.
- WiFi with DHCP or static IP, access point with captive portal for configuration.
- OTA updates via PlatformIO (espota).
- Hardware: ESP32-C3 SuperMini (tested), ESP8266 ESP-12E (prepared, not tested).

---

## Acknowledgements

This project was developed with the kind support of [Claude Code](https://claude.com/claude-code) by Anthropic.

---

## License

MIT – see [LICENSE](LICENSE).

Uses the libraries [homee-api-esp32](https://github.com/Oxi75/homee-api-esp32), [ArduinoJson](https://arduinojson.org/) and [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) – see their respective licenses.
