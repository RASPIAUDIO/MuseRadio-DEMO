# Muse Radio USB Display POC Driver

This POC uses the Espressif USB extended screen driver path:

- Firmware exposes a composite USB device with VID `0x303A`, PID `0x2986`.
- Interface 0 is a TinyUSB vendor interface used by the Windows IDD driver.
- The vendor interface string is `esp32s3udisp0_R320x240_Ejpg4_Fps5_Bl65536`.
- Audio remains UAC speaker output at 44.1 kHz, 16-bit stereo.

Use the Espressif signed installer first:

https://dl.espressif.com/AE/esp-iot-solution/xfz1986_usb_graphic_250224_rc_sign.exe

If the signed driver does not bind to the Muse, use the source driver referenced by Espressif:

https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/device/usb_extend_screen/windows_driver

For a self-built driver, Windows may require test signing:

```powershell
bcdedit /set testsigning on
```

Then reboot, install the modified INF from Device Manager, and confirm Windows shows both:

- a new display adapter / monitor for the Muse screen;
- a USB audio output device for Muse Radio.

Initial test target is intentionally conservative: 320x240 landscape, JPEG quality 4, 5 FPS.
