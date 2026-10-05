#include "PortfolioLink.h"

#include <string.h>
#include <time.h>
#include <esp_task_wdt.h>

namespace {
const unsigned char TRANSMIT_OVERWRITE[3] = {0x05, 0x00, 0x70};
const unsigned char TRANSMIT_CANCEL[3] = {0x00, 0x00, 0x00};
const unsigned char RECEIVE_FINISH[3] = {0x20, 0x00, 0x03};
const uint32_t CLOCK_TIMEOUT_US = 2000000;
const uint32_t DETECT_TIMEOUT_US = 50000;
// Above this, a single byte handshake is considered anomalously slow (normal
// case is well under 1ms) - logged at Debug to catch the "transfer degrades
// to ~1 byte/sec after a long idle period" issue.
const uint32_t SLOW_BYTE_WARN_US = 10000;
const TickType_t DETECT_INTERVAL = pdMS_TO_TICKS(100);
const uint8_t DETECT_MISSES_TO_DISCONNECT = 8;

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

PortfolioLink::PortfolioLink(log4mcu::Logger& log) : log_(log) {}

PortfolioLink::~PortfolioLink() {
  free(payload_);
  free(controlData_);
}

bool PortfolioLink::waitForJob(SemaphoreHandle_t done) {
  const TickType_t serviceInterval = pdMS_TO_TICKS(100);

  for (;;) {
    if (xSemaphoreTake(done, serviceInterval) == pdTRUE) {
      return true;
    }

    // Synchronous HTTP handlers run in async_tcp, which is watched by the
    // task watchdog. Keep its subscription alive while the Portfolio task
    // waits for a missing or unresponsive Portfolio.
    esp_task_wdt_reset();
  }
}

bool PortfolioLink::begin(const PortfolioPins& pins) {
  pins_ = pins;
  setupPort();

  payload_ = static_cast<unsigned char*>(malloc(PAYLOAD_BUFSIZE));
  controlData_ = static_cast<unsigned char*>(malloc(CONTROL_BUFSIZE));
  jobQueue_ = xQueueCreate(1, sizeof(Job));

  if (!payload_ || !controlData_ || !jobQueue_) {
    log_.error("PortfolioLink: init failed");
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

bool PortfolioLink::hasPFTD() const {
  return hasPFTD_;
}

uint32_t PortfolioLink::pftdBuildId() const {
  return pftdBuildId_;
}

PftdVersion PortfolioLink::pftdVersion() const {
  return pftdVersion_;
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
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    output = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::listFilesExtended(const char* pattern, String& output, uint32_t& freeBytes, uint32_t& totalBytes) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  freeBytes = 0;
  totalBytes = 0;

  Job job{};
  job.type = JobType::ListExt;
  strncpy(job.pofoPath, pattern, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.listOutput = &output;
  job.resultOut = &result;
  job.listFreeBytes = &freeBytes;
  job.listTotalBytes = &totalBytes;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
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
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    response = "";
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::helloDaemon(bool& present, uint32_t& buildId, PftdVersion& version) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  present = false;
  buildId = 0;
  version = {};

  Job job{};
  job.type = JobType::Hello;
  job.resultOut = &result;
  job.helloPresent = &present;
  job.helloBuildId = &buildId;
  job.helloVersion = &version;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::listDrives(uint8_t& driveCount) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  driveCount = 0;

  Job job{};
  job.type = JobType::Drives;
  job.resultOut = &result;
  job.driveCount = &driveCount;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::mkdirAtari(const char* pofoPath, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::Mkdir;
  job.resultOut = &result;
  strncpy(job.pofoPath, pofoPath, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::deleteAtari(const char* pofoPath, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::Delete;
  job.resultOut = &result;
  strncpy(job.pofoPath, pofoPath, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::rmdirAtari(const char* pofoPath, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::Rmdir;
  job.resultOut = &result;
  strncpy(job.pofoPath, pofoPath, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::renameAtari(const char* oldPofoPath, const char* newPofoPath, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::Rename;
  job.resultOut = &result;
  strncpy(job.pofoPath, oldPofoPath, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  strncpy(job.newPofoPath, newPofoPath, MAX_FILENAME_LEN);
  job.newPofoPath[MAX_FILENAME_LEN] = '\0';
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::copyAtari(const char* srcPofoPath, const char* dstPofoPath, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::Copy;
  job.resultOut = &result;
  strncpy(job.pofoPath, srcPofoPath, MAX_FILENAME_LEN);
  job.pofoPath[MAX_FILENAME_LEN] = '\0';
  strncpy(job.newPofoPath, dstPofoPath, MAX_FILENAME_LEN);
  job.newPofoPath[MAX_FILENAME_LEN] = '\0';
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::getDatetimeAtari(uint16_t& dosDate, uint16_t& dosTime) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  dosDate = 0;
  dosTime = 0;

  Job job{};
  job.type = JobType::GetDatetime;
  job.resultOut = &result;
  job.dosDate = &dosDate;
  job.dosTime = &dosTime;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
    vSemaphoreDelete(job.done);
    status_ = PortfolioStatus::Disconnected;
    return PortfolioResult::Unknown;
  }

  vSemaphoreDelete(job.done);
  return result;
}

PortfolioResult PortfolioLink::setDatetimeAtari(uint16_t dosDate, uint16_t dosTime, uint8_t& errCode) {
  if (!jobQueue_ || status_ == PortfolioStatus::Busy) {
    return PortfolioResult::Unknown;
  }

  PortfolioResult result = PortfolioResult::Unknown;
  errCode = 0xFF;

  Job job{};
  job.type = JobType::SetDatetime;
  job.resultOut = &result;
  job.dosDate = &dosDate;
  job.dosTime = &dosTime;
  job.errCode = &errCode;
  job.done = xSemaphoreCreateBinary();

  if (!job.done) {
    return PortfolioResult::Unknown;
  }

  status_ = PortfolioStatus::Busy;
  if (xQueueSend(jobQueue_, &job, 0) != pdTRUE || !waitForJob(job.done)) {
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
        result = runListExt(job.pofoPath, *job.listOutput, *job.listFreeBytes, *job.listTotalBytes);
      } else if (job.type == JobType::Upload && job.fs) {
        result = runUpload(*job.fs, job.localPath, job.pofoPath, job.overwrite);
      } else if (job.type == JobType::Download && job.fs) {
        result = runDownload(*job.fs, job.pofoPath, job.localPath, job.overwrite);
      } else if (job.type == JobType::Raw && job.rawResponse) {
        result = runRaw(job.rawData, job.rawLen, *job.rawResponse);
      } else if (job.type == JobType::Hello && job.helloPresent) {
        result = runHello(*job.helloPresent, *job.helloBuildId, *job.helloVersion);
      } else if (job.type == JobType::Drives && job.driveCount) {
        result = runDrives(*job.driveCount);
      } else if (job.type == JobType::Mkdir && job.errCode) {
        result = runMkdir(job.pofoPath, *job.errCode);
      } else if (job.type == JobType::Delete && job.errCode) {
        result = runDelete(job.pofoPath, *job.errCode);
      } else if (job.type == JobType::Rmdir && job.errCode) {
        result = runRmdir(job.pofoPath, *job.errCode);
      } else if (job.type == JobType::Rename && job.errCode) {
        result = runRename(job.pofoPath, job.newPofoPath, *job.errCode);
      } else if (job.type == JobType::Copy && job.errCode) {
        result = runCopy(job.pofoPath, job.newPofoPath, *job.errCode);
      } else if (job.type == JobType::GetDatetime && job.dosDate && job.dosTime) {
        result = runGetDatetime(*job.dosDate, *job.dosTime);
      } else if (job.type == JobType::SetDatetime && job.dosDate && job.dosTime && job.errCode) {
        result = runSetDatetime(*job.dosDate, *job.dosTime, *job.errCode);
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

      // A completed job just exchanged real bytes with the Portfolio, so
      // the link is known-good - reset the passive Z-byte detector instead
      // of letting it immediately re-test on a possibly-quiet line (the
      // ROM's idle 'Z' broadcast isn't perfectly periodic, so a detectOnce()
      // right here can spuriously miss and, over a few ticks, flip status_
      // to Disconnected and trigger a needless HELLO re-probe on the next
      // job even though the connection never actually dropped).
      if (result != PortfolioResult::Unknown) {
        misses = 0;
        lastDetect = xTaskGetTickCount();
      }
      continue;
    }

    TickType_t now = xTaskGetTickCount();
    if (now - lastDetect >= DETECT_INTERVAL) {
      lastDetect = now;
      if (detectOnce()) {
        bool wasDisconnected = status_ != PortfolioStatus::Connected;
        misses = 0;
        status_ = PortfolioStatus::Connected;

        // Just transitioned disconnected -> connected: probe once for
        // PFTD so hasPFTD()/pftd*() are ready by the time anything asks,
        // without the caller ever having to issue an explicit HELLO.
        if (wasDisconnected) {
          bool present = false;
          uint32_t buildId = 0;
          PftdVersion version{};
          runHello(present, buildId, version);
          hasPFTD_ = present;
          pftdBuildId_ = buildId;
          pftdVersion_ = version;
          status_ = PortfolioStatus::Connected;
        }
      } else if (misses < DETECT_MISSES_TO_DISCONNECT) {
        misses++;
      } else {
        status_ = PortfolioStatus::Disconnected;
        hasPFTD_ = false;
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

// Both waitClock* functions poll a GPIO with a timeout up to CLOCK_TIMEOUT_US
// (2s). A pure busy-poll here starves same-core tasks on this single-core
// (ESP32-C3) target - reproduced on real hardware as async_tcp's task
// watchdog firing (Aborting/reboot) while this loop spun waiting for the
// Portfolio to answer a handshake byte that never came (observed during the
// auto-probe HELLO right after a fresh WiFi connect, before the Portfolio
// side was ready). A plain taskYIELD() every iteration was tried first and
// did NOT fix it: taskYIELD() only offers the CPU to another already-READY
// task at that exact instant - if async_tcp is blocked waiting on its own
// socket/queue at that moment (its normal idle state), taskYIELD() returns
// almost immediately and the loop keeps monopolizing the CPU, which starved
// the watchdog just the same (confirmed via a decoded crash backtrace
// showing vPortYield itself on the stack at the moment of the watchdog
// abort).
//
// Fix: busy-poll with taskYIELD() only for the first ~1 tick's worth of
// time (kBusyPollBudgetUs) - this keeps normal bit-level polling (Portfolio
// answers within microseconds during an active transfer) exactly as fast as
// before. Once that budget is exceeded, switch to vTaskDelay(1), which
// unconditionally blocks this task for at least one real FreeRTOS tick -
// guaranteed to give every other task (including async_tcp and the idle
// task that feeds its watchdog) real CPU time, unlike taskYIELD(). This
// only adds up-to-1-tick latency to the rare case of waiting out most of a
// multi-second timeout for a Portfolio that isn't responding at all.
namespace {
constexpr uint32_t kBusyPollBudgetUs = 1000;
}

bool PortfolioLink::waitClockHigh(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (!digitalRead(pins_.inClock)) {
    uint32_t elapsed = micros() - start;
    if (elapsed >= timeoutUs) {
      return false;
    }
    if (elapsed < kBusyPollBudgetUs) {
      taskYIELD();
    } else {
      vTaskDelay(1);
    }
  }
  return true;
}

bool PortfolioLink::waitClockLow(uint32_t timeoutUs) {
  uint32_t start = micros();
  while (digitalRead(pins_.inClock)) {
    uint32_t elapsed = micros() - start;
    if (elapsed >= timeoutUs) {
      return false;
    }
    if (elapsed < kBusyPollBudgetUs) {
      taskYIELD();
    } else {
      vTaskDelay(1);
    }
  }
  return true;
}

unsigned char PortfolioLink::getBit() {
  return digitalRead(pins_.inData);
}

bool PortfolioLink::receiveByte(unsigned char& out, uint32_t timeoutUs) {
  unsigned char recv = 0;
  uint32_t start = micros();

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

  uint32_t elapsed = micros() - start;
  if (elapsed >= SLOW_BYTE_WARN_US) {
    log_.debugf("receiveByte slow: %u us", static_cast<unsigned>(elapsed));
  }

  out = recv;
  return true;
}

bool PortfolioLink::sendByte(unsigned char data) {
  uint32_t start = micros();
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

  uint32_t elapsed = micros() - start;
  if (elapsed >= SLOW_BYTE_WARN_US) {
    log_.debugf("sendByte slow: %u us", static_cast<unsigned>(elapsed));
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
      log_.error("Portfolio not ready");
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
      log_.debugf("Sent %d of %d bytes.", i + 1, len);
    }
    if (transferTotal_ > 0) {
      transferDone_++;
    }
  }

  if (!sendByte(checksum)) {
    return false;
  }

  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || recv != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.errorf("checksum ERR: got %d expected %d", recv, checksum);
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
      log_.errorf("Acknowledge ERROR (received %2X instead of A5)", recv);
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
      log_.errorf("Receive buffer too small (%d instead of %d bytes).", maxLen, len);
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
      log_.debugf("Received %d of %d bytes.", i + 1, len);
    }
    if (transferTotal_ > 0) {
      transferDone_++;
    }
  }

  if (!receiveByte(recv, CLOCK_TIMEOUT_US) || static_cast<unsigned char>(256 - recv) != checksum) {
    if (verbosity >= VERB_ERRORS) {
      log_.errorf("checksum ERR %d %d", static_cast<unsigned char>(256 - recv), checksum);
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
  log_.infof("Fetching directory listing for %s", pattern);

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

PortfolioResult PortfolioLink::runListExt(const char* pattern, String& output, uint32_t& freeBytes, uint32_t& totalBytes) {
  log_.infof("Fetching extended directory listing for %s", pattern);

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

  // Free/total drive space (8 bytes: free(4B LE) + total(4B LE)) follow
  // unconditionally after the last entry - see list.inc's response
  // layout comment. Not present if the Portfolio-side PFTD build predates
  // this addition (older BUILD_ID) - guard against a short response.
  freeBytes = 0;
  totalBytes = 0;
  if (pos + 8 <= static_cast<size_t>(received)) {
    freeBytes = static_cast<uint32_t>(payload_[pos]) | (static_cast<uint32_t>(payload_[pos + 1]) << 8) |
                (static_cast<uint32_t>(payload_[pos + 2]) << 16) | (static_cast<uint32_t>(payload_[pos + 3]) << 24);
    totalBytes = static_cast<uint32_t>(payload_[pos + 4]) | (static_cast<uint32_t>(payload_[pos + 5]) << 8) |
                 (static_cast<uint32_t>(payload_[pos + 6]) << 16) | (static_cast<uint32_t>(payload_[pos + 7]) << 24);
  }

  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runRaw(const uint8_t* data, size_t len, String& response) {
  log_.infof("Sending raw block, %u bytes", static_cast<unsigned>(len));

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

PortfolioResult PortfolioLink::runHello(bool& present, uint32_t& buildId, PftdVersion& version) {
  log_.info("Probing for PFTD (HELLO)");

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
  version.major = payload_[8];
  version.minor = payload_[9];
  version.patch = payload_[10];
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runDrives(uint8_t& driveCount) {
  log_.info("Probing for drive count (DRIVES)");

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x87;

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  int received = receiveBlock(payload_, PAYLOAD_BUFSIZE, VERB_ERRORS);
  if (received < 1) {
    return PortfolioResult::Unknown;
  }

  driveCount = payload_[0];
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runMkdir(const char* pofoPath, uint8_t& errCode) {
  log_.infof("Creating directory on Portfolio: %s", pofoPath);

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x88;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, pofoPath, MAX_FILENAME_LEN);
  request[sizeof(request) - 1] = '\0';

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Mkdir failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected mkdir response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runDelete(const char* pofoPath, uint8_t& errCode) {
  log_.infof("Deleting file on Portfolio: %s", pofoPath);

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x89;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, pofoPath, MAX_FILENAME_LEN);
  request[sizeof(request) - 1] = '\0';

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Delete failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected delete response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runRmdir(const char* pofoPath, uint8_t& errCode) {
  log_.infof("Removing directory on Portfolio: %s", pofoPath);

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x8A;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, pofoPath, MAX_FILENAME_LEN);
  request[sizeof(request) - 1] = '\0';

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Rmdir failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected rmdir response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runRename(const char* oldPofoPath, const char* newPofoPath, uint8_t& errCode) {
  log_.infof("Renaming on Portfolio: %s -> %s", oldPofoPath, newPofoPath);

  // Two ASCIIZ paths back-to-back after the 3-byte header, unlike the
  // single-path commands (mkdir/delete/rmdir). The wire request must still
  // fit in RAW_BUFSIZE (90) total - that's not just a convention, it's the
  // ROM's own fixed receive-block buffer size (see ROM_RESEARCH_NOTES.md's
  // File Transfer Server dispatch disassembly, `push 0x5A`/90 decimal) -
  // sending more than that overruns the ROM's stack-frame buffer, observed
  // as a hard crash/disconnect on real hardware. The buffer below is sized
  // generously (matching rename.inc's rename_old_path/rename_new_path,
  // each resb 80 on the Atari side) purely as local scratch space for
  // building the two ASCIIZ strings - only the bytes actually used
  // (3 + oldLen + 1 + newLen + 1) are ever sent, never sizeof(request).
  constexpr size_t kRenameBufSize = 3 + 2 * (MAX_FILENAME_LEN + 1);
  unsigned char request[kRenameBufSize] = {0};
  request[0] = 0x8B;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, oldPofoPath, MAX_FILENAME_LEN);
  request[3 + MAX_FILENAME_LEN] = '\0';
  size_t oldLen = strlen(reinterpret_cast<char*>(request) + 3);
  strncpy(reinterpret_cast<char*>(request) + 3 + oldLen + 1, newPofoPath, MAX_FILENAME_LEN);
  request[kRenameBufSize - 1] = '\0';
  size_t newLen = strlen(reinterpret_cast<char*>(request) + 3 + oldLen + 1);

  size_t wireLen = 3 + oldLen + 1 + newLen + 1;
  if (wireLen > RAW_BUFSIZE) {
    log_.errorf("Rename paths too long for one request: %u bytes (max %u)", static_cast<unsigned>(wireLen),
                static_cast<unsigned>(RAW_BUFSIZE));
    return PortfolioResult::Unknown;
  }

  if (!sendBlock(request, wireLen, VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Rename failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected rename response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runCopy(const char* srcPofoPath, const char* dstPofoPath, uint8_t& errCode) {
  log_.infof("Copying on Portfolio: %s -> %s", srcPofoPath, dstPofoPath);

  // Same two-ASCIIZ-path wire shape as runRename (see its comment for the
  // RAW_BUFSIZE=90 rationale) - COPY (0x8C) reuses the identical request
  // layout, just a different command byte and a server-side implementation
  // that does a real read/write data copy instead of a directory-entry
  // rewrite (see POFOSCAB/copy.inc).
  constexpr size_t kCopyBufSize = 3 + 2 * (MAX_FILENAME_LEN + 1);
  unsigned char request[kCopyBufSize] = {0};
  request[0] = 0x8C;
  request[2] = 0x70;
  strncpy(reinterpret_cast<char*>(request) + 3, srcPofoPath, MAX_FILENAME_LEN);
  request[3 + MAX_FILENAME_LEN] = '\0';
  size_t srcLen = strlen(reinterpret_cast<char*>(request) + 3);
  strncpy(reinterpret_cast<char*>(request) + 3 + srcLen + 1, dstPofoPath, MAX_FILENAME_LEN);
  request[kCopyBufSize - 1] = '\0';
  size_t dstLen = strlen(reinterpret_cast<char*>(request) + 3 + srcLen + 1);

  size_t wireLen = 3 + srcLen + 1 + dstLen + 1;
  if (wireLen > RAW_BUFSIZE) {
    log_.errorf("Copy paths too long for one request: %u bytes (max %u)", static_cast<unsigned>(wireLen),
                static_cast<unsigned>(RAW_BUFSIZE));
    return PortfolioResult::Unknown;
  }

  if (!sendBlock(request, wireLen, VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Copy failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected copy response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runGetDatetime(uint16_t& dosDate, uint16_t& dosTime) {
  log_.info("Reading Portfolio date and time");

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x8D;

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 4) {
    return PortfolioResult::Unknown;
  }

  dosDate = static_cast<uint16_t>(controlData_[0]) | (static_cast<uint16_t>(controlData_[1]) << 8);
  dosTime = static_cast<uint16_t>(controlData_[2]) | (static_cast<uint16_t>(controlData_[3]) << 8);
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runSetDatetime(uint16_t dosDate, uint16_t dosTime, uint8_t& errCode) {
  log_.info("Setting Portfolio date and time");

  unsigned char request[RAW_BUFSIZE] = {0};
  request[0] = 0x8E;
  request[2] = 0x70;
  request[3] = dosDate & 0xFF;
  request[4] = dosDate >> 8;
  request[5] = dosTime & 0xFF;
  request[6] = dosTime >> 8;

  if (!sendBlock(request, sizeof(request), VERB_ERRORS)) {
    return PortfolioResult::Unknown;
  }

  if (receiveBlock(controlData_, CONTROL_BUFSIZE, VERB_ERRORS) < 2) {
    return PortfolioResult::Unknown;
  }

  if (controlData_[0] == 0x10) {
    errCode = controlData_[1];
    log_.errorf("Set datetime failed, errcode=%u", controlData_[1]);
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected set datetime response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  errCode = 0;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runUpload(fs::FS& fs, const char* filename, const char* dest, bool overwrite) {
  log_.info("transmitFile begin");

  File file = fs.open(filename);
  if (!file) {
    log_.errorf("File not found: %s", filename);
    return PortfolioResult::Unknown;
  }

  size_t len = file.size();
  log_.infof("transmitFile: File length: %d", len);

  if (len > 32 * 1024 * 1024) {
    log_.warnf("Skipping %s.", file.name());
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
    log_.error("Invalid destination file (bad path or disk full)");
    file.close();
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] == 0x20) {
    if (overwrite) {
      log_.info("File exists on Portfolio and is being overwritten.");
      if (!sendBlock(TRANSMIT_OVERWRITE, sizeof(TRANSMIT_OVERWRITE), VERB_ERRORS)) {
        file.close();
        return PortfolioResult::Unknown;
      }
    } else {
      log_.warn("File exists on Portfolio! Overwrite disabled.");
      sendBlock(TRANSMIT_CANCEL, sizeof(TRANSMIT_CANCEL), VERB_ERRORS);
      file.close();
      return PortfolioResult::Unknown;
    }
  }

  int blocksize = controlData_[1] + (controlData_[2] << 8);
  if (blocksize > static_cast<int>(PAYLOAD_BUFSIZE)) {
    log_.error("Payload buffer too small");
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

  log_.infof("Upload finish response: %02X %02X %02X", controlData_[0], controlData_[1], controlData_[2]);

  if (controlData_[0] == 0x10) {
    log_.error("Invalid destination path (bad path or disk full).");
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.error("Transmission failed.");
    return PortfolioResult::Unknown;
  }

  transferDone_ = transferTotal_;
  return PortfolioResult::Ok;
}

PortfolioResult PortfolioLink::runDownload(fs::FS& fs, const char* pofoPath, const char* localPath, bool overwrite) {
  log_.info("receiveFile begin");

  if (!overwrite && fs.exists(localPath)) {
    log_.errorf("Local file already exists: %s", localPath);
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
    log_.error("File not found on Portfolio");
    return PortfolioResult::InvalidPath;
  }

  if (controlData_[0] != 0x20) {
    log_.errorf("Unexpected receive response: %02X", controlData_[0]);
    return PortfolioResult::Unknown;
  }

  size_t total = controlData_[7] | (static_cast<size_t>(controlData_[8]) << 8) |
                 (static_cast<size_t>(controlData_[9]) << 16) | (static_cast<size_t>(controlData_[10]) << 24);

  log_.infof("receiveFile: File length: %u", total);

  File file = fs.open(localPath, FILE_WRITE);
  if (!file) {
    log_.errorf("Cannot create local file: %s", localPath);
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
