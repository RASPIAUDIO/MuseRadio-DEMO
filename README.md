# Muse Radio Demo

Internet radio demo project for the [Muse Radio](https://raspiaudio.com/product/muse-radio/) by RASPIAUDIO.

The demo supports Wi-Fi streaming, USB Audio Class output, the integrated TFT display, hardware buttons, IR remote control, OTA support, ES8388 codec output, headphone/speaker switching, and battery monitoring.

## Features

- Wi-Fi streaming with MP3, AAC, and common internet radio formats.
- TFT display UI with encoder and button controls.
- IR remote support.
- OTA update support.
- ES8388 codec output with speaker and headphone routing.
- Battery level display.
- Multiple saved Wi-Fi credentials through LittleFS `/wifi.json`.
- Captive Wi-Fi setup portal when no saved network connects.
- Windows-first USB Audio Class output in the main firmware.
- Optional USB Display + Audio Windows POC build.
- Optional Spotify Connect POC build through `cspot`.
- Optional experimental AirPlay 1/RAOP receiver POC build.

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

## USB Display + Audio POC

`muse_radio_usb_display_poc` is an experimental Windows USB screen build. It exposes the Muse as a composite USB device: a TinyUSB vendor display interface plus the existing UAC speaker output.

- Target display mode: 320x240 landscape, JPEG quality 4, 10 FPS.
- USB IDs for this POC: VID `0x303A`, PID `0x2986`.
- Vendor interface string: `esp32s3udisp0_R320x240_Ejpg4_Fps10_Bl65536`.
- Windows speaker and microphone endpoints are both advertised as `Muse Radio`.
- Audio remains PCM stereo, 16-bit, 44.1 kHz, through the ES8388 codec path.
- USB microphone input is enabled as a stereo 16-bit / 44.1 kHz UAC source. The two Muse Radio differential microphone channels are captured from the ES8388 ADC over I2S `DIN GPIO4`.
- The ES8388 microphone path keeps two-pair differential routing, inverts the right ADC channel to align stereo phase, uses 32-bit ADC serial slots on the shared I2S clock, and logs ADC registers at boot for validation.
- Volume is shared between the Muse knob, remote volume keys, and Windows. Host UAC volume/mute updates drive the ES8388 codec, while local knob/remote changes send standard USB HID consumer volume/mute keys back to Windows. PCM stays at full scale.
- The POC is not a smooth video target; frame drops are preferred over USB audio underruns.
- USB display mode latches active after the first received frame; lack of screen updates no longer resumes internet radio.
- The display service caches the last non-black frame and replays it periodically so a static Windows desktop stays visible.
- Incoming all-black blanking frames are ignored when a previous frame is available.
- The firmware does not draw local USB status text over the Windows-rendered frame in this POC.
- Backlight sleep is disabled in all modes on this experimental branch.
- First boot without saved Wi-Fi credentials keeps the USB display/audio runtime alive and starts the captive portal in the background instead of blocking setup.
- The previous USB audio distortion was caused mainly by UAC cadence drift: 44.1 kHz was consumed as integer `44100 / 1000 = 44` frames/ms, effectively 44.0 kHz. The local UAC component now uses fractional frame accumulation so 10 ms consumes exactly 441 frames, keeps the default 10 ms UAC interval, and only lets display rendering proceed while the audio buffer is healthy.
- The microphone wiring and ES8388 ADC setup are based on [RASPIAUDIO/Muse_library](https://github.com/RASPIAUDIO/Muse_library), especially the Muse Radio `museS3` and recorder examples.

Build it with:

```powershell
python -m platformio run -e muse_radio_usb_display_poc
```

The full factory image for this POC is:

```text
bin/MuseRadio-DEMO-V1.8-display-poc-factory-full.bin
```

Flash it at address `0x0`. This image includes LittleFS and overwrites saved Wi-Fi credentials, presets, and settings:

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 bin/MuseRadio-DEMO-V1.8-display-poc-factory-full.bin
```

Use the Espressif USB extended screen driver first. Notes and driver links are in `tools/windows_usb_display_driver/README.md`.

## Release 1.7

Version 1.7 adds USB Audio Class output to the main `muse_radio` firmware. The Muse can act as a Windows USB speaker: PC -> USB -> Muse -> ES8388.

- Added `ENABLE_USB_AUDIO=1` to the main `muse_radio` environment.
- Added `UsbAudioService` around Espressif's `usb_device_uac` component and TinyUSB UAC speaker endpoint.
- The USB audio profile is output-only, PCM stereo, 16-bit, 44.1 kHz.
- USB PCM is buffered and sent through `Audio::writeRawPCM16(..., 44100, 2)`, so the existing ES8388/I2S output path remains shared.
- The USB output path prerolls about 50 ms and aggregates USB packets into 20 ms I2S writes to reduce transient underruns.
- USB Audio starts before Wi-Fi setup, and the USB CDC/Improv serial path is disabled in this build so UAC owns the native USB link.
- When USB audio starts, internet radio playback is stopped and the TFT shows a minimal `USB Audio` screen.
- When USB audio becomes inactive, the firmware waits briefly before resuming the previous radio mode.
- Windows host volume and mute events are mapped to the ES8388 codec volume/mute path, not destructive software attenuation.
- The USB product descriptor is `Muse Radio`; Windows may show the playback endpoint as `Speakers (usb uac)`.
- Microphone/UAC input is not included in 1.7 and remains planned for a later build.
- The displayed firmware version is now `V1.7`.

## Release 1.6

Version 1.6 adds an isolated AirPlay 1/RAOP proof of concept for local testing. This is an experimental, source-available, non-certified receiver; it must not be described as official Apple AirPlay compatibility or as an MFi-certified product.

- Added `muse_radio_airplay_poc` with `ENABLE_AIRPLAY=1`.
- Added `AirPlayService` and a local RAOP component derived from permissively licensed AirPlay 1 reference code.
- The radio advertises itself on the local Wi-Fi as `Muse Radio-XXXX` through `_raop._tcp`.
- When AirPlay starts, internet radio playback stops and the TFT shows a minimal `AirPlay` screen with title/artist metadata when available.
- AirPlay volume controls the ES8388 codec output path, matching the Spotify codec-volume strategy.
- When AirPlay disconnects, the firmware waits briefly before resuming the last radio station.
- AirPlay 2 is not included in this release; it remains a separate spike because available ESP32 AirPlay 2 projects have heavier integration and licensing constraints.
- The displayed firmware version is now `V1.6`.

For a lightweight Windows smoke test without iTunes, use `tools/airplay_sine_test.ps1`. iTunes 12.13, a real iPhone, or a Mac is still recommended before claiming robust user compatibility.

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
- Main firmware exposes a Windows-first USB Audio Class output device for 44.1 kHz / 16-bit stereo playback.
- Optional Spotify Connect POC is available in `muse_radio_spotify_poc`.
- Optional experimental AirPlay 1/RAOP POC is available in `muse_radio_airplay_poc`.
- When no saved Wi-Fi network connects, the radio starts a captive setup portal at `http://192.168.4.1` and displays a Wi-Fi QR code on the TFT.
- The captive portal screen also offers the OK/encoder button as a shortcut to manual credential entry.
- The displayed firmware version is now `V1.7`.
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
python -m platformio run -e muse_radio_usb_display_poc
python -m platformio run -e muse_radio_spotify_poc
python -m platformio run -e muse_radio_airplay_poc
python -m platformio run -e muse_radio -t upload --upload-port COM5
python -m platformio run -e muse_radio -t uploadfs --upload-port COM5
```

USB Audio 1.7 changes native USB enumeration because the ESP32-S3 presents itself as a UAC device. If the native USB serial port disappears after flashing the USB Audio firmware, use the CP210x UART bridge for logs and uploads during tests. On the current bench this UART was observed as `COM8`; adapt the port to your Windows Device Manager.

```bash
python -m platformio run -e muse_radio -t upload --upload-port COM8
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

For the AirPlay 1 POC, use the isolated `muse_radio_airplay_poc` environment. This build is experimental and not Apple certified.

```powershell
python -m platformio run -e muse_radio_airplay_poc
Copy-Item -Force .pio/build/muse_radio_airplay_poc/firmware.bin bin/MuseRadio-DEMO-V1.6-airplay-poc-app.bin
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x10000 bin/MuseRadio-DEMO-V1.6-airplay-poc-app.bin
```

To test RAOP from Windows without installing iTunes, install or place `ffmpeg` at `C:\ffmpeg\bin\ffmpeg.exe`, flash the app-only AirPlay image, then run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/airplay_sine_test.ps1 -ScanOnly
powershell -ExecutionPolicy Bypass -File tools/airplay_sine_test.ps1 -Volume 45
```

The script creates a local `.venv-airplay-test`, installs pinned `pyatv==0.17.0`, scans `_raop._tcp`, and streams an 8-second sine wave to `Muse Radio-3BD8` by default. Override `-HostAddress`, `-Name`, or pass `-File path\to\audio.wav` for a specific test file.

Use the app-only image above for normal updates; it preserves LittleFS credentials and presets. The factory-full image below includes the LittleFS data partition and will overwrite saved Wi-Fi credentials, Spotify auth cache, presets, and settings.

```powershell
python -m platformio run -e muse_radio_airplay_poc
python -m platformio run -e muse_radio_airplay_poc -t buildfs
$bootApp0 = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
python -m esptool --chip esp32s3 merge_bin --output bin/MuseRadio-DEMO-V1.6-airplay-poc-factory-full.bin --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 .pio/build/muse_radio_airplay_poc/bootloader.bin 0x8000 .pio/build/muse_radio_airplay_poc/partitions.bin 0xe000 $bootApp0 0x10000 .pio/build/muse_radio_airplay_poc/firmware.bin 0x310000 .pio/build/muse_radio_airplay_poc/littlefs.bin
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 bin/MuseRadio-DEMO-V1.6-airplay-poc-factory-full.bin
```

The LittleFS data directory is `RadioV01/data`. If upload does not start automatically, press the hidden IO0 button with a paperclip inserted in the headphone jack.

## Notes

- PSRAM is required by `ESP32-audioI2S` 3.4.5.
- The ES8388 codec needs MCLK on GPIO0 with the current audio library.
- USB Audio 1.7 is output-only; ES8388 microphone/I2S RX support is intentionally deferred.
- USB Audio uses Espressif `usb_device_uac` 1.2.3, vendored locally with a PlatformIO CMake descriptor build fix.
- USB Audio is Windows-first in this release. The macOS-specific UAC descriptor mode is disabled because it can prevent Windows recognition.
- USB Display + Audio POC keeps 44.1 kHz exact with fractional UAC frame reads; this avoids the 44.0 kHz drift that caused buffer pressure and audible distortion.
- Spotify Connect POC firmware is close to the 3 MB app partition limit: current clean build is about 95% of `app0`.
- AirPlay 1/RAOP POC firmware is currently smaller than the Spotify POC, but it uses an unofficial legacy RAOP implementation and is not an MFi/Apple-certified receiver.
- Legacy factory I2S tests can be re-enabled with `ENABLE_LEGACY_FACTORY_I2S=1`, but they are incompatible with the new audio driver path and are disabled by default.
