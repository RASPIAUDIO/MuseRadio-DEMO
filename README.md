V1.2 fix: AAC support, mp3 fix that was making slow pitch artifacts.

# Muse Radio : Demo Internet Radio Project
====> Copy the libraries directory to your local librairies directory
====> ESP32 Arduino 2.0.13

Install LittleFS plugin in arduino IDE 
Upload your data directory unsing the LittleFS, if upload does not automatically starts you might need to use the hidden IO0 button by inserting a paperclip in the headphone jack

to load a precompiled firmware using ESPtool use the command adjusted with you serial com port and file name:

esptool -p COM16 -b 1000000 write_flash 0 myfirmware.bin

To recompile use following Arduino parameters:

![image](https://github.com/user-attachments/assets/871232ee-431e-4a81-93a3-66468d48b6c2)


