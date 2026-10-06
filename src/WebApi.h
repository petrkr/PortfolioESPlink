#pragma once

// Starts the HTTP server and registers the REST API used by data/web:
//   GET  /status                -> {status, connected, phase, done, total}
//   GET  /listESP32?dir=PATH    -> {items:[{name,type}]}
//   GET  /listAtari?dir=PATH    -> {files:[...]}
//   POST /upload?overwrite=1    -> 202 once queued for transfer to the Portfolio
void webApiBegin();

// Must be called from loop().
void webApiLoop();
