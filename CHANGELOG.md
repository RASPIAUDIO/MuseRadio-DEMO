# Changelog

## 1.8-display-poc - Unreleased

- Added isolated `muse_radio_usb_display_poc` PlatformIO environment with `ENABLE_USB_AUDIO=1` and `ENABLE_USB_DISPLAY=1`.
- Added a TinyUSB vendor display interface alongside UAC speaker output for a Windows USB extended-screen POC.
- Added `UsbDisplayService` with PSRAM frame buffers, JPEG/RGB565 frame receive, 320x240 landscape validation, TFT rendering, frame-drop counters, and display inactivity events.
- The display POC uses VID `0x303A`, PID `0x2986`, and vendor interface string `esp32s3udisp0_R320x240_Ejpg4_Fps10_Bl65536` for the Espressif Windows driver path.
- Raised the USB display POC target from 5 FPS to 10 FPS while keeping audio-first frame dropping.
- Fixed USB display color rendering by pushing RGB565 frames with the byte order expected by the TFT_eSPI ST7789 path.
- Removed the local `USB Display` status overlay so Windows-rendered frames are not covered by a black banner.
- Reverted the mono USB audio test after issue research showed similar UAC glitches in mono, and kept 16-bit / 44.1 kHz stereo for Windows compatibility.
- Bypassed the raw PCM software gain/EQ path so USB, Spotify, and AirPlay PCM keep their source level while volume remains codec-driven through the ES8388.
- Corrected the local UAC speaker cadence for 44.1 kHz with fractional frame reads, restored the default 10 ms UAC interval, and fixed the new-play timeout unit.
- Documented the UAC distortion root cause: integer 44.1 kHz consumption drifted to 44.0 kHz and created buffer pressure.
- Added USB audio diagnostics for UAC bytes, buffered milliseconds, drops, underruns, I2S write latency, and host volume.
- Lowered USB display rendering priority and drop frames when the USB audio buffer is below the safe threshold.
- USB display activity stops internet radio and keeps the backlight on; radio resumes after display inactivity with the existing guard-delay pattern.
- Documented the Espressif Windows driver path in `tools/windows_usb_display_driver/README.md`.

## 1.7 - 2026-05-07

- Added Windows-first USB Audio Class output to the main `muse_radio` firmware with `ENABLE_USB_AUDIO=1`.
- Added `UsbAudioService` around Espressif `usb_device_uac` 1.2.3 and TinyUSB UAC speaker output.
- Vendorized `usb_device_uac` locally and patched only the CMake descriptor build path needed by PlatformIO.
- The UAC profile is output-only PCM stereo, 16-bit, 44.1 kHz; ES8388 microphone/UAC input is deferred.
- USB product descriptor is `Muse Radio`; endpoint naming remains Windows-dependent.
- USB PCM is queued from the UAC OUT callback and written through the existing `Audio::writeRawPCM16(..., 44100, 2)` path, avoiding a second I2S TX driver.
- Hardened USB audio against transient crackles by moving UI/radio switching out of the UAC callback, lowering UAC bandwidth from 48 kHz to 44.1 kHz, increasing the UAC speaker interval to 50 ms, adding a 50 ms preroll, aggregating USB packets into 20 ms I2S writes, and logging drop/underrun counters.
- USB Audio now initializes before Wi-Fi setup so Windows can enumerate the sound card even when Wi-Fi falls back to the captive portal.
- Disabled the Improv serial/USB CDC startup path in the USB Audio build because the native USB link is owned by UAC.
- Internet radio stops when USB audio becomes active and resumes after a short inactivity/disconnect guard.
- Windows host volume and mute events now map to the ES8388 codec volume/mute path instead of software PCM attenuation.
- Added a minimal TFT `USB Audio` status screen.
- Refactored PlatformIO environments so the main firmware builds as Arduino + ESP-IDF with USB Audio, while Spotify and AirPlay POC environments remain isolated.
- Bumped the displayed firmware version and Improv device version to 1.7.

## 1.6 - 2026-05-06

- Added an isolated `muse_radio_airplay_poc` PlatformIO environment with `ENABLE_AIRPLAY=1`.
- Added an experimental AirPlay 1/RAOP receiver POC; this is source-available, unofficial, and not Apple/MFi certified.
- Added `AirPlayService` with ready, active, inactive, metadata, and volume events.
- Added a local `muse_airplay_raop` component for RAOP/RTSP, RTP, ALAC decode, mDNS advertisement, and PCM output.
- Added `GET /info` plist handling, numeric RTSP session IDs, and raw L16/44.1 kHz PCM receive support so `pyatv` can stream AirPlay 1 test audio from Windows without iTunes.
- Added `tools/airplay_sine_test.ps1` to scan the Muse RAOP endpoint and stream a generated sine wave with pinned `pyatv==0.17.0`.
- Reused the existing raw 44.1 kHz / 16-bit stereo PCM path in `ESP32-audioI2S` so AirPlay shares the Muse ES8388 I2S driver.
- Added a minimal TFT `AirPlay` screen with title and artist metadata when the sender provides it.
- AirPlay volume now drives the ES8388 codec output and radio playback resumes after AirPlay disconnects.
- Fixed the AirPlay POC boot crash caused by RAOP/RTP static tasks using an invalid default core ID on ESP-IDF 5.5.
- Hardened the Wi-Fi setup portal and manual fallback against empty scans and oversized manual password input.
- Kept AirPlay 2 out of scope for this release because current ESP32 AirPlay 2 options need separate licensing and integration review.
- Isolated the `cspot` CMake build so Spotify-specific sources are only built in `muse_radio_spotify_poc`.
- Added `bin/MuseRadio-DEMO-V1.6-airplay-poc-app.bin` for data-preserving updates and `bin/MuseRadio-DEMO-V1.6-airplay-poc-factory-full.bin` for full factory flashes with LittleFS included at `0x310000`.
- Bumped the displayed firmware version and Improv device version to 1.6.

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
