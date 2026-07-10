# Muse Radio - Radio Only

Stable internet-radio firmware for the RASPIAUDIO Muse Radio.

This branch deliberately does one job: play internet radio stations reliably. Spotify Connect, AirPlay, USB Audio, OTA updates, cloud station services and factory tests are excluded.

## Target Hardware

- ESP32-S3 with 8 MB flash and 8 MB octal PSRAM
- ES8388 audio codec
- I2S: MCLK GPIO0, BCLK GPIO5, LRCK GPIO16, DOUT GPIO17
- Codec I2C: SDA GPIO18, SCL GPIO11
- Integrated 320 x 240 TFT, encoders, ADC buttons, speaker and headphone output

## Daily Use

- Turn the volume encoder to adjust the ES8388 hardware volume.
- Turn the station encoder to browse presets, then press it to select.
- Press ADC buttons 1, 2 or 3 to start presets 1, 2 or 3 directly.
- Hold the ADC CHECK button for 5 seconds to open the settings portal.
- Plugging in headphones switches the ES8388 output from speaker to headphones.

The last station and volume are restored after a restart.

## Wi-Fi And Radio Portal

The radio tries every saved Wi-Fi network. If none connects, or when the ADC CHECK button is held for 5 seconds, it creates:

- Wi-Fi name: `MuseRadio-XXXX`
- Password: `museradio`
- Local portal: `http://192.168.4.1`

Scan the on-screen QR code to join the setup Wi-Fi. The captive page should open automatically; otherwise open `http://192.168.4.1`.

When the portal was opened from the radio, press CHECK once to return to playback. The radio also returns automatically after 60 seconds with no portal client connected. First-time Wi-Fi setup remains open until a network is saved, then the radio restarts automatically.

The portal can:

- scan visible Wi-Fi networks;
- edit the SSID field manually for a hidden network;
- save several Wi-Fi networks and forget old ones;
- search the local station catalog;
- assign stations to presets 1 through 99;
- add a station manually with a name and an `http://` or `https://` stream URL.

The station catalog and presets are stored locally in LittleFS. No remote station-management server is required. Press the station encoder on the portal screen to use the manual on-device Wi-Fi entry as a fallback.

## Reliability Choices

- Radio decoding runs on core 0; display and controls run on core 1.
- Audio callbacks never draw on the TFT or reconnect Wi-Fi.
- Presets are cached in PSRAM to avoid filesystem access from playback and UI tasks.
- Wi-Fi, presets, station and volume files use temporary files plus rollback backups.
- Interrupted writes are recovered at boot.
- LittleFS is never formatted automatically when mounting fails.
- ES8388 access is serialized and initialization is retried.
- Volume is handled by the ES8388 codec, without reducing PCM precision in software.

## Build

Requirements: Python, PlatformIO Core 6.1.19 or newer, and Windows PowerShell.

```powershell
python -m platformio run -e muse_radio
python -m platformio run -e muse_radio -t buildfs
```

The build is pinned to pioarduino `55.03.38`, Arduino ESP32 `3.3.8`, ESP-IDF libraries `5.5.4`, and the ESP32-S3 N8R8 board profile.

## Flash Without Erasing Wi-Fi

Flash only the application at `0x10000`. This preserves LittleFS and saved credentials:

```powershell
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x10000 .pio/build/muse_radio/firmware.bin
```

Replace `COM5` with the board's USB serial port. Do not use `erase-flash` or `uploadfs` when credentials must be preserved.

## Full Image With Default Data

Use this only for factory provisioning because it replaces LittleFS. The generated `firmware.factory.bin` does not contain the LittleFS partition.

```powershell
python -m platformio run -e muse_radio
python -m platformio run -e muse_radio -t buildfs
$bootApp0 = "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
python -m esptool --chip esp32s3 merge_bin --output .pio/build/muse_radio/firmware.with-data.bin --flash_mode keep --flash_freq 80m --flash_size 8MB 0x0 .pio/build/muse_radio/bootloader.bin 0x8000 .pio/build/muse_radio/partitions.bin 0xe000 $bootApp0 0x10000 .pio/build/muse_radio/firmware.bin 0x310000 .pio/build/muse_radio/littlefs.bin
python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 .pio/build/muse_radio/firmware.with-data.bin
```

LittleFS starts at `0x310000` in the pinned `huge_app.csv` partition table.

## Source

- [MuseRadio-DEMO repository](https://github.com/RASPIAUDIO/MuseRadio-DEMO)
- [Muse Radio product](https://raspiaudio.com/product/muse-radio/)
