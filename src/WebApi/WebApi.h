#pragma once

// Starts the HTTP server and registers the REST API used by data/web:
//   GET  /status                              -> {fwBuildId, status, connected, phase, done, total, error,
//                                                 otaEnabled, espUsed, espTotal, pftd}
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
//   GET  /datetimeAtari                       -> {ok,datetime:"YYYY-MM-DDTHH:MM:SS"}, PFTD-only
//                                                 (404 if not present)
//   POST /datetimeAtari?datetime=ISO8601      -> {ok,datetime[,message,errcode]}, PFTD-only (404 if not
//                                                 present, 409 if the Portfolio rejected the request).
//                                                 No timezone offset (DOS packed date/time has none).
//   POST /sendRaw?data=HEX                    -> {ok,response:HEX}, raw protocol-level debug/probing
//                                                 escape hatch; only requires the link to be online, not
//                                                 PFTD presence - the caller builds the request bytes
//   POST /drawAsciiAtari?row=0-7&col=0-39&text=TEXT -> {ok,message}, PFTD-only (404 if not present,
//                                                 409 if the Portfolio reported failure). Diagnostic/
//                                                 debug tool - writes text to the Portfolio's own LCD.
//   POST /uploadFile?overwrite&toAtari&destDir= -> {ok[,message]}, stores under DATA_DIR; if toAtari is set,
//                                                 blocks until the transfer to the Portfolio finishes
//   POST /sendToAtari?path&overwrite&destDir= -> {ok[,message]}, path is DATA_DIR-relative; blocks until
//                                                 the transfer finishes
//   POST /downloadFile?path=PATH              -> raw file bytes (application/octet-stream,
//                                                 Content-Disposition: attachment) on success, or
//                                                 {ok:false,message} with a 4xx/5xx status on failure;
//                                                 path is the full Portfolio path; blocks until the
//                                                 transfer finishes. Saves under DATA_DIR along the way
//                                                 and leaves it there (visible in /listESP32) regardless
//                                                 of whether the browser's save dialog is accepted.
//   POST /ota/enable, /ota/disable            -> starts/stops the ArduinoOTA (espota) listener
//
// Also serves the ElegantOTA firmware update portal at GET/POST /update.
//
// Each route group is registered from its own .cpp (Status.cpp,
// Esp32Files.cpp, AtariList.cpp, Transfer.cpp, Ota.cpp, Mkdir.cpp,
// Rmdir.cpp, Delete.cpp, Rename.cpp, Copy.cpp, DateTime.cpp, SendRaw.cpp,
// DrawAscii.cpp) via a register*() function declared in WebApiInternal.h,
// called from webApiBegin() here.
void webApiBegin();

// Must be called from loop().
void webApiLoop();
