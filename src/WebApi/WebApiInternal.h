#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

// Shared state and helpers between the WebApi handler .cpp files (not part
// of WebApi.h's public API). server/fsUploadFile are defined once in
// WebApi.cpp; each handler file registers its routes via a register*()
// function called from webApiBegin().

#define DATA_DIR "/data/"

extern WebServer server;
extern File fsUploadFile;

String hex32(uint32_t value);

// Derives the Portfolio basename from a DOS path (last segment after '\\' or ':').
String pofoBasename(const String& pofoPath);

void registerStatusRoutes();
void registerEsp32FileRoutes();
void registerAtariListRoutes();
void registerTransferRoutes();
void registerOtaRoutes();
void registerMkdirRoutes();
void registerRmdirRoutes();
void registerDeleteRoutes();
void registerRenameRoutes();
void registerCopyRoutes();
void registerDateTimeRoutes();
