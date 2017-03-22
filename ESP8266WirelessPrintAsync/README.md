# ESP8266WirelessPrintAsync

The following external libraries need to be installed:

```
cd $HOME/Arduino/libraries/
git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
git clone https://github.com/me-no-dev/ESPAsyncTCP.git
```
Additionally, ESPAsyncWebServer needs to be patched:

```
nano ESPAsyncWebServer/src/WebServer.cpp 
nano ESPAsyncWebServer/src/WebResponseImpl.h 
nano ESPAsyncWebServer/src/ESPAsyncWebServer.h
```
