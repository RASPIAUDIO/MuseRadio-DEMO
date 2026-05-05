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
- Captive Wi-Fi setup portal when no saved network connects.
- Optional Spotify Connect POC build through `cspot`.

## Source Code and Discussion

- Source code: [github.com/RASPIAUDIO/MuseRadio-DEMO](https://github.com/RASPIAUDIO/MuseRadio-DEMO)
- Discussion forum: [Muse Radio demo app for internet radio update](https://forum.raspiaudio.com/t/muse-radio-demo-app-fo-internet-radio-update/1214)

## Installation

1. Visit [apps.raspiaudio.com](https://apps.raspiaudio.com) with Chrome.
2. Power on the Muse Radio and connect it over USB.
3. Select "Muse Radio - Radio".
4. Click "Connect" and choose the serial port.
5. Configure Wi-Fi credentials in the browser, through the on-screen menu, or through the fallback captive portal.

If no saved Wi-Fi network connects, the radio starts an access point named `MuseRadio-XXXX` with password `museradio`, shows a Wi-Fi QR code for that access point, and serves a local setup page at `http://192.168.4.1`. Scan the QR code to connect a phone to the radio, open `http://192.168.4.1`, choose a network, enter the password, and the radio saves it in `/wifi.json` before restarting. Press the OK/encoder button on the radio to skip the portal and use the manual on-screen credential entry.

For installation or usage issues, open an issue on [GitHub](https://github.com/RASPIAUDIO/MuseRadio-DEMO/issues).

## Release 1.5

Version 1.5 stabilizes the Spotify Connect POC from 1.4 and keeps the implementation outside the future LVGL UI work.

- Spotify audio playback now resolves CDN streams through Spotify's `spclient` storage-resolve endpoint and parses the protobuf response locally.
- Spotify pause/resume commands are handled by the local `cspot` track player.
- Spotify volume now drives the ES8388 codec output directly in Spotify mode, avoiding software attenuation of PCM samples.
- Radio resume after Spotify disconnects includes a short guard delay to reduce task races while Spotify finishes resetting playback.
- The displayed firmware version is now `V1.5`.

## Release 1.4

Version 1.4 adds an isolated Spotify Connect proof of concept for Muse Radio. The default `muse_radio` environment remains the normal internet radio firmware; Spotify is enabled only in the `muse_radio_spotify_poc` PlatformIO environment with `ENABLE_SPOTIFY_CONNECT=1`.

- Native Spotify Connect receiver POC based on vendored [`cspot`](https://github.com/feelfreelinux/cspot), not the Spotify Web API remote-control wrappers.
- The radio advertises itself on the local Wi-Fi as `Muse Radio-XXXX` through Spotify Connect ZeroConf.
- When Spotify playback starts, internet radio playback stops and the TFT shows a minimal `Spotify Connect` screen.
- When Spotify disconnects, the firmware leaves Spotify mode and lets the last radio station resume.
- Spotify authentication cache is stored in LittleFS at `/spotify_auth.json`; no Spotify user password is stored by the firmware.
- PCM output from `cspot` goes through a new `ESP32-audioI2S` raw 44.1 kHz / 16-bit stereo write path, so the Muse ES8388 I2S driver is shared instead of creating a second I2S driver.
- `cspot` is vendored under `third_party/cspot` from upstream commit `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`; nested `bell` is vendored from `e83737367a08b5a5a1f652a7ecb97a0d926929dd`.
- The POC uses Arduino ESP32 3.3.8 with ESP-IDF 5.5.4 through the pioarduino stable platform.

Spotify Connect requires a Spotify Premium account on the same Wi-Fi network. This is a GPLv3/source-public direction and uses the unofficial `cspot` implementation; Spotify's official commercial hardware eSDK is only available through Spotify's approved partner route.

## Release 1.3

Version 1.3 is the captive-portal Wi-Fi release for Muse Radio. It keeps the Arduino ESP32 3.3.8 / `ESP32-audioI2S` 3.4.5 base from 1.2 and focuses on making Wi-Fi setup easier for end users.

- Captive Wi-Fi setup portal when no saved network connects.
- Real Wi-Fi QR code for the fallback `MuseRadio-XXXX` access point.
- Fallback access point password: `museradio`.
- Local setup page at `http://192.168.4.1` for choosing the target network and saving credentials.
- OK/encoder button shortcut from the captive portal screen to the manual on-screen Wi-Fi entry.
- First-boot default to Wi-Fi mode so a fresh LittleFS image reaches the captive portal without requiring manual setup first.
- Single full-flash binary workflow with `firmware.with-data.bin`, including the LittleFS data partition at `0x310000`.
- Retained PSRAM-enabled ESP32-S3 N8R8 build, TFT startup fix, ES8388 MCLK routing, audio metadata callback migration, and Arduino ESP32 3.x compatibility fixes from 1.2.

## Recent Changes

- Multiple Wi-Fi credentials are stored in LittleFS at `/wifi.json` and loaded into `WiFiMulti` on boot.
- Optional Spotify Connect POC is available in `muse_radio_spotify_poc`.
- When no saved Wi-Fi network connects, the radio starts a captive setup portal at `http://192.168.4.1` and displays a Wi-Fi QR code on the TFT.
- The captive portal screen also offers the OK/encoder button as a shortcut to manual credential entry.
- The displayed firmware version is now `V1.5`.
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
python -m platformio run -e muse_radio_spotify_poc
python -m platformio run -e muse_radio -t upload --upload-port COM5
python -m platformio run -e muse_radio -t uploadfs --upload-port COM5
```

PlatformIO also generates a combined binary at `.pio/build/muse_radio/firmware.factory.bin`. This "total" binary contains the bootloader, partition table, OTA boot app, and firmware image, and must be flashed at address `0x0`.

```bash
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 .pio/build/muse_radio/firmware.factory.bin
```

To include the LittleFS data partition from `RadioV01/data` in the same binary, build the filesystem image and merge it at the `huge_app.csv` data offset `0x310000`:

```powershell
python -m platformio run -e muse_radio -t buildfs
$bootApp0 = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
python -m esptool --chip esp32s3 merge_bin --output .pio/build/muse_radio/firmware.with-data.bin --flash_mode keep --flash_freq 80m --flash_size 8MB 0x0 .pio/build/muse_radio/bootloader.bin 0x8000 .pio/build/muse_radio/partitions.bin 0xe000 $bootApp0 0x10000 .pio/build/muse_radio/firmware.bin 0x310000 .pio/build/muse_radio/littlefs.bin
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 .pio/build/muse_radio/firmware.with-data.bin
```

For the Spotify Connect POC, use the same LittleFS data offset with the `muse_radio_spotify_poc` environment:

```powershell
python -m platformio run -e muse_radio_spotify_poc
python -m platformio run -e muse_radio_spotify_poc -t buildfs
$bootApp0 = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
python -m esptool --chip esp32s3 merge_bin --output .pio/build/muse_radio_spotify_poc/firmware.with-data.bin --flash_mode keep --flash_freq 80m --flash_size 8MB 0x0 .pio/build/muse_radio_spotify_poc/bootloader.bin 0x8000 .pio/build/muse_radio_spotify_poc/partitions.bin 0xe000 $bootApp0 0x10000 .pio/build/muse_radio_spotify_poc/firmware.bin 0x310000 .pio/build/muse_radio_spotify_poc/littlefs.bin
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 .pio/build/muse_radio_spotify_poc/firmware.with-data.bin
```

The LittleFS data directory is `RadioV01/data`. If upload does not start automatically, press the hidden IO0 button with a paperclip inserted in the headphone jack.

## Notes

- PSRAM is required by `ESP32-audioI2S` 3.4.5.
- The ES8388 codec needs MCLK on GPIO0 with the current audio library.
- Spotify Connect POC firmware is close to the 3 MB app partition limit: current clean build is about 95% of `app0`.
- Legacy factory I2S tests can be re-enabled with `ENABLE_LEGACY_FACTORY_I2S=1`, but they are incompatible with the new audio driver path and are disabled by default.
