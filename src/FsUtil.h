#pragma once

#include <Arduino.h>

// Appends a JSON {"items":[{"name":..,"type":"file"|"folder"}, ...]} listing
// of one LittleFS directory (non-recursive) to out. Returns false if dir
// could not be opened as a directory.
bool listEspDirJson(const String& dir, String& out);
