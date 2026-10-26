# CYD Controller v1

A touchscreen multi-device controller for the **Sunton ESP32-2432S022C** CYD (240x320 display, CST816S touch).

This project is the companion controller for the ESP32-S3 Wi-Fi Internet Radio:

- Radio project: https://github.com/LightNeO/esp-32-internet-radio
- Controller project: https://github.com/LightNeO/CYD_Controller
- Target radio IP used by this version: `192.168.50.52`

The controller sends HTTP commands to the radio. It does not receive or decode the radio audio itself.

## Features

- Dark/cyan touchscreen UI.
- Home screen with device tiles: Radio, Music, Lights, Other.
- Radio screen with station name, animated equalizer, and controls:
  - Previous station
  - Play/Pause
  - Next station
  - Volume −
  - OFF/ON software sleep toggle
  - Volume +
- The controls are arranged as two rows of three square buttons:
  - `Previous | Play/Pause | Next`
  - `Volume − | OFF/ON | Volume +`
- The equalizer uses quick independent pseudo-random bar jumps while the radio is playing and drops to the minimum when stopped, sleeping, or offline.
- Radio station list is deliberately not duplicated in the controller. Previous/Next use the radio API and the radio remains the source of truth.
- Wi-Fi setup through WiFiManager.
- Saved Wi-Fi credentials persist in ESP32 flash.

## Hardware

This version targets the Sunton `ESP32-2432S022C`:

- ESP32-WROOM-32
- 240x320 ST7789 display
- CST816S capacitive touch
- USB serial connection

The display/touch pin configuration is provided by the local board definition in `boards/`.

## Project layout

```text
CYD_Controller_v1/
├── boards/                 # PlatformIO Sunton board definitions/submodule
├── include/lv_conf.h      # LVGL configuration
├── src/main.cpp           # Controller firmware
├── platformio.ini         # Build/upload configuration
└── README.md
```

## Requirements

- Windows, macOS, or Linux
- VS Code + PlatformIO, or PlatformIO CLI
- USB data cable
- 2.4 GHz Wi-Fi network shared by the CYD and the radio
- ESP32-S3 radio already running the companion firmware

## Build and flash

Open this directory in PlatformIO. The configured board is:

```text
esp32-2432S022C
```

Build:

```bash
pio run
```

Connect the CYD and identify its serial port. In the current development setup it was `COM5`. Upload:

```bash
pio run --target upload --upload-port COM5
```

The upload port is configured in `platformio.ini` and can be changed if Windows assigns another COM port.

## Wi-Fi setup

On boot, the controller first tries the Wi-Fi credentials saved by WiFiManager.

If no saved network is available, it opens an access point:

```text
CYD-Remote-Setup
```

1. Connect a phone or computer to `CYD-Remote-Setup`.
2. Open `http://192.168.4.1` if the captive portal does not open automatically.
3. Select the home Wi-Fi network and enter its password.
4. Save and let the controller reboot/connect.

The controller and radio must be on the same LAN. The radio is currently reserved at:

```text
http://192.168.50.52/
```

The controller's address is shown in the serial monitor. In the reference setup it was `192.168.50.218`, but it may change unless reserved in the router.

## Radio API used

The controller calls these endpoints on `http://192.168.50.52`:

The radio's current volume range is `0..21`. The `Volume −` and `Volume +` buttons call the one-step volume endpoints; the actual volume state remains stored and managed by the radio.

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Read station, playing and sleeping state |
| `GET /api/prev` | Previous station |
| `GET /api/next` | Next station |
| `GET /api/toggle` | Toggle play/stop |
| `GET /api/vdown` | Decrease volume by one step |
| `GET /api/vup` | Increase volume by one step |
| `GET /api/sleep` | Stop audio/display and keep Wi-Fi alive |
| `GET /api/wake` | Wake the radio |

The radio status is polled while the Radio screen is open. The equalizer is a UI animation driven by the `playing`, `sleeping`, and online fields; it is not an audio spectrum analyzer.

## Troubleshooting

### Radio shows offline

- Confirm the radio is powered and connected.
- Open `http://192.168.50.52/api/status` from a device on the same Wi-Fi.
- Check that the radio IP reservation is still `192.168.50.52`.
- Check the CYD serial log for its assigned IP and Wi-Fi status.

### Wi-Fi network is not found

- Make sure the router broadcasts 2.4 GHz Wi-Fi.
- Reboot the CYD and wait for the setup AP.
- Use `CYD-Remote-Setup` and configure the network again.

### Upload fails

- Close Serial Monitor before uploading.
- Check the COM port in Device Manager.
- Use a USB data cable.
- Hold BOOT/reset during the PlatformIO connection attempt if the board does not enter download mode automatically.

## Development notes

The previous MP3/A2DP player implementation is retained in `src/main.cpp.full-backup` for reference. The current v1 firmware is an HTTP touchscreen remote and intentionally does not use the SD card, MP3 decoder, or Bluetooth A2DP audio path.

## License

See the repository license and the licenses of the included board and library dependencies.
