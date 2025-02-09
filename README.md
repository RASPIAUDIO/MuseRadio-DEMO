
# Muse Radio: Demo Internet Radio Project

Welcome to the Muse Radio demo project, an internet radio designed for the [Muse Radio](https://raspiaudio.com/product/muse-radio/) product by RASPIAUDIO. This demonstration showcases various features including WiFi streaming, integrated screen and button controls, and more.

## Features

- **WiFi Streaming**: Supports multiple stream formats such as MP3, AAC, and more.
- **User Interface**: Seamlessly integrates a display screen and six-button control.
- **Remote Control**: Operate using an IR remote.
- **OTA Updates**: Over-the-air demo support.
- **Audio Codec**: Utilizes the ES8388 codec, with support for both headphone jack and speaker, including codec volume control.
- **Battery Monitoring**: Displays battery level status.

## Source Code and Discussion

- **Source Code**: Available on [GitHub](https://github.com/RASPIAUDIO/MuseRadio-DEMO).
- **Discussion Forum**: Join the conversation on our [forum](https://forum.raspiaudio.com/t/muse-radio-demo-app-fo-internet-radio-update/1214).

## Installation Instructions

1. **Access the Application**:
   - Visit [apps.raspiaudio.com](https://apps.raspiaudio.com) using Chrome.

2. **Connect the Device**:
   - Power on the Muse Radio and connect it to your computer via a USB data cable.

3. **Setup Muse Radio**:
   - Select "Muse Radio - Radio" on the website.
   - Click "Connect" and choose the appropriate COM port.

4. **Set Up WiFi**:
   - After the 2-minute download, configure your WiFi credentials directly in the browser (an offline option) or through the on-screen menu.

5. **Troubleshooting**:
   - For any issues during installation or usage, please open an issue on [GitHub](https://github.com/RASPIAUDIO/MuseRadio-DEMO/issues).

## Updates

### Version 1.2:
- Fixed AAC support and corrected slow pitch artifacts in MP3 playback.

## Development Setup

#### Libraries & Tools Required
- Copy the `libraries` directory to your local libraries folder.
- ESP32 Arduino 2.0.13.
- Install the LittleFS plugin in Arduino IDE.

#### Flashing and Uploading Data
- Upload your data directory using LittleFS. If upload doesn’t start automatically, you may need to:
  - Press the hidden IO0 button with a paperclip inserted in the headphone jack.
- To flash precompiled firmware, use the following command (adjust with your serial COM port and file name):

  ```bash
  esptool -p COM16 -b 1000000 write_flash 0 myfirmware.bin
  ```

#### Recompilation Parameters
- Use specified Arduino parameters as shown in your IDE settings.

![Arduino Setup](https://github.com/user-attachments/assets/871232ee-431e-4a81-93a3-66468d48b6c2)
