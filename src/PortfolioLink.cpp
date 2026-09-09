#include "PortfolioLink.h"

#include <string.h>
#include <time.h>

namespace {
const unsigned char TRANSMIT_OVERWRITE[3] = {0x05, 0x00, 0x70};
const unsigned char TRANSMIT_CANCEL[3] = {0x00, 0x00, 0x00};
const unsigned char RECEIVE_FINISH[3] = {0x20, 0x00, 0x03};
const uint32_t CLOCK_TIMEOUT_US = 2000000;
const uint32_t DETECT_TIMEOUT_US = 50000;
const TickType_t DETECT_INTERVAL = pdMS_TO_TICKS(100);
const uint8_t DETECT_MISSES_TO_DISCONNECT = 8;
const TickType_t JOB_WAIT = pdMS_TO_TICKS(30000);

// currentDosTimeDate: current UTC time as a DOS packed time/date pair (same
// encoding runListExt decodes from the LIST extended DTA - see list.inc).
// Falls back to a fixed placeholder if the ESP32 hasn't synced NTP yet
// (time(nullptr) reads as 1970 before sync - year clamps to the DOS epoch,
// which would misrepresent "unsynced" as a real 1980 date, so this checks
// for that explicitly instead).
void currentDosTimeDate(uint16_t& dosTime, uint16_t& dosDate) {
  time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);

  if (utc.tm_year + 1900 < 1980) {
    dosTime = 0;
    dosDate = 0;
    return;
  }

  dosDate = static_cast<uint16_t>(((utc.tm_year + 1900 - 1980) << 9) | ((utc.tm_mon + 1) << 5) | utc.tm_mday);
  dosTime = static_cast<uint16_t>((utc.tm_hour << 11) | (utc.tm_min << 5) | (utc.tm_sec / 2));
}
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
  jobQueue_ = xQueueCreate(1, sizeof(Job));

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

PortfolioTransferPhase PortfolioLink::transferPhase() const {
  return transferPhase_;
}

size_t PortfolioLink::transferDone() const {
  return transferDone_;
}

size_t PortfolioLink::transferTotal() const {
  return transferTotal_;
}

bool PortfolioLink::isConnected() const {
  return status_ == PortfolioStatus::Connected;
}

bool PortfolioLink::isBusy() const {
  return status_ == PortfolioStatus::Busy;
}

bool PortfolioLink::startUpload(fs::FS& fs, const char* localPath, const char* pofoPath, bool overwrite) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return false;
  }

  Job job{};
  job.type = JobType::Upload;
  job.fs = &fs;
  job.overwrite = overwrite;
  strncpy(job.localPath, localPath, MAX_FILENAME_LEN);
  strncpy(job.pofoPath, pofoPath, MAX_FILENAME_LEN);
  job.localPath[MAX_FILENAME_LEN] = '\0';
  job.pofoPath[MAX_FILENAME_LEN] = '\0';

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE) {
    status_ = PortfolioStatus::Disconnected;
    return false;
  }
  return true;
}

bool PortfolioLink::startDownload(fs::FS& fs, const char* pofoPath, const char* localPath, bool overwrite) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return false;
  }

  Job job{};
  job.type = JobType::Download;
  job.fs = &fs;
  job.overwrite = overwrite;
  strncpy(job.localPath, localPath, MAX_FILENAME_LEN);
  strncpy(job.pofoPath, pofoPath, MAX_FILENAME_LEN);
  job.localPath[MAX_FILENAME_LEN] = '\0';
  job.pofoPath[MAX_FILENAME_LEN] = '\0';

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE) {
    status_ = PortfolioStatus::Disconnected;
    return false;
  }
  return true;
}

PortfolioResult PortfolioLink::listFiles(const char* pattern, String& output) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;

  Job job{};
  job.type = JobType::List;
  strncpy(job.pofoPath, pattern, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.listOutput = &output;
  job.resultOut = &result;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    output = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::listFilesExtended(const char* pattern, String& output) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;

  Job job{};
  job.type = JobType::ListExt;
  strncpy(job.pofoPath, pattern, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.listOutput = &output;
  job.resultOut = &result;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    output = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::sendRaw(const uint8_t* data, size_t len, String& response) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;

  Job job{};
  job.type = JobType::Raw;
  job.rawData = data;
  job.rawLen = len;
  job.rawResponse = &response;
  job.resultOut = &result;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    response = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::helloDaemon(bool& present, uint32_t& buildId, uint8_t& version, uint8_t& capabilities) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  present = false;
  buildId = 0;
  version = 0;
  capabilities = 0;

  Job job{};
  job.type = JobType::Hello;
  job.resultOut = &result;
  job.helloPresent = &present;
  job.helloBuildId = &buildId;
  job.helloVersion = &version;
  job.helloCapabilities = &capabilities;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || xSemaphoreTake(job.done, JOB_WAIT) != pdTRUE) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

void PortfolioLink::taskThunk(void* arg) {
  static_cast<PortfolioLink*>(arg)->taskLoop();
}

void PortfolioLink::taskLoop() {
  uint8_t misses = DETECT_MISSES_TO_DISCONNECT;
  TickType_t lastDetect = 0;

  for (;;) {
    Job job{};
    if (xQueueReceive(jobQueue_, &job, 0) == pdTRUE) {
      PortfolioResult result;
      if (job.type == JobType::List && job.listOutput) {
        result = runList(job.pofoPath, *job.listOutput);
      } else if (job.type == JobType::ListExt && job.listOutput) {
        result = runListExt(job.pofoPath, *job.listOutput);
      } else if (job.type == JobType::Upload && job.fs) {
        result = runUpload(*job.fs, job.localPath, job.pofoPath, job.overwrite);
      } else if (job.type == JobType::Download && job.fs) {
        result = runDownload(*job.fs, job.pofoPath, job.localPath, job.overwrite);
      } else if (job.type == JobType::Raw && job.rawResponse) {
        result = runRaw(job.rawData, job.rawLen, *job.rawResponse);
      } else if (job.type == JobType::Hello && job.helloPresent) {
        result = runHello(*job.helloPresent, *job.helloBuildId, *job.helloVersion, *job.helloCapabilities);
      } else {
        result = PortfolioResult::Unknown;
      }

      finishJob(result);
      if (job.resultOut) {
        *job.resultOut = result;
      }
      if (job.done) {
        xSemaphoreGive(job.done);
      }
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
  transferPhase_ = PortfolioTransferPhase::Idle;
  status_ = result == PortfolioResult::Unknown ? PortfolioStatus::Disconnected : PortfolioStatus::Connected;
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

bool PortfolioLink::receiveByte(unsigned char& out, uint32_t timeoutUs) {
  unsigned char recv = 0;

  for (int i = 0; i < 4; i++) {
    if (!waitClockLow(timeoutUs)) {
      return false;
    }
    recv = (recv << 1) | getBit();
    writePort(0);

    if (!waitClockHigh(timeoutUs)) {
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
  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || recv != 'Z') {
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
    if (transferTotal_ > 0) {
      transferDone_++;
    }
  }

  if (!sendByte(checksum)) {
    return false;
  }

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || recv != checksum) {
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
  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || recv != 0x0a5) {
    if (verbosity >= VERB_ERRORS) {
      log_.printf("Acknowledge ERROR (received %2X instead of A5)\n", recv);
    }
    return -1;
  }

  unsigned char lenL = 0;
  unsigned char lenH = 0;
  if (!receiveByte(lenL, CLOCK_TIMEOUT_US) || !receiveByte(lenH, CLOCK_TIMEOUT_US)) {
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
    if (!receiveByte(recv, CLOCK_TIMEOUT_US)) {
      return -1;
    }
    checksum += recv;
    data[i] = recv;

    if (verbosity >= VERB_COUNTER) {
      log_.print(".");
    }
    if (transferTotal_ > 0) {
      transferDone_++;
    }
  }

  if (verbosity >= VERB_COUNTER) {
    log_.println();
  }

  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || static_cast<unsigned char>(256 - recv) != checksum) {
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
  return receiveByte(recv, DETECT_TIMEOUT_US) && recv == 'Z';
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

PortfolioResult PortfolioLink::runListExt(const char* pattern, String& output) {
  log_.printf("Fetching extended directory listing for %s\n", pattern);

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x86;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, pattern, MAX_FILENAME_LEN);
  request[sizeof(request) - 1] = '\0';

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  int received = receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS);
  if (received < 0) {
    return PortfolioResult::Unknown;
  }

  if (received < 2) {
    return PortfolioResult::Unknown;
  }

  int num = payload_[0] + (payload_[1] << 8);
  size_t pos = 2;
  output = "";

  for (int i = 0; i < num; i++) {
    if (pos + 9 > static_cast<size_t>(received)) {
      break;
    }

    uint8_t attr = payload_[pos];
    uint32_t size = static_cast<uint32_t>(payload_[pos + 1]) | (static_cast<uint32_t>(payload_[pos + 2]) << 8) |
                    (static_cast<uint32_t>(payload_[pos + 3]) << 16) | (static_cast<uint32_t>(payload_[pos + 4]) << 24);
    uint16_t date = static_cast<uint16_t>(payload_[pos + 5]) | (static_cast<uint16_t>(payload_[pos + 6]) << 8);
    uint16_t time = static_cast<uint16_t>(payload_[pos + 7]) | (static_cast<uint16_t>(payload_[pos + 8]) << 8);
    pos += 9;

    const char* name = reinterpret_cast<char*>(payload_) + pos;
    size_t nameLen = strnlen(name, received - pos);
    pos += nameLen + 1;

    // DOS packed date/time (Find First/Next DTA format): date = year-1980
    // (bits 15-9) / month (8-5) / day (4-0); time = hour (15-11) / minute
    // (10-5) / second/2 (4-0).
    int year = ((date >> 9) & 0x7F) + 1980;
    int month = (date >> 5) & 0x0F;
    int day = date & 0x1F;
    int hour = (time >> 11) & 0x1F;
    int minute = (time >> 5) & 0x3F;
    int second = (time & 0x1F) * 2;

    if (i > 0) {
      output += '\n';
    }
    output += (attr & 0x10) ? "D" : "F";
    output += ',';
    output += size;
    output += ',';
    char stamp[20];
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
    output += stamp;
    output += ',';
    output += name;
  }

  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runRaw(const uint8_t* data, size_t len, String& response) {
  log_.printf("Sending raw block, %u bytes\n", static_cast<unsigned>(len));

  if (!sendBlock(data, len, VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  int received = receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS);
  if (received < 0) {
    return PortfolioResult::Unknown;
  }

  response = "";
  static const char hexDigits[] = "0123456789ABCDEF";
  for (int i = 0; i < received; i++) {
    response += hexDigits[(payload_[i] >> 4) & 0x0F];
    response += hexDigits[payload_[i] & 0x0F];
  }

  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runHello(bool& present, uint32_t& buildId, uint8_t& version, uint8_t& capabilities) {
  log_.println("Probing for PFTD (HELLO)");

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x80;

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  int received = receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS);
  if (received < 0) {
    return PortfolioResult::Unknown;
  }

  if (received < 12 || memcmp(payload_, "PFD1", 4) != 0) {
    present = false;
    return PortfolioResult::Ok;
  }

  present = true;
  buildId = static_cast<uint32_t>(payload_[4]) | (static_cast<uint32_t>(payload_[5]) << 8) |
            (static_cast<uint32_t>(payload_[6]) << 16) | (static_cast<uint32_t>(payload_[7]) << 24);
  version = payload_[8];
  capabilities = payload_[9];
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

  uint16_t dosTime = 0;
  uint16_t dosDate = 0;
  currentDosTimeDate(dosTime, dosDate);
  transmitInit_[3] = dosTime & 0xFF;
  transmitInit_[4] = (dosTime >> 8) & 0xFF;
  transmitInit_[5] = dosDate & 0xFF;
  transmitInit_[6] = (dosDate >> 8) & 0xFF;

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
    log_.println("Invalid destination file (bad path or disk full)");
    file.close();
    return PortfolioResult::InvalidPath;
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

  transferPhase_ = PortfolioTransferPhase::PofoUpload;
  transferDone_ = 0;
  transferTotal_ = len;

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

  delayMicroseconds(50000);

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    file.close();
    return PortfolioResult::Unknown;
  }

  file.close();

  log_.printf("Upload finish response: %02X %02X %02X\n", controlData_[0], controlData_[1], controlData_[2]);

  if (controlData_[0] == 0x10) {
    log_.println("Invalid destination path (bad path or disk full).");
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.println("Transmission failed.");
    return PortfolioResult::Unknown;
  }

  transferDone_ = transferTotal_;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runDownload(fs::FS& fs, const char* pofoPath, const char* localPath, bool overwrite) {
  log_.println("receiveFile begin");

  if (!overwrite && fs.exists(localPath)) {
    log_.printf("Local file already exists: %s\n", localPath);
    return PortfolioResult::Unknown;
  }

  strncpy(reinterpret_cast<char*>(receiveFileInit_) + 3, pofoPath, MAX_FILENAME_LEN);
  receiveFileInit_[sizeof(receiveFileInit_) - 1] = '\0';

  if (!sendBlock(receiveFileInit_, sizeof(receiveFileInit_), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 0) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    log_.println("File not found on Portfolio");
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.printf("Unexpected receive response: %02X\n", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  size_t total = controlData_[7] | (static_cast<size_t>(controlData_[8]) << 8) |
                 (static_cast<size_t>(controlData_[9]) << 16) | (static_cast<size_t>(controlData_[10]) << 24);

  log_.printf("receiveFile: File length: %u\n", total);

  File file = fs.open(localPath, FILE_WRITE);
  if (!file) {
    log_.printf("Cannot create local file: %s\n", localPath);
    return PortfolioResult::Unknown;
  }

  transferPhase_ = PortfolioTransferPhase::PofoUpload;
  transferDone_ = 0;
  transferTotal_ = total;

  while (total > 0) {
    int len = receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_COUNTER);
    if (len < 0) {
      file.close();
      return PortfolioResult::Unknown;
    }
    file.write(payload_, len);
    total -= len;
    transferDone_ = transferTotal_ - total;
  }

  file.close();

  if (!sendBlock(RECEIVE_FINISH, sizeof(RECEIVE_FINISH), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  return PortfolioResult::Ok;
}
