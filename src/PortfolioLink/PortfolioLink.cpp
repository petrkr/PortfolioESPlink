#include "PortfolioLink.h"

PortfolioLink::PortfolioLink(PofoSmartCable& cable)
    : cable_(cable), fileTransfer_(cable) {}

void PortfolioLink::setProgressCallback(PofoFileTransferProgress progress) {
  fileTransfer_.setProgressCallback(progress);
}

PofoResult PortfolioLink::list(const char* path,
                               PofoFileTransferList* response) {
  return fileTransfer_.list(path, response);
}

PofoResult PortfolioLink::receiveFile(const char* path, Stream& output) {
  return fileTransfer_.receiveFile(path, output);
}

PofoResult PortfolioLink::transmitFile(const char* path, Stream& input,
                                       size_t length, bool overwrite,
                                       time_t timestamp) {
  return fileTransfer_.transmitFile(path, input, length, overwrite,
                                    timestamp);
}
