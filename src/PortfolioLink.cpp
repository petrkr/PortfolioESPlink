#include "PortfolioLink.h"

#include <string.h>

namespace {
const unsigned char TRANSMIT_OVERWRITE[3] = {0x05, 0x00, 0x70};
const unsigned char TRANSMIT_CANCEL[3] = {0x00, 0x00, 0x00};
const uint32_t CLOCK_TIMEOUT_US = 250000;
const TickType_t DETECT_INTERVAL = pdMS_TO_TICKS(100);
const uint8_t DETECT_MISSES_TO_DISCONNECT = 8;
const TickType_t JOB_WAIT = pdMS_TO_TICKS(30000);
}

PortfolioLink::PortfolioLink(Print& log) : log_(log) {}

PortfolioLink::~PortfolioLink() {
  free(payload_);
  free(controlData_);
}

bool PortfolioLink::begin(const PortfolioPins& pins) {
  pins_ = pins;
  setupPort();

  payload_ = static_cast<unsigned char*>(malloc(PAYLOAD_BUFSIZE));
  controlData_ = static_cast<unsigned char*>(malloc(CONTROL_BUFSIZE));
  jobQueue_ = xQueueCreate(1, sizeof(Job*));

  if (!payload_ || !controlData_ || !jobQueue_) {
    log_.println("PortfolioLink: init failed");
    return false;
  }

  BaseType_t ok = xTaskCreate(taskThunk, "pofo", 8192, this, 1, &task_);
  return ok == pdPASS;
}

PortfolioStatus PortfolioLink::status() const {
  return status_;
}

PortfolioResult PortfolioLink::lastResult() const {
  return lastResult_;
}

bool PortfolioLink::isConnected() const {
  return status_ == PortfolioStatus::Connected;
}

bool PortfolioLink::isBusy() const {
  return status_ == PortfolioStatus::Busy;
}

PortfolioResult PortfolioLink::listFiles(const char* pattern, String& output) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  Job job{};
  job.type = JobType::List;
  job.pofoPath = pattern;
  job.listOutput = &output;
  job.result = PortfolioResult::Unknown;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  Job* jobPtr = &job;
  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &jobPtr, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    output = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return job.result;
}

PortfolioResult PortfolioLink::uploadFile(fs::FS& fs, const char* localPath, const char* pofoPath, bool overwrite) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  Job job{};
  job.type = JobType::Upload;
  job.fs = &fs;
  job.localPath = localPath;
  job.pofoPath = pofoPath;
  job.overwrite = overwrite;
  job.result = PortfolioResult::Unknown;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  Job* jobPtr = &job;
  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &jobPtr, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return job.result;
}

void PortfolioLink::taskThunk(void* arg) {
  static_cast<PortfolioLink*>(arg)->taskLoop();
}

void PortfolioLink::taskLoop() {
  uint8_t misses = DETECT_MISSES_TO_DISCONNECT;
  TickType_t lastDetect = 0;

  for (;;) {
    Job* job = nullptr;
    if (xQueueReceive(jobQueue_, &job, 0) == pdTRUE && job) {
      if (job->type == JobType::List && job->listOutput) {
        job->result = runList(job->pofoPath, *job->listOutput);
      } else if (job->type == JobType::Upload && job->fs) {
        job->result = runUpload(*job->fs, job->localPath, job->pofoPath, job->overwrite);
      } else {
        job->result = PortfolioResult::Unknown;
      }

      finishJob(job->result);
      xSemaphoreGive(job->done);
      continue;
    }

    TickType_t now = xTaskGetTickCount();
    if (now - lastDetect >= DETECT_INTERVAL) {
      lastDetect = now;
      if (detectOnce()) {
        misses = 0;
        status_ = PortfolioStatus::Connected;
      } else if (misses < DETECT_MISSES_TO_DISCONNECT) {
        misses++;
      } else {
        status_ = PortfolioStatus::Disconnected;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void PortfolioLink::finishJob(PortfolioResult result) {
  lastResult_ = result;
  status_ = result == PortfolioResult::Ok ? PortfolioStatus::Connected : PortfolioStatus::Disconnected;
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

bool PortfolioLink::waitClockHigh(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (!digitalRead(pins_.inClock)) {
    if (micros() - start >= timeoutUs) {
      return false;
    }
  }
  return true;
}

bool PortfolioLink::waitClockLow(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(pins_.inClock)) {
    if (micros() - start >= timeoutUs) {
      return false;
    }
  }
  return true;
}

unsigned char PortfolioLink::getBit() {
  return digitalRead(pins_.inData);
}

bool PortfolioLink::receiveByte(unsigned char& out) {
  unsigned char recv = 0;

  for (int i = 0; i < 4; i++) {
    if (!waitClockLow(CLOCK_TIMEOUT_US)) {
      return false;
    }
    recv = (recv << 1) | getBit();
    writePort(0);

    if (!waitClockHigh(CLOCK_TIMEOUT_US)) {
      return false;
    }
    recv = (recv << 1) | getBit();
    writePort(2);
  }

  out = recv;
  return true;
}

bool PortfolioLink::sendByte(unsigned char data) {
  delayMicroseconds(250);

  for (int i = 0; i < 4; i++) {
    unsigned char b = ((data & 0x80) >> 7) | 2;
    writePort(b);
    b = (data & 0x80) >> 7;
    writePort(b);

    data = data << 1;
    if (!waitClockLow(CLOCK_TIMEOUT_US)) {
      return false;
    }

    b = (data & 0x80) >> 7;
    writePort(b);
    b = ((data & 0x80) >> 7) | 2;
    writePort(b);

    data = data << 1;
    if (!waitClockHigh(CLOCK_TIMEOUT_US)) {
      return false;
    }
  }

  return true;
}

bool PortfolioLink::sendBlock(const unsigned char* data, unsigned int len, Verbosity verbosity) {
  if (!len) {
    return true;
  }

  unsigned char recv = 0;
  if (!receiveByte(recv) || recv != 'Z') {
    if (verbosity >= VERB_ERRORS) {
      log_.println("Portfolio not ready");
    }
    return false;
  }

  delayMicroseconds(50000);
  if (!sendByte(0x0a5)) {
    return false;
  }

  unsigned char checksum = 0;
  unsigned char lenH = len >> 8;
  unsigned char lenL = len & 255;
  if (!sendByte(lenL)) {
    return false;
  }
  checksum -= lenL;

  if (!sendByte(lenH)) {
    return false;
  }
  checksum -= lenH;

  for (unsigned int i = 0; i < len; i++) {
    recv = data[i];
    if (!sendByte(recv)) {
      return false;
    }
    checksum -= recv;

    if (verbosity >= VERB_COUNTER) {
      log_.printf("Sent %d of %d bytes.\r\n", i + 1, len);
    }
  }

  if (!sendByte(checksum)) {
    return false;
  }

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  if (!receiveByte(recv) || recv != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("checksum ERR: got %d expected %d\n", recv, checksum);
    }
    return false;
  }

  return true;
}

int PortfolioLink::receiveBlock(unsigned char* data, int maxLen, Verbosity verbosity) {
  unsigned char checksum = 0;

  if (!sendByte('Z')) {
    return -1;
  }

  unsigned char recv = 0;
  if (!receiveByte(recv) || recv != 0x0a5) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("Acknowledge ERROR (received %2X instead of A5)\n", recv);
    }
    return -1;
  }

  unsigned char lenL = 0;
  unsigned char lenH = 0;
  if (!receiveByte(lenL) || !receiveByte(lenH)) {
    return -1;
  }
  checksum += lenL;
  checksum += lenH;
  unsigned int len = (lenH << 8) | lenL;

  if (len > static_cast<unsigned int>(maxLen)) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("Receive buffer too small (%d instead of %d bytes).\n", maxLen, len);
    }
    return -1;
  }

  for (unsigned int i = 0; i < len; i++) {
    if (!receiveByte(recv)) {
      return -1;
    }
    checksum += recv;
    data[i] = recv;

    if (verbosity >= VERB_COUNTER) {
      log_.print(".");
    }
  }

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  if (!receiveByte(recv) || static_cast<unsigned char>(256 - recv) != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("checksum ERR %d %d\n", static_cast<unsigned char>(256 - recv), checksum);
    }
    return -1;
  }

  delayMicroseconds(100);
  if (!sendByte(static_cast<unsigned char>(256 - checksum))) {
    return -1;
  }

  return len;
}

bool PortfolioLink::detectOnce() {
  unsigned char recv = 0;
  return receiveByte(recv) && recv == 'Z';
}

PortfolioResult PortfolioLink::runList(const char* pattern, String& output) {
  log_.printf("Fetching directory listing for %s\n", pattern);

  receiveInit_[0] = 6;
  strncpy(reinterpret_cast<char*>(receiveInit_) + 3, pattern, MAX_FILENAME_LEN);
  receiveInit_[sizeof(receiveInit_) - 1] = '\0';

  if (!sendBlock(receiveInit_, sizeof(receiveInit_), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS) < 0) {
    return PortfolioResult::Unknown;
  }

  int num = payload_[0] + (payload_[1] << 8);
  char* name = reinterpret_cast<char*>(payload_) + 2;
  output = "";

  for (int i = 0; i < num; i++) {
    if (i > 0) {
      output += '\n';
    }
    output += name;
    name += strlen(name) + 1;
  }

  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runUpload(fs::FS& fs, const char* filename, const char* dest, bool overwrite) {
  log_.println("transmitFile begin");

  File file = fs.open(filename);
  if (!file) {
    log_.printf("File not found: %s\n", filename);
    return PortfolioResult::Unknown;
  }

  size_t len = file.size();
  log_.printf("transmitFile: File length: %d\n", len);

  if (len > 32 * 1024 * 1024) {
    log_.printf("Skipping %s.\n", file.name());
    file.close();
    return PortfolioResult::Unknown;
  }

  file.seek(0, SeekSet);

  transmitInit_[7] = len & 255;
  transmitInit_[8] = (len >> 8) & 255;
  transmitInit_[9] = (len >> 16) & 255;

  strncpy(reinterpret_cast<char*>(transmitInit_) + 11, dest, MAX_FILENAME_LEN);
  transmitInit_[sizeof(transmitInit_) - 1] = '\0';

  if (!sendBlock(transmitInit_, sizeof(transmitInit_), VERB_ERRORS)) {
    file.close();
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    file.close();
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    log_.println("Invalid destination file");
    file.close();
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x20) {
    log_.print("File exists on Portfolio");
    if (overwrite) {
      log_.println(" and is being overwritten.");
      if (!sendBlock(TRANSMIT_OVERWRITE, sizeof(TRANSMIT_OVERWRITE), VERB_ERRORS)) {
        file.close();
        return PortfolioResult::Unknown;
      }
    } else {
      log_.println("! Overwrite disabled.");
      sendBlock(TRANSMIT_CANCEL, sizeof(TRANSMIT_CANCEL), VERB_ERRORS);
      file.close();
      return PortfolioResult::Unknown;
    }
  }

  int blocksize = controlData_[1] + (controlData_[2] << 8);
  if (blocksize > static_cast<int>(PAYLOAD_BUFSIZE)) {
    log_.println("Payload buffer too small");
    file.close();
    return PortfolioResult::Unknown;
  }

  while (len > static_cast<size_t>(blocksize)) {
    file.readBytes(reinterpret_cast<char*>(payload_), blocksize);
    if (!sendBlock(payload_, blocksize, VERB_COUNTER)) {
      file.close();
      return PortfolioResult::Unknown;
    }
    len -= blocksize;
  }

  file.readBytes(reinterpret_cast<char*>(payload_), len);
  if (len && !sendBlock(payload_, len, VERB_COUNTER)) {
    file.close();
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    file.close();
    return PortfolioResult::Unknown;
  }

  file.close();

  if (controlData_[0] != 0x20) {
    log_.println("Transmission failed.");
    return PortfolioResult::Unknown;
  }

  return PortfolioResult::Ok;
}
