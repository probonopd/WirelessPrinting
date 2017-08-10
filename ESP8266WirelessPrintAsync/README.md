# ESP8266WirelessPrintAsync

## Building

Pre-built binaries are available on GitHub Releases.

The following external libraries need to be installed:

```
mkdir -p $HOME/Arduino/libraries/
cd $HOME
git clone https://github.com/probonopd/WirelessPrinting.git
cd $HOME/Arduino/libraries/
# git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
git clone -o 56e7450 https://github.com/probonopd/ESPAsyncWebServer.git # Patched version until https://github.com/me-no/dev/ESPAsyncWebServer/pull/192 is merged
( cd ESPAsyncWebServer ; git checkout 56e745 )
git clone https://github.com/me-no-dev/ESPAsyncTCP.git
git clone https://github.com/alanswx/ESPAsyncWiFiManager.git
git clone https://github.com/bblanchon/ArduinoJson.git
cd -
```
## Flashing

Can be flashed via USB or (subsequently) over the air. You can use the Arduino IDE if you compiled yourself, or one of the following commands if you just want to flash a precompiled firmware.

```
# USB
 sudo chmod a+x /dev/ttyUSB0
`/tmp/.mount_*/usr/bin/hardware/esp8266/esp8266/tools/esptool/esptool -vv -cd nodemcu -cb 921600 -cp /dev/ttyUSB0 -ca 0x00000 -cf ESP8266WirelessPrint_d1_mini_08d8b11.bin

# Wireless
python /tmp/.mount_*/usr/bin/hardware/esp8266/esp8266/tools/espota.py -i 192.168.0.27 -p 8266 --auth= -f ESP8266WirelessPrint_*.bin
```
