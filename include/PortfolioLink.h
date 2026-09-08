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
  Ok,
  Unknown
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
  bool isConnected() const;
  bool isBusy() const;

  PortfolioResult listFiles(const char* pattern, String& output);
  PortfolioResult uploadFile(fs::FS& fs, const char* localPath, const char* pofoPath, bool overwrite);

private:
  enum Verbosity {
    VERB_QUIET = 0,
    VERB_ERRORS,
    VERB_COUNTER,
    VERB_FLOWCONTROL
  };

  enum class JobType {
    List,
    Upload
  };

  struct Job {
    JobType type;
    fs::FS* fs;
    const char* localPath;
    const char* pofoPath;
    bool overwrite;
    String* listOutput;
    PortfolioResult result;
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
  bool receiveByte(unsigned char& out);
  bool sendByte(unsigned char data);
  bool sendBlock(const unsigned char* data, unsigned int len, Verbosity verbosity);
  int receiveBlock(unsigned char* data, int maxLen, Verbosity verbosity);
  bool detectOnce();

  PortfolioResult runList(const char* pattern, String& output);
  PortfolioResult runUpload(fs::FS& fs, const char* filename, const char* dest, bool overwrite);

  Print& log_;
  PortfolioPins pins_{};
  QueueHandle_t jobQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile PortfolioStatus status_ = PortfolioStatus::Disconnected;
  volatile PortfolioResult lastResult_ = PortfolioResult::Unknown;
  unsigned char* payload_ = nullptr;
  unsigned char* controlData_ = nullptr;
  unsigned char transmitInit_[90] = {
    0x03, 0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,
    0, 0, 0, 0
  };
  unsigned char receiveInit_[82] = {
    0x06, 0x00, 0x70
  };
};

#endif
