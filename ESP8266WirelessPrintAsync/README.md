# ESP8266WirelessPrintAsync

The following external libraries need to be installed:

```
mkdir -p $HOME/Arduino/libraries/
cd $HOME
git clone https://github.com/probonopd/WirelessPrinting.git
cd $HOME/Arduino/libraries/
# git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
git clone -o 56e7450 https://github.com/probonopd/ESPAsyncWebServer.git # Patched version until https://github.com/me-no-dev/ESPAsyncWebServer/pull/192 is merged
git clone https://github.com/me-no-dev/ESPAsyncTCP.git
git clone https://github.com/alanswx/ESPAsyncWiFiManager.git
git clone https://github.com/bblanchon/ArduinoJson.git
cd -
```
