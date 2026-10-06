#include "WebApi.h"
#include "WebApiInternal.h"

#include <LittleFS.h>
#include <ElegantOTA.h>

WebServer server(80);
File fsUploadFile;

String hex32(uint32_t value) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lx", static_cast<unsigned long>(value));
  return String(buf);
}

String pofoBasename(const String& pofoPath) {
  int pos = pofoPath.lastIndexOf('\\');
  if (pos < 0) {
    pos = pofoPath.lastIndexOf(':');
  }
  return pos >= 0 ? pofoPath.substring(pos + 1) : pofoPath;
}

void webApiBegin() {
  // Must be registered before the "/" static handler below: that handler's
  // canHandle() matches every path starting with "/" (i.e. everything), so
  // anything registered after it, including ElegantOTA's own /update route,
  // would never be reached.
  ElegantOTA.begin(&server);

  registerStatusRoutes();
  registerEsp32FileRoutes();
  registerAtariListRoutes();
  registerTransferRoutes();
  registerOtaRoutes();
  registerMkdirRoutes();
  registerRmdirRoutes();
  registerDeleteRoutes();
  registerRenameRoutes();
  registerCopyRoutes();

  server.serveStatic("/files/", LittleFS, DATA_DIR);
  server.serveStatic("/", LittleFS, "/web/");

  server.begin();
}

void webApiLoop() {
  server.handleClient();
  ElegantOTA.loop();
}
