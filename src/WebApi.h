#pragma once

// Starts the HTTP server and registers the REST API used by data/web:
//   GET  /status                              -> {status, connected, phase, done, total, error}
//   GET  /listESP32?dir=PATH                  -> {items:[{name,type}]}, rooted at DATA_DIR
//   GET  /listAtari?dir=PATH                  -> {files:[...]}
//   POST /upload?overwrite&toAtari&destDir=   -> 202, stores under DATA_DIR, optionally also sends to Portfolio
//   POST /sendToAtari?path&overwrite&destDir= -> 202 once started, path is DATA_DIR-relative
//   POST /downloadFromAtari?path&overwrite    -> 202 once started, path is the full Portfolio path
//
// Also serves the ElegantOTA firmware update portal at GET/POST /update.
void webApiBegin();

// Must be called from loop().
void webApiLoop();
