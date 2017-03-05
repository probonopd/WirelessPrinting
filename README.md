# WirelessPrinting [![Build Status](https://travis-ci.org/probonopd/WirelessPrint.svg?branch=master)](https://travis-ci.org/probonopd/WirelessPrinting)

__IN DEVELOPMENT__. See Issues.

Allows you to print from [Cura](https://ultimaker.com/en/products/cura-software) to your 3D printer connected to an [ESP8266](https://espressif.com/en/products/hardware/esp8266ex/overview) module.

## ESP8266WirelessPrint

Sketch to be uploaded to a Wemos D1 mini module. To be connected with your 3D printer via the serial connection and to a SD card (acting as a cache during printing).

## WirelessPrinting
Cura plugin which discovers ESP8266WirelessPrint instances using Zeroconf and enables printing directly to ESP8266WirelessPrint

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
  able to switch to "Print to WirelessPrint" on the bottom of the sidebar.

