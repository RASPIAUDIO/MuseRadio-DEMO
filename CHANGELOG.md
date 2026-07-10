# Changelog

## V1.0-radio - 2026-07-10

- Created the `radio-only-stable` branch from the last local-radio firmware base.
- Reduced the firmware to internet radio, local presets, Wi-Fi setup, controls, display and ES8388 output.
- Removed Spotify Connect, AirPlay, USB Audio, OTA, Improv Serial, MQTT, remote station-server and factory-test code.
- Kept a captive Wi-Fi portal with scanned and manually editable SSIDs, multiple saved networks and network removal.
- Kept a fully local station catalog, 99 presets and manual station name/URL entry.
- Mapped ADC buttons 1, 2 and 3 to presets 1, 2 and 3, and mapped a 5-second CHECK hold to the settings portal.
- Added CHECK-to-return and a 60-second no-client timeout to leave the portal, plus an automatic restart after saving Wi-Fi.
- Live-checked all 18 default presets and 52 catalog entries, replacing ten obsolete stream URLs.
- Replaced the France Culture Icecast URL with the Radio France high-quality HLS stream and migrated existing presets at boot.
- Moved radio reconnect and retry handling into the radio task and kept TFT work out of audio callbacks.
- Added PSRAM preset caching to avoid concurrent LittleFS reads during playback and station selection.
- Added atomic LittleFS writes, paired preset-file rollback and interrupted-write recovery at boot.
- Disabled automatic LittleFS formatting so a mount error cannot silently erase credentials.
- Added validated restoration and debounced storage of station and volume settings.
- Serialized ES8388 I2C access, added codec initialization retries and kept hardware codec volume at full PCM precision.
- Corrected speaker/headphone routing and kept the power amplifier disabled when muted or on headphones.
- Pinned the build to pioarduino 55.03.38 and Arduino ESP32 3.3.8 for the ESP32-S3 N8R8 target.
