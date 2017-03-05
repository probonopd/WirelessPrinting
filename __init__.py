from . import WirelessPrintOutputDevicePlugin
from . import DiscoverWirelessPrintAction
from UM.i18n import i18nCatalog
catalog = i18nCatalog("cura")

def getMetaData():
    return {
        "type": "extension",
        "plugin": {
            "name": "WirelessPrint connection",
            "author": "probonopd",
            "version": "1.0",
            "description": catalog.i18nc("@info:whatsthis", "Allows sending prints to WirelessPrint"),
            "api": 3
        }
    }

def register(app):
    return {
        "output_device": WirelessPrintOutputDevicePlugin.WirelessPrintOutputDevicePlugin(),
        "machine_action": DiscoverWirelessPrintAction.DiscoverWirelessPrintAction()
    }
