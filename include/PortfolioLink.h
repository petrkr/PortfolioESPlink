#ifndef PORTFOLIO_LINK_H_
#define PORTFOLIO_LINK_H_

#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

struct PortfolioPins {
  uint8_t outData;
  uint8_t outClock;
  uint8_t inClock;
  uint8_t inData;
};

enum class PortfolioStatus {
  Disconnected,
  Connected,
  Busy
};

enum class PortfolioResult {
  None,
  Ok,
  Unknown,
  InvalidPath
};

enum class PortfolioTransferPhase {
  Idle,
  PofoUpload
};

class PortfolioLink {
public:
  static constexpr size_t PAYLOAD_BUFSIZE = 60000;
  static constexpr size_t CONTROL_BUFSIZE = 100;
  static constexpr size_t MAX_FILENAME_LEN = 79;

  explicit PortfolioLink(Print& log = Serial);
  ~PortfolioLink();

  bool begin(const PortfolioPins& pins);

  PortfolioStatus status() const;
  PortfolioResult lastResult() const;
  PortfolioTransferPhase transferPhase() const;
  size_t transferDone() const;
  size_t transferTotal() const;
  bool isConnected() const;
  bool isBusy() const;

  bool startUpload(fs::FS& fs, const char* localPath, const char* pofoPath, bool overwrite);
  bool startDownload(fs::FS& fs, const char* pofoPath, const char* localPath, bool overwrite);
  PortfolioResult listFiles(const char* pattern, String& output);

private:
  enum Verbosity {
    VERB_QUIET = 0,
    VERB_ERRORS,
    VERB_COUNTER,
    VERB_FLOWCONTROL
  };

  enum class JobType {
    List,
    Upload,
    Download
  };

  struct Job {
    JobType type;
    fs::FS* fs;
    char localPath[MAX_FILENAME_LEN + 1];
    char pofoPath[MAX_FILENAME_LEN + 1];
    bool overwrite;
    String* listOutput;
    PortfolioResult* resultOut;
    SemaphoreHandle_t done;
  };

  static void taskThunk(void* arg);
  void taskLoop();
  void finishJob(PortfolioResult result);

  void setupPort();
  void writePort(unsigned char data);
  bool waitClockHigh(uint32_t timeoutUs);
  bool waitClockLow(uint32_t timeoutUs);
  unsigned char getBit();
  bool receiveByte(unsigned char& out, uint32_t timeoutUs);
  bool sendByte(unsigned char data);
  bool sendBlock(const unsigned char* data, unsigned int len, Verbosity verbosity);
  int receiveBlock(unsigned char* data, int maxLen, Verbosity verbosity);
  bool detectOnce();

  PortfolioResult runList(const char* pattern, String& output);
  PortfolioResult runUpload(fs::FS& fs, const char* filename, const char* dest, bool overwrite);
  PortfolioResult runDownload(fs::FS& fs, const char* pofoPath, const char* localPath, bool overwrite);

  Print& log_;
  PortfolioPins pins_{};
  QueueHandle_t jobQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile PortfolioStatus status_ = PortfolioStatus::Disconnected;
  volatile PortfolioResult lastResult_ = PortfolioResult::None;
  volatile PortfolioTransferPhase transferPhase_ = PortfolioTransferPhase::Idle;
  volatile size_t transferDone_ = 0;
  volatile size_t transferTotal_ = 0;
  unsigned char* payload_ = nullptr;
  unsigned char* controlData_ = nullptr;
  unsigned char transmitInit_[90] = {
    0x03, 0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,
    0, 0, 0, 0
  };
  unsigned char receiveInit_[82] = {
    0x06, 0x00, 0x70
  };
  unsigned char receiveFileInit_[82] = {
    0x02, 0x00, 0x70
  };
};

#endif
