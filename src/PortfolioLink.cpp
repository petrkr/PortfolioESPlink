#include "PortfolioLink.h"

#include <string.h>

namespace {
const unsigned char TRANSMIT_OVERWRITE[3] = {0x05, 0x00, 0x70};
const unsigned char TRANSMIT_CANCEL[3] = {0x00, 0x00, 0x00};
}

PortfolioLink::PortfolioLink(Print& log) : log_(log) {}

PortfolioLink::~PortfolioLink() {
  free(payload_);
  free(controlData_);
  free(list_);
}

bool PortfolioLink::begin(const PortfolioPins& pins) {
  pins_ = pins;
  setupPort();

  payload_ = static_cast<unsigned char*>(malloc(PAYLOAD_BUFSIZE));
  controlData_ = static_cast<unsigned char*>(malloc(CONTROL_BUFSIZE));
  list_ = static_cast<unsigned char*>(malloc(LIST_BUFSIZE));

  if (!payload_ || !controlData_ || !list_) {
    log_.println("PortfolioLink: out of memory");
    return false;
  }

  return true;
}

void PortfolioLink::setForce(bool enabled) {
  force_ = enabled;
}

void PortfolioLink::setupPort() {
  pinMode(pins_.outData, OUTPUT);
  pinMode(pins_.outClock, OUTPUT);
  pinMode(pins_.inClock, INPUT_PULLUP);
  pinMode(pins_.inData, INPUT_PULLUP);
}

void PortfolioLink::writePort(unsigned char data) {
  digitalWrite(pins_.outData, data & 1);
  digitalWrite(pins_.outClock, data & 2);
}

void PortfolioLink::waitClockHigh() {
  while (!digitalRead(pins_.inClock)) {
    delay(0);
  }
}

void PortfolioLink::waitClockLow() {
  while (digitalRead(pins_.inClock)) {
    delay(0);
  }
}

unsigned char PortfolioLink::getBit() {
  return digitalRead(pins_.inData);
}

unsigned char PortfolioLink::receiveByte() {
  unsigned char recv = 0;

  for (int i = 0; i < 4; i++) {
    waitClockLow();
    recv = (recv << 1) | getBit();
    writePort(0);
    waitClockHigh();
    recv = (recv << 1) | getBit();
    writePort(2);
  }

  return recv;
}

void PortfolioLink::sendByte(unsigned char data) {
  delayMicroseconds(250);

  for (int i = 0; i < 4; i++) {
    unsigned char b = ((data & 0x80) >> 7) | 2;
    writePort(b);
    b = (data & 0x80) >> 7;
    writePort(b);

    data = data << 1;
    waitClockLow();

    b = (data & 0x80) >> 7;
    writePort(b);
    b = ((data & 0x80) >> 7) | 2;
    writePort(b);

    data = data << 1;
    waitClockHigh();
  }
}

bool PortfolioLink::sendBlock(const unsigned char* data, unsigned int len, Verbosity verbosity) {
  if (!len) {
    return true;
  }

  unsigned char recv = receiveByte();
  if (recv != 'Z') {
    if (verbosity >= VERB_ERRORS) {
      log_.println("Portfolio not ready");
    }
    return false;
  }

  delayMicroseconds(50000);
  sendByte(0x0a5);

  unsigned char checksum = 0;
  unsigned char lenH = len >> 8;
  unsigned char lenL = len & 255;
  sendByte(lenL);
  checksum -= lenL;
  sendByte(lenH);
  checksum -= lenH;

  for (unsigned int i = 0; i < len; i++) {
    recv = data[i];
    sendByte(recv);
    checksum -= recv;

    if (verbosity >= VERB_COUNTER) {
      log_.printf("Sent %d of %d bytes.\r\n", i + 1, len);
    }
  }

  sendByte(checksum);

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  recv = receiveByte();
  if (recv != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("checksum ERR: %d\n", recv);
    }
    return false;
  }

  return true;
}

int PortfolioLink::receiveBlock(unsigned char* data, int maxLen, Verbosity verbosity) {
  unsigned char checksum = 0;

  sendByte('Z');

  unsigned char recv = receiveByte();
  if (recv != 0x0a5) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("Acknowledge ERROR (received %2X instead of A5)\n", recv);
    }
    return -1;
  }

  unsigned char lenL = receiveByte();
  checksum += lenL;
  unsigned char lenH = receiveByte();
  checksum += lenH;
  unsigned int len = (lenH << 8) | lenL;

  if (len > static_cast<unsigned int>(maxLen)) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("Receive buffer too small (%d instead of %d bytes).\n", maxLen, len);
    }
    return -1;
  }

  for (unsigned int i = 0; i < len; i++) {
    recv = receiveByte();
    checksum += recv;
    data[i] = recv;

    if (verbosity >= VERB_COUNTER) {
      log_.print(".");
    }
  }

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  recv = receiveByte();
  if (static_cast<unsigned char>(256 - recv) != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("checksum ERR %d %d\n", static_cast<unsigned char>(256 - recv), checksum);
    }
    return -1;
  }

  delayMicroseconds(100);
  sendByte(static_cast<unsigned char>(256 - checksum));

  return len;
}

bool PortfolioLink::detect() {
  waitClockLow();
  writePort(0);
  waitClockHigh();
  writePort(2);

  unsigned char recv = receiveByte();
  log_.print(recv);

  return recv == 'Z';
}

bool PortfolioLink::listFilesJson(const char* pattern, String& output) {
  log_.printf("Fetching directory listing for %s\n", pattern);

  receiveInit_[0] = 6;
  strncpy(reinterpret_cast<char*>(receiveInit_) + 3, pattern, MAX_FILENAME_LEN);
  receiveInit_[sizeof(receiveInit_) - 1] = '\0';

  if (!sendBlock(receiveInit_, sizeof(receiveInit_), VERB_ERRORS)) {
    return false;
  }

  if (receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS) < 0) {
    return false;
  }

  int num = payload_[0] + (payload_[1] << 8);
  if (num == 0) {
    output = "{ \"files\" : [] }";
    return true;
  }

  char* name = reinterpret_cast<char*>(payload_) + 2;
  output = "{ \"files\" : [";

  for (int i = 0; i < num; i++) {
    if (i > 0) {
      output += ',';
    }
    output += "\"";
    output += name;
    output += "\"";

    name += strlen(name) + 1;
  }

  output += "]}";
  return true;
}

bool PortfolioLink::transmitFile(fs::FS& fs, const String& filename, const char* dest) {
  log_.println("transmitFile begin");

  File file = fs.open(filename);
  if (!file) {
    log_.printf("File not found: %s\n", filename.c_str());
    return false;
  }

  size_t len = file.size();
  log_.printf("transmitFile: File length: %d\n", len);

  if (len > 32 * 1024 * 1024) {
    log_.printf("Skipping %s.\n", file.name());
    file.close();
    return false;
  }

  file.seek(0, SeekSet);

  transmitInit_[7] = len & 255;
  transmitInit_[8] = (len >> 8) & 255;
  transmitInit_[9] = (len >> 16) & 255;

  strncpy(reinterpret_cast<char*>(transmitInit_) + 11, dest, MAX_FILENAME_LEN);
  transmitInit_[sizeof(transmitInit_) - 1] = '\0';

  if (!sendBlock(transmitInit_, sizeof(transmitInit_), VERB_ERRORS)) {
    file.close();
    return false;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    file.close();
    return false;
  }

  if (controlData_[0] == 0x10) {
    log_.println("Invalid destination file");
    file.close();
    return false;
  }

  if (controlData_[0] == 0x20) {
    log_.print("File exists on Portfolio");
    if (force_) {
      log_.println(" and is being overwritten.");
      if (!sendBlock(TRANSMIT_OVERWRITE, sizeof(TRANSMIT_OVERWRITE), VERB_ERRORS)) {
        file.close();
        return false;
      }
    } else {
      log_.println("! Force overwrite disabled.");
      sendBlock(TRANSMIT_CANCEL, sizeof(TRANSMIT_CANCEL), VERB_ERRORS);
      file.close();
      return false;
    }
  }

  int blocksize = controlData_[1] + (controlData_[2] << 8);
  if (blocksize > static_cast<int>(PAYLOAD_BUFSIZE)) {
    log_.println("Payload buffer too small");
    file.close();
    return false;
  }

  while (len > static_cast<size_t>(blocksize)) {
    file.readBytes(reinterpret_cast<char*>(payload_), blocksize);
    if (!sendBlock(payload_, blocksize, VERB_COUNTER)) {
      file.close();
      return false;
    }
    len -= blocksize;
  }

  file.readBytes(reinterpret_cast<char*>(payload_), len);
  if (len && !sendBlock(payload_, len, VERB_COUNTER)) {
    file.close();
    return false;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    file.close();
    return false;
  }

  file.close();

  if (controlData_[0] != 0x20) {
    log_.println("Transmission failed. Possibly disk full or destination directory missing.");
    return false;
  }

  return true;
}
