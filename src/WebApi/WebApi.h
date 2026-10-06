#pragma once

// Starts the HTTP server and registers the REST API used by data/web:
//   GET  /status                              -> {status, connected, phase, done, total, error, otaEnabled, pftd}
//                                                 pftd is {buildId, version:{major,minor,patch}} or null -
//                                                 whether PFTD (the Atari-side companion TSR) answered HELLO
//                                                 after the link's last offline->online transition.
//   GET  /listESP32?dir=PATH                  -> {items:[{name,type}]}, rooted at DATA_DIR
//   GET  /listAtari?dir=PATH                  -> {files:[...]}
//   GET  /listAtariExt?dir=PATH               -> {items:[{name,type,size,modified}],freeBytes,totalBytes},
//                                                 PFTD-only (404 if not present)
//   GET  /drives                              -> {drives:["A","B",...]}, PFTD-only (404 if not present)
//   POST /mkdirAtari?path=PATH                -> {ok,message[,errcode]}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio rejected the request)
//   POST /rmdirAtari?path=PATH                -> {ok,message[,errcode]}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio rejected the request)
//   POST /deleteAtari?path=PATH               -> {ok,message[,errcode]}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio rejected the request)
//   POST /renameAtari?oldPath=PATH&newPath=PATH -> {ok,message[,errcode]}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio rejected the request). No frontend
//                                                 wiring yet - index.htm's rename button is disabled.
//   POST /copyAtari?srcPath=PATH&dstPath=PATH -> {ok,message[,errcode]}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio rejected the request). Blocks for
//                                                 the whole copy duration (no frontend wiring yet).
//   POST /upload?overwrite&toAtari&destDir=   -> 202, stores under DATA_DIR, optionally also sends to Portfolio
//   POST /sendToAtari?path&overwrite&destDir= -> 202 once started, path is DATA_DIR-relative
//   POST /downloadFromAtari?path&overwrite    -> 202 once started, path is the full Portfolio path
//   POST /ota/enable, /ota/disable            -> starts/stops the ArduinoOTA (espota) listener
//
// Also serves the ElegantOTA firmware update portal at GET/POST /update.
//
// Each route group is registered from its own .cpp (Status.cpp,
// Esp32Files.cpp, AtariList.cpp, Transfer.cpp, Ota.cpp, Mkdir.cpp,
// Rmdir.cpp, Delete.cpp, Rename.cpp, Copy.cpp) via a register*() function
// declared in WebApiInternal.h, called from webApiBegin() here.
void webApiBegin();

// Must be called from loop().
void webApiLoop();
