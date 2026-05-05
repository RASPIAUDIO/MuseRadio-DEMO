# Changelog

## 1.5 - 2026-05-05

- Stabilized Spotify Connect playback after the initial 1.4 POC.
- Fixed Spotify audio fetching through the Spotify `spclient` storage-resolve endpoint with protobuf CDN URL parsing.
- Added Spotify pause/resume handling in the local `cspot` track player.
- Changed Spotify volume handling to drive the ES8388 codec output directly, avoiding software attenuation of PCM samples.
- Added a short guard before radio resume after Spotify disconnects to reduce I2S/network task races.
- Bumped the displayed firmware version and Improv device version to 1.5.

## 1.4 - 2026-05-05

- Added an isolated `muse_radio_spotify_poc` PlatformIO environment with `ENABLE_SPOTIFY_CONNECT=1`.
- Added a native Spotify Connect POC using vendored `cspot` from `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`, with nested `bell` vendored from `e83737367a08b5a5a1f652a7ecb97a0d926929dd`.
- Added `SpotifyConnectService` wiring for ZeroConf setup, LittleFS auth cache at `/spotify_auth.json`, Spotify active/inactive events, and radio resume after Spotify disconnects.
- Added a minimal TFT `Spotify Connect` status screen; LVGL remains out of scope for this version.
- Added raw 44.1 kHz / 16-bit stereo PCM output to the local `ESP32-audioI2S` copy so Spotify PCM can reuse the Muse ES8388 I2S path.
- Added reproducible nanopb/protobuf generated files for the `cspot` build and PlatformIO/ESP-IDF compatibility shims.
- Bumped the displayed firmware version and Improv device version to 1.4.
- Documented the GPLv3/source-public direction, Spotify Premium requirement, and unofficial `cspot` risk.

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
