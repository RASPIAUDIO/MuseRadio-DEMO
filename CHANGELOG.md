# Changelog

## 1.3 - 2026-05-04

- Added a fallback captive Wi-Fi setup portal at `http://192.168.4.1` when no saved network connects.
- Display a real Wi-Fi QR code for the fallback `MuseRadio-XXXX` access point on the TFT.
- Added an OK/encoder button shortcut from the captive portal screen to manual credential entry.
- Documented `firmware.with-data.bin`, a single flash image that includes the LittleFS data partition at `0x310000`.
- Defaulted fresh installs without `/mode` to Wi-Fi mode so the captive portal starts on first boot.
- Bumped the displayed firmware version and Improv device version to 1.3.

## 1.2 - 2026-05-04

- Added PlatformIO support for the Muse Radio ESP32-S3 build.
- Moved the build to pioarduino stable with Arduino ESP32 core 3.3.8.
- Updated `ESP32-audioI2S` to 3.4.5.
- Enabled the ESP32-S3 N8R8 board profile for 8 MB flash and 8 MB PSRAM.
- Confirmed PSRAM detection on boot.
- Documented the combined `firmware.factory.bin` image, which must be flashed at address `0x0`.
- Fixed TFT startup on ESP32-S3 by selecting HSPI and enabling the backlight.
- Fixed ES8388 output with the new audio library by passing MCLK on GPIO0 to `audio.setPinout`.
- Migrated audio metadata handling to the new `Audio::audio_info_callback` API.
- Adapted bundled compatibility libraries for Arduino ESP32 3.x timer, Wi-Fi, and GPIO register changes.
- Disabled legacy factory I2S audio tests by default because the old I2S driver conflicts with the new ESP-IDF 5 I2S driver.
