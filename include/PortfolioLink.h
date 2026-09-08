#ifndef PORTFOLIO_LINK_H_
#define PORTFOLIO_LINK_H_

#include <Arduino.h>
#include <FS.h>

struct PortfolioPins {
  uint8_t outData;
  uint8_t outClock;
  uint8_t inClock;
  uint8_t inData;
};

class PortfolioLink {
public:
  enum Verbosity {
    VERB_QUIET = 0,
    VERB_ERRORS,
    VERB_COUNTER,
    VERB_FLOWCONTROL
  };

  static constexpr size_t PAYLOAD_BUFSIZE = 60000;
  static constexpr size_t CONTROL_BUFSIZE = 100;
  static constexpr size_t LIST_BUFSIZE = 2000;
  static constexpr size_t MAX_FILENAME_LEN = 79;

  explicit PortfolioLink(Print& log = Serial);
  ~PortfolioLink();

  bool begin(const PortfolioPins& pins);
  bool detect();
  bool listFilesJson(const char* pattern, String& output);
  bool transmitFile(fs::FS& fs, const String& filename, const char* dest);

  void setForce(bool enabled);

private:
  void setupPort();
  void writePort(unsigned char data);
  void waitClockHigh();
  void waitClockLow();
  unsigned char getBit();
  unsigned char receiveByte();
  void sendByte(unsigned char data);
  bool sendBlock(const unsigned char* data, unsigned int len, Verbosity verbosity);
  int receiveBlock(unsigned char* data, int maxLen, Verbosity verbosity);

  Print& log_;
  PortfolioPins pins_{};
  bool force_ = false;
  unsigned char* payload_ = nullptr;
  unsigned char* controlData_ = nullptr;
  unsigned char* list_ = nullptr;
  unsigned char transmitInit_[90] = {
    0x03, 0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,
    0, 0, 0, 0
  };
  unsigned char receiveInit_[82] = {
    0x06, 0x00, 0x70
  };
};

#endif
