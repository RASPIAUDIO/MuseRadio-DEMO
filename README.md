# Muse Radio Demo

Internet radio demo project for the [Muse Radio](https://raspiaudio.com/product/muse-radio/) by RASPIAUDIO.

The demo supports Wi-Fi streaming, the integrated TFT display, hardware buttons, IR remote control, OTA support, ES8388 codec output, headphone/speaker switching, and battery monitoring.

## Features

- Wi-Fi streaming with MP3, AAC, and common internet radio formats.
- TFT display UI with encoder and button controls.
- IR remote support.
- OTA update support.
- ES8388 codec output with speaker and headphone routing.
- Battery level display.
- Multiple saved Wi-Fi credentials through LittleFS `/wifi.json`.

## Source Code and Discussion

- Source code: [github.com/RASPIAUDIO/MuseRadio-DEMO](https://github.com/RASPIAUDIO/MuseRadio-DEMO)
- Discussion forum: [Muse Radio demo app for internet radio update](https://forum.raspiaudio.com/t/muse-radio-demo-app-fo-internet-radio-update/1214)

## Installation

1. Visit [apps.raspiaudio.com](https://apps.raspiaudio.com) with Chrome.
2. Power on the Muse Radio and connect it over USB.
3. Select "Muse Radio - Radio".
4. Click "Connect" and choose the serial port.
5. Configure Wi-Fi credentials in the browser or through the on-screen menu.

For installation or usage issues, open an issue on [GitHub](https://github.com/RASPIAUDIO/MuseRadio-DEMO/issues).

## Release 1.2

- Added PlatformIO support for ESP32-S3 Muse Radio builds.
- Updated the build to pioarduino / Arduino ESP32 core 3.3.8.
- Updated `ESP32-audioI2S` to 3.4.5.
- Enabled the ESP32-S3 N8R8 profile for 8 MB flash and 8 MB PSRAM.
- Fixed TFT startup on ESP32-S3 by selecting the HSPI port and restoring the display backlight.
- Fixed ES8388 audio output with the new audio library by explicitly routing MCLK on GPIO0.
- Adapted bundled compatibility libraries for Arduino ESP32 3.x.
- Disabled legacy factory I2S audio tests by default because the old I2S driver conflicts with the ESP-IDF 5 I2S driver used by `ESP32-audioI2S`.

## Recent Changes

- Multiple Wi-Fi credentials are stored in LittleFS at `/wifi.json` and loaded into `WiFiMulti` on boot.
- The Wi-Fi failure menu can retry, forget a saved network, or add a new network.
- Long Wi-Fi passwords wrap onto two lines for readability.
- `USBSerial` falls back to `Serial` when native USB CDC is not enabled.

## Development Setup

Required tools and target:

- PlatformIO Core 6.1.19 or newer.
- pioarduino stable `platform-espressif32`.
- Arduino ESP32 core 3.3.8.
- ESP32-audioI2S 3.4.5.
- ESP32-S3 with 8 MB flash and 8 MB PSRAM.
- PlatformIO board profile: `esp32-s3-devkitc1-n8r8`.

The repository includes a `platformio.ini` configured for the Muse Radio ESP32-S3 build. On Windows, use `python -m platformio` if another older `platformio.exe` is first in `PATH`.

```bash
python -m platformio run -e muse_radio
python -m platformio run -e muse_radio -t upload --upload-port COM5
python -m platformio run -e muse_radio -t uploadfs --upload-port COM5
```

PlatformIO also generates a combined binary at `.pio/build/muse_radio/firmware.factory.bin`. This "total" binary contains the bootloader, partition table, OTA boot app, and firmware image, and must be flashed at address `0x0`.

```bash
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 .pio/build/muse_radio/firmware.factory.bin
```

The LittleFS data directory is `RadioV01/data`. If upload does not start automatically, press the hidden IO0 button with a paperclip inserted in the headphone jack.

## Notes

- PSRAM is required by `ESP32-audioI2S` 3.4.5.
- The ES8388 codec needs MCLK on GPIO0 with the current audio library.
- Legacy factory I2S tests can be re-enabled with `ENABLE_LEGACY_FACTORY_I2S=1`, but they are incompatible with the new audio driver path and are disabled by default.
