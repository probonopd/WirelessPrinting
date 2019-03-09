# WirelessPrinting [![Build Status](https://travis-ci.org/probonopd/WirelessPrinting.svg?branch=master)](https://travis-ci.org/probonopd/WirelessPrinting)

![](https://user-images.githubusercontent.com/2480569/53683404-5b21ab80-3cf8-11e9-8a6e-647df742612b.jpg)

Print wirelessly from [Cura](https://ultimaker.com/en/products/cura-software), [PrusaControl](http://prusacontrol.org/), or [Slic3r PE](https://github.com/prusa3d/Slic3r/releases) to your 3D printer connected to an [ESP8266](https://espressif.com/en/products/hardware/esp8266ex/overview) module.

__UNDER DEVELOPMENT__. See [Issues](https://github.com/probonopd/WirelessPrinting/issues). Pull requests welcome!

## Comparison with other printer hosts

| Printer SD card slot | OctoPrint | WirelessPrint |
| --- | --- | --- |
| Instant | Booting can take minutes | Booting takes seconds |
| Need to plug SD card into computer and then into printer for each print | Ethernet and wireless | Wireless |
| No cost (comes with many printers) | High cost (Raspberry Pi, Power supply, SD card) | Inexpensive | 
| No clutter on desktop | Clutter on desktop (Raspberry Pi, cable) | No clutter (can be placed inside printer electronics box) |
| No set-up needed | Set-up needed (full Linux operating system, hundreds of megabytes) | Only quick wireless network setup needed | 
| No maintenance needed (other than replacing broken SD card slots) | High maintenance needed (OS updates) | Low maintenance needed (Firmware updates for bugfixes and new features) |
| No extra power consumption | 2.5 W power consumption | Under 1 W power consumption |
| No webcam | Webcam can be attached | No webcam |
| No notifications | Notifications, e.g., "print ready" | Notifications possible (send pull requests) |
| Cumbersome for print farms (sneakernet) | Suitable for print farms (can be managed centrally) | Suitable for print farms (can be managed centrally, OctoPrint compatible protocol subset) |

## Hardware

_Readymade, pre-assembled, flashed and tested hardware may be available, please contact me for information._

The ESP8266 module is connected with your 3D printer via the serial connection and to a SD card (acting as a cache during printing). You need to connect
* TX, RX from your 3D printer to the ESP8266 module (__AUX-1__ header on RAMPS boards)
* Power and GND from your 3D printer to the ESP8266 module (attention, the __AUX-1__ header on RAMPS boards has 5V while the ESP8266 needs 3.3V)
* Optional: SD card shield to the ESP8266 module (a capacitor across the power pins of the SD card; SD shields have this). Using a SanDisk 2 GB card formatted with `mkfs.vfat` on Linux seems to work for me. If no SD card is connected, then the internal SPIFFS memory (3 MB) is used
* A matching case for a WeMos D1 mini and microSD shield can be found at http://www.thingiverse.com/thing:2287618

## esp8266/Arduino sketch

The [esp8266/Arduino](https://github.com/esp8266/Arduino) sketch `ESP8266WirelessPrintAsync.ino` is uploaded to a ESP8266 module. As or July 2017, this code compiled on Arduino hourly and esp8266/Arduino git master. See `.travis.yml` for how this is compiled on Travis CI.

### Building

Pre-built binaries are available for download on [GitHub Releases](https://github.com/probonopd/WirelessPrinting/releases).

The following external libraries need to be installed:

```
mkdir -p $HOME/Arduino/libraries/
cd $HOME
git clone https://github.com/probonopd/WirelessPrinting
cd $HOME/Arduino/libraries/
# wget "https://raw.githubusercontent.com/probonopd/WirelessPrinting/master/.travis.yml" -O - | grep "git clone" | cut -d " " -f 4-99
git clone https://github.com/me-no-dev/ESPAsyncWebServer
git clone -o 991f855 https://github.com/me-no-dev/ESPAsyncTCP
git clone -o 6734c16 https://github.com/alanswx/ESPAsyncWiFiManager
( git clone -b 6.x https://github.com/bblanchon/ArduinoJson ; cd ArduinoJson ; git reset --hard 3df4efd )
git clone https://github.com/greiman/SdFat # SD long names support
git clone https://github.com/me-no-dev/AsyncTCP # for ESP32
cd -
```
### Flashing from Linux

Can be flashed via USB or (subsequently) over the air. You can use the Arduino IDE if you compiled yourself, or one of the following commands if you just want to flash a precompiled firmware.

#### ESP8266

```
# USB
sudo chmod a+x /dev/ttyUSB0 ; /tmp/.mount_*/usr/bin/hardware/esp8266/esp8266/tools/esptool/esptool -vv -cd nodemcu -cb 921600 -cp /dev/ttyUSB0 -ca 0x00000 -cf ESP8266WirelessPrint*.bin

# Wireless
wget -c "https://raw.githubusercontent.com/esp8266/Arduino/master/tools/espota.py"
python espota.py -i 192.168.0.27 -p 8266 --auth= -f ESP8266WirelessPrint*.bin
```

##### ESP32

```
# USB
sudo apt install python-serial
sudo chmod a+x /dev/ttyUSB0 ; python $HOME/.arduino15/packages/esp32/tools/esptool_py/2.6.0/esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x10000 ESP8266WirelessPrintAsync_esp32_*.bin

# Wireless
python $HOME/.arduino15/packages/esp32/hardware/esp32/1.0.1/tools/espota.py -i 192.168.0.16 -p 3232 --auth= -f ESP8266WirelessPrintAsync_esp32_*.bin
```

After the initial flashing, you can upload new versions of this firmware from the web interface without any further tools.

## Initial WiFi Configuration
Following the instrucions in https://github.com/alanswx/ESPAsyncWiFiManager/ : 

The first time the sketch is uploaded the ESP will enter in Access Point mode, so you have to open the wifi manager of your system and connect to wifi "AutoConnectAP", then open your browser and type http://192.168.4.1/, there you will see a menu, select "Configure WiFi", press scan and wait until the device scans available networks and select yours, enter the the password and click save. It will try to connect to your network, if it's successfull you will see a message on your 3D printer (or in a serial monitor if conected to your computer) with the new device IP, write down this IP if you wish to connect via browser. 

## Wireless printing with Cura

Cura 2.6 and later come with a bundled plugin which discovers OctoPrint instances using Zeroconf and enables printing directly to them. In newer versions of Cura, you need to install the [Cura OctoPrint Plugin](https://github.com/fieldOfView/Cura-OctoPrintPlugin) from the "Toolbox" menu. To use it,
- In Cura, add a Printer matching the 3D printer you have connected to WirelessPrint
- Select "Connect to OctoPrint" on the Manage Printers page
- Select your OctoPrint instance from the list
- Enter an API key (for now a random one is sufficient)
- Click "Connect", then click "Close"
From this point on, the print monitor should be functional and you should see a "Print with OctoPrint" button on the bottom of the sidebar. Use this button to print wirelessly.

## Wireless printing with PrusaControl

Currently a custom fork, [PrusaControlWireless](https://github.com/probonopd/PrusaControl/tree/WirelessPrinting), is required. This may change in the future.

## Wireless printing with Slic3r PE

Slic3r PE 1.36.0 and later discovers OctoPrint instances using Zeroconf and enables printing directly to them. No further software needs to be installed. To use it,
- In Slic3r PE, add the corresponding profile for your printer
- Select the "Printer Settings" tab
- Under "OctoPrint upload", enter the IP address of your WirelessPrinting device (in the future, it may be discoverable by Bonjour)
- Click "Test"
From this point on, you should see a "Send to printer" button on the "Plater" tab. Use this button to print wirelessly.

## Wireless printing using a browser or the command line

To print, just open http://the-ip-address/ and upload a G-Code file using the form:

![Upload](https://cloud.githubusercontent.com/assets/2480569/23586936/fd0e3fa2-01a0-11e7-9d83-dc4e7d031f30.png)

Ycan also print from the command line using curl:

```
curl -F "file=@/path/to/some.gcode" -F "print=true" http://the-ip-address/print
```
