import sys
from machine import Pin
from machine import const
from time import usleep



class Pofolink():
    VERB_QUIET = 0
    VERB_ERRORS = 1
    VERB_COUNTER = 2
    VERB_FLOWCONTROL = 3

    _PAYLOAD_BUFSIZE = 60000
    _CONTROL_BUFSIZE = 100
    _LIST_BUFSIZE = 2000
    _MAX_FILENAME_LEN = 79

    _transmitInit = [
        # Offset 0: Function
        0x03, 0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,

        # Offset 7: Data length
        0, 0, 0, 0

        # Offset 11: Path
        ]
    _transmitOverwrite = [ 0x05, 0x00, 0x70 ]
    _transmitCancel = [ 0x00, 0x00, 0x00 ]

    _receiveInit = [
        # Offset 0: Function
        0x06,

        # Offset 2: Buffer size (28672 bytes)
        0x00, 0x70
    ]
    _receiveFinish = [ 0x20, 0x00, 0x03 ]


    def __init__(self, dataIn, dataOut, clkIn, clkOut):
        self._din = dataIn
        self._dout = dataOut
        self._cin = clkIn
        self._cout = clkOut


    # Output a byte to the data register of the parallel port
    def _writePort(self, data):
        self._dout.value(data & 1)
        self._cout.value(data & 2)


    def _waitClockHigh(self):
        while not self.cin.value:
            pass


    def _waitClockLow(self):
        while self._cin.value:
            pass


    def _getBit(self):
        return self.din.value


    #  Receives one byte serially, MSB first
    #  One bit is read on every falling and every rising slope of the clock signal.
    def _receiveByte(self):
        recv = 0x00

        for _ in range(0, 4):
            self._waitClockLow()
            recv = (recv << 1) | self._getBit()
            self._writePort(0) # Clear clock
            self._waitClockHigh()
            recv = (recv << 1) | self._getBit()
            self._writePort(2) # Set clock

        return recv


    # Transmits one byte serially, MSB first
    # One bit is transmitted on every falling and every rising slope of the clock signal.
    def _sendByte(self, data):
        b = 0x00

        usleep(250)
        for _ in range(0, 4):
            b = ((data & 0x80) >> 7) | 2     # Output data bit
            self._writePort(b)

            b = (data & 0x80) >> 7           # Set clock low
            self._writePort(b)

            data = data << 1
            self._waitClockLow()

            b = (data & 0x80) >> 7           # Output data bit
            self._writePort(b)

            b = ((data & 0x80) >> 7) | 2     # Set clock high
            self._writePort(b)

            data = data << 1
            self._waitClockHigh()



    # This function transmits a block of data.
    # Call int 61h with AX=3002 (open) and AX=3001 (receive) on the Portfolio
    def _sendBlock(self, *pData, len, verbosity):
        recv = 0x00
        i = 0x00
        lenH, lenL = 0x00
        checksum = 0x00

        if not len:
            return

        recv = self._receiveByte()

        if (recv == 'Z'):
            if (verbosity >= VERB_FLOWCONTROL):
                print("Portfolio ready for receiving.")
        else:
            if (verbosity >= VERB_ERRORS):
                print( "Portfolio not ready!")

            return False


        usleep(50000)
        self._sendByte(0x0a5)

        lenH = len >> 8
        lenL = len & 255
        self._sendByte(lenL); checksum -= lenL
        self._sendByte(lenH); checksum -= lenH

        for i in range(0, len):
            recv = pData[i]
            sendByte(recv); checksum -= recv

            if (verbosity >= VERB_COUNTER):
                print("Sent {} of {} bytes.", i + 1, len)

            sendByte(checksum)

            if (verbosity >= VERB_COUNTER):
                print("");

            recv = receiveByte();

            if (recv == checksum):
                if (verbosity >= VERB_FLOWCONTROL):
                    print( "checksum OK")
            else:
                if (verbosity >= VERB_ERRORS):
                    print( "checksum ERR: {}", recv)

                return False


    # This function receives a block of data and returns its length in bytes.
    # Call int 61h with AX=3002 (open) and AX=3000 (transmit) on the Portfolio.
    def _receiveBlock(self, pData, maxLen, verbosity):
        len, i = 0
        lenH, lenL = 0x00
        checksum = 0x00
        recv = 0x00

        self._sendByte('Z')
        recv = self._receiveByte()

        if (recv == 0x0a5):
            if (verbosity >= VERB_FLOWCONTROL):
                print( "Acknowledge OK")

        else:
            if (verbosity >= VERB_ERRORS):
                print( "Acknowledge ERROR (received {} instead of A5)\n", recv)

            return False

        lenL = self._receiveByte();  checksum += lenL
        lenH = self._receiveByte();  checksum += lenH
        len = (lenH << 8) | lenL

        if (len > maxLen):
            if (verbosity >= VERB_ERRORS):
                print( "Receive buffer too small ({} instead of {} bytes).", maxLen, len)

            return 0

        for i in range(0, len):
            recv = receiveByte()
            checksum += recv
            pData[i] = recv

            if (verbosity >= VERB_COUNTER):
                sys.out.write(".")

        if (verbosity >= VERB_COUNTER):
            print("")

        recv = receiveByte()

        if ((256 - recv) == checksum):
            if (verbosity >= VERB_FLOWCONTROL):
                print("checksum OK")
        else:
            if (verbosity >= VERB_ERRORS):
                print("checksum ERR {} {}", (256 - recv), checksum)

            return False

        usleep(100)
        sendByte(256 - checksum)

        return len


    def _syncTick(self):
        self._waitClockLow()
        self._writePort(0)

        self._waitClockHigh()
        self._writePort(2)


    def detectPortfolio(self):
        recv = 0x00

        self._syncTick()
        recv = self._receiveByte()

        return recv == 90


    def listFiles(self, pattern):
        i, num = 0x00
        name = ""

        print("Fetching directory listing for {}\n", pattern)

        #strncpy((char*)receiveInit + 3, pattern, _MAX_FILENAME_LEN)
        data = bytearray()
        data.extend(self._receiveInit)
        data.extend(pattern.encode()[:self._MAX_FILENAME_LEN])

        #sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS)
        #receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_ERRORS)

        num = payload[0] + (payload[1] << 8)
        if (num == 0):
            DBG_OUTPUT_PORT.printf("No files.\n")

        name = payload + 2

        for _ in range (0, n):
            DBG_OUTPUT_PORT.printf("%s\n", name);
            name += strlen(name) + 1;


def main():
    dIn = Pin(6, Pin.INPUT)
    dOut = Pin(7, Pin.INPUT)

    cIn = Pin(8, Pin.OUTPUT)
    cOut = Pin(5, Pin.OUTPUT)

    link = Pofolink(dIn, dOut, cIn, cOut)

    link.detectPortfolio()


main()
