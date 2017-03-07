import os.path
from io import StringIO
from time import time

from UM.i18n import i18nCatalog
from UM.Application import Application
from UM.Logger import Logger
from UM.Signal import signalemitter
from UM.Message import Message
from UM.Util import parseBool

from cura.PrinterOutputDevice import PrinterOutputDevice, ConnectionState
from UM.OutputDevice.OutputDevice import OutputDevice
from UM.OutputDevice import OutputDeviceError

from PyQt5 import QtNetwork
# from PyQt5.QtNetwork import QHttpMultiPart, QHttpPart, QNetworkRequest, QNetworkAccessManager, QNetworkReply
from PyQt5.QtCore import QUrl, QTimer, pyqtSignal, pyqtProperty, pyqtSlot, QCoreApplication
from PyQt5.QtGui import QImage, QDesktopServices

import json
from time import time

catalog = i18nCatalog("cura")

from enum import Enum
class OutputStage(Enum):
    ready = 0
    writing = 1
    uploading = 2

##  WirelessPrint connected (wifi / lan) printer using the WirelessPrint API
@signalemitter
class WirelessPrintOutputDevice(PrinterOutputDevice):
    def __init__(self, key, address, port, properties):
        super().__init__(key)

        self.key = key
        self._key = key
        self._properties = properties
        self._address = address
        self._port = port
        self._progress_message = None
        self.setName(key)
        description = catalog.i18nc("@action:button", "Print on {0} ({1})").format(key, address)
        self.setShortDescription(description)
        self.setDescription(description)

        self._stage = OutputStage.ready
        self._address = address

        self._qnam = QtNetwork.QNetworkAccessManager()
        self._qnam.authenticationRequired.connect(self._onAuthRequired)
        self._qnam.sslErrors.connect(self._onSslErrors)
        self._qnam.finished.connect(self._onNetworkFinished)

        self._stream = None
        self._cleanupRequest()

    def getKey(self):
        return(self.key)

    def requestWrite(self, node, fileName = None, *args, **kwargs):
        if self._stage != OutputStage.ready:
            raise OutputDeviceError.DeviceBusyError()

        if fileName:
            fileName = os.path.splitext(fileName)[0] + '.gcode'
        else:
            fileName = "%s.gcode" % Application.getInstance().getPrintInformation().jobName
        self._fileName = fileName

        # create the temp file for the gcode
        self._stream = StringIO()
        self._stage = OutputStage.writing
        self.writeStarted.emit(self)

        # show a progress message
        message = Message(catalog.i18nc("@info:progress", "Saving to <filename>{0}</filename>").format(self.getName()), 0, False, -1)
        message.show()
        self._message = message

        # send all the gcode to self._stream
        gcode = getattr(Application.getInstance().getController().getScene(), "gcode_list")
        lines = len(gcode)
        nextYield = time() + 0.05
        i = 0
        for line in gcode:
            i += 1
            self._stream.write(line)
            if time() > nextYield:
                self._onProgress(i / lines)
                QCoreApplication.processEvents()
                nextYield = time() + 0.05

        # self._stream now contains the gcode, now upload it
        self._stage = OutputStage.uploading
        self._stream.seek(0)

        # set up a multi-part post
        self._multipart = QtNetwork.QHttpMultiPart(QtNetwork.QHttpMultiPart.FormDataType)

        # add the file part
        part = QtNetwork.QHttpPart()
        part.setHeader(QtNetwork.QNetworkRequest.ContentDispositionHeader,
                'form-data; name="file"; filename="%s"' % fileName)
        part.setBody(self._stream.getvalue().encode())
        self._multipart.append(part)

        # send the post
        url = "http://" + self._address + "/print"
        Logger.log("d", url)

####################################################
# This is working but ugly. Pull Requests welcome.

        fd = open('/tmp/' + fileName, "w")
        fd.write (self._stream.getvalue())
        fd.close()

        command = 'curl -F "file=@/tmp/' + fileName + '" ' + url
        Logger.log("d", command)

        import subprocess, shlex
        subprocess.Popen(shlex.split(command))

        self._stage = OutputStage.ready
        if self._message:
            self._message.hide()
        self._message = None

####################################################

#        self._request = QtNetwork.QNetworkRequest(QUrl(url))
#        self._request.setRawHeader('User-agent'.encode(), 'Cura WirelessPrinting Plugin'.encode())
#        self._reply = self._qnam.post(self._request, self._multipart)
#
#        # connect the reply signals
#        self._reply.error.connect(self._onNetworkError)
#        self._reply.uploadProgress.connect(self._onUploadProgress)
#        self._reply.downloadProgress.connect(self._onDownloadProgress)

    def _onProgress(self, progress):
        progress = (50 if self._stage == OutputStage.uploading else 0) + (progress / 2)
        if self._message:
            self._message.setProgress(progress)
        self.writeProgress.emit(self, progress)

    def _cleanupRequest(self):
        self._reply = None
        self._request = None
        self._multipart = None
        self._body_part = None
        if self._stream:
            self._stream.close()
        self._stream = None
        self._stage = OutputStage.ready
        self._fileName = None

    def _onNetworkFinished(self, reply):
        Logger.log("i", "_onNetworkFinished reply: %s", repr(reply.readAll()))
        Logger.log("i", "_onNetworkFinished reply.error(): %s", repr(reply.error()))

        self._stage = OutputStage.ready
        if self._message:
            self._message.hide()
        self._message = None

        self.writeFinished.emit(self)
        if reply.error():
            message = Message(catalog.i18nc("@info:status", "Could not save to {0}: {1}").format(self.getName(), str(reply.errorString())))
            message.show()
            self.writeError.emit(self)
        else:
            message = Message(catalog.i18nc("@info:status", "Saved to {0} as {1}").format(self.getName(), os.path.basename(self._fileName)))
            message.addAction("open_browser", catalog.i18nc("@action:button", "Open Browser"), "globe", catalog.i18nc("@info:tooltip", "Open browser to printer."))
            message.actionTriggered.connect(self._onMessageActionTriggered)
            message.show()
            self.writeSuccess.emit(self)
        self._cleanupRequest()

    def _onMessageActionTriggered(self, message, action):
        if action == "open_browser":
            QDesktopServices.openUrl(QUrl(self._address))

    def _onAuthRequired(self, authenticator):
        Logger.log("e", "Not yet implemented: authentication")

    def _onSslErrors(self, reply, errors):
        Logger.log("e", "Ssl errors: %s", repr(errors))

        errorString = ", ".join([str(error.errorString()) for error in errors])
        self.setErrorText(errorString)
        message = Message(catalog.i18nc("@info:progress", "One or more SSL errors has occurred: {0}").format(errorString), 0, False, -1)
        message.show()

    def _onUploadProgress(self, bytesSent, bytesTotal):
        if bytesTotal > 0:
            self._onProgress(int(bytesSent * 100 / bytesTotal))
        Logger.log("d", "bytesSent: %s", str(bytesSent))
        Logger.log("d", "bytesTotal: %s", str(bytesTotal))


    def _onDownloadProgress(self, bytesReceived, bytesTotal):
        pass

    def _onNetworkError(self, errorCode):
        Logger.log("e", "_onNetworkError: %s", repr(errorCode))
        Logger.log("e", "_onNetworkError: %s", str(errorCode))
        if self._message:
            self._message.hide()
        self._message = None
        self.setErrorText(str(errorCode))

    def _cancelUpload(self):
        if self._message:
            self._message.hide()
        self._message = None
        self._reply.abort()

    def close(self):
        Logger.log("d", "Closing connection of printer %s with ip %s", self._key, self._address)
        self._updateJobState("")
        self.setConnectionState(ConnectionState.closed)
        if self._progress_message:
            self._progress_message.hide()

    def getProperties(self):
        return self._properties

    @pyqtSlot(str, result = str)
    def getProperty(self, key):
        key = key.encode("utf-8")
        if key in self._properties:
            return self._properties.get(key, b"").decode("utf-8")
        else:
            return ""

    ##  Get the unique key of this machine
    #   \return key String containing the key of the machine.
    @pyqtSlot(result = str)
    def getKey(self):
        return self._key

    ##  Name of the printer (as returned from the zeroConf properties)
    @pyqtProperty(str, constant = True)
    def name(self):
        return self._properties.get(b"name", b"").decode("utf-8")

    ##  Firmware version (as returned from the zeroConf properties)
    @pyqtProperty(str, constant=True)
    def firmwareVersion(self):
        return self._properties.get(b"firmware_version", b"").decode("utf-8")

    ## IPadress of this printer
    @pyqtProperty(str, constant=True)
    def ipAddress(self):
        return self._address


    ##  Get the unique key of this machine
    #   \return key String containing the key of the machine.
    @pyqtSlot(result = str)
    def getKey(self):
        return self._key

    ##  Name of the instance (as returned from the zeroConf properties)
    @pyqtProperty(str, constant = True)
    def name(self):
        return self._key

    ##  Version (as returned from the zeroConf properties)
    @pyqtProperty(str, constant=True)
    def wirelessprintVersion(self):
        return self._properties.get(b"version", b"").decode("utf-8")

    ## IPadress of this instance
    @pyqtProperty(str, constant=True)
    def ipAddress(self):
        return self._address

    ## port of this instance
    @pyqtProperty(int, constant=True)
    def port(self):
        return self._port

    ## path of this instance
    @pyqtProperty(str, constant=True)
    def path(self):
        return self._path

    ## absolute url of this instance
    @pyqtProperty(str, constant=True)
    def baseURL(self):
        return self._base_url

    def isConnected(self):
        return self._connection_state != ConnectionState.closed and self._connection_state != ConnectionState.error

    ##  Start requesting data from the instance
    def connect(self):
        self.setConnectionState(ConnectionState.connected)
