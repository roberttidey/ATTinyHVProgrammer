# ATTinyHVProgrammer
High voltage programming utility for ATTiny using web based ESP8266

Developed from previous fuse editor.

Construction details to be uploaded shortly

## Features
- Web based utility for ATTiny programming using high voltage interface
- read and write low and high fuse data
- erase chip (needed if new write operations have non blank 0xff locations)
- read and write Flash data from and into Intel hex files
- read and write EEPROM data from and into Intel hex files
- Useful for re-installing micronucleus
- Supports ATTiny25, ATTiny45, ATTiny85
- USB powered
- Configure wifi via built in WebManager AP
- Software can be updated via web interface
- ip/ gives access to fuse utility
- ip/edit allows viewing and editing SPIFF files (web server files)
- ip/firmware allows update of firmware from a binary

## Install
Uses BaseSupport library for common functions https://github.com/roberttidey/BaseSupport
Edit BaseConfig.h for WifiManager and upload passwords
Uncomment FASTCONNECT in BaseConfig.h as required
Compile and serial upload in Arduino
Further updates can be done OTA from an exported binary

##Build






