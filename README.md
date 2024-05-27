# Muse Radio : Demo Internet Radio Project
====> Copy the libraries directory to your local librairies directory
====> ESP32 Arduino 2.0.13

Install LittleFS plugin in arduino IDE 
Upload your data directory unsing the LittleFS, you might need to use the hidden IO0 button by inserting a paperclip in the headphone jack

to load a precompiled firmware using ESPtool use the command adjusted with you serial com port and file name:

esptool -p COM16 -b 1000000 write_flash 0 myfirmware.bin
