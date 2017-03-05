# WirelessPrinting [![Build Status](https://travis-ci.org/probonopd/WirelessPrint.svg?branch=master)](https://travis-ci.org/probonopd/WirelessPrinting)

__UNDER DEVELOPMENT__. See [Issues](https://github.com/probonopd/WirelessPrinting/issues). Pull requests welcome!

Allows you to print from [Cura](https://ultimaker.com/en/products/cura-software) to your 3D printer connected to an [ESP8266](https://espressif.com/en/products/hardware/esp8266ex/overview) module.

## ESP8266WirelessPrint

[esp8266/Arduino](https://github.com/esp8266/Arduino) sketch to be uploaded to a Wemos D1 mini module. To be connected with your 3D printer via the serial connection and to a SD card (acting as a cache during printing).

To print, just open http://3d.local/print and upload a G-Code file using the form:

![Upload](https://cloud.githubusercontent.com/assets/2480569/23586936/fd0e3fa2-01a0-11e7-9d83-dc4e7d031f30.png)

Ycan also print from the command line using curl:

```
curl -F "file=@/path/to/some.gcode" http://3d.local/print
```

### Compiling

Before you compile this, you need to create a file called `private.h` in the same direcory as the sketch. It should contain:

```
const char* ssid = "________";
const char* password = "________";
```

This should be replaced by something more elegant. Pull requests welcome.

## WirelessPrinting

Cura plugin which discovers ESP8266WirelessPrint instances using Zeroconf and enables printing directly to ESP8266WirelessPrint.

### Installation

- Make sure your Cura version is 2.4 or newer
- Download or clone the repository into [Cura installation folder]/plugins/WirelessPrinting 
  or in the plugins folder inside the configuration folder. The configuration folder can be
  found via Help -> Show Configuration Folder inside Cura.
  NB: The folder of the plugin itself *must* be ```WirelessPrinting```
- If you are running Cura from source, make sure you install python-zeroconf using pip: 
  ```pip3 install python3-zeroconf```.
  Released versions of Cura already meet this requirement.

### How to use

- Make sure WirelessPrint is up and running, and the discovery plugin is not disabled
- In Cura, add a Printer matching the 3D printer you have connected to WirelessPrint
- Select "Connect to WirelessPrint" on the Manage Printers page.
- Select your WirelessPrint instance from the list
- From this point on, the print monitor should be functional and you should be
  able to switch to "Print on <devicename>" on the bottom of the sidebar.
