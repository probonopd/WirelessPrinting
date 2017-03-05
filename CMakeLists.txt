project(WirelessPrinting NONE)
cmake_minimum_required(VERSION 2.8.12)

install(FILES
    __init__.py
    DiscoverWirelessPrintAction.py
    DiscoverWirelessPrintAction.qml
    WirelessPrintComponents.qml
    WirelessPrintOutputDevice.py
    WirelessPrintOutputDevicePlugin.py
    LICENSE
    README.md
    DESTINATION lib/cura/plugins/WirelessPrinting
)
