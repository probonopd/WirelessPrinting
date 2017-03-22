# ESP8266WirelessPrintAsync

The following external libraries need to be installed:

```
cd $HOME/Arduino/libraries/
git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
git clone https://github.com/me-no-dev/ESPAsyncTCP.git
```
Additionally, ESPAsyncWebServer needs to be patched until https://github.com/me-no-dev/ESPAsyncWebServer/pull/147 is merged:

```
   using File = fs::File;
   using FS = fs::FS;
```
needs to be inserted at the start of
 * class AsyncWebServerRequest in ESPAsyncWebServer.h (~line 122)
 * class AsyncFileResponse in WebResponseImpl.h (~line 44)
 * class AsyncStaticWebHandler in WebHandlerImpl.h (~line 29)
 
```
nano ESPAsyncWebServer/src/ESPAsyncWebServer.h 
nano ESPAsyncWebServer/src/WebResponseImpl.h 
nano ESPAsyncWebServer/src/WebHandlerImpl.h
```
