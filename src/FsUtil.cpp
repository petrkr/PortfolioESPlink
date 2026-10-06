#include "FsUtil.h"

#include <LittleFS.h>

bool listEspDirJson(const String& dir, String& out) {
  File root = LittleFS.open(dir);
  if (!root || !root.isDirectory()) {
    return false;
  }

  out = "{\"items\":[";
  bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!first) {
      out += ',';
    }
    first = false;

    String name = file.name();
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) {
      name = name.substring(lastSlash + 1);
    }

    out += "{\"name\":\"";
    out += name;
    out += "\",\"type\":\"";
    out += file.isDirectory() ? "folder" : "file";
    out += "\"}";

    file = root.openNextFile();
  }
  out += "]}";

  return true;
}
