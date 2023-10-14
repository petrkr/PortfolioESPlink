/*
  PortfolioESPlink is Transfolio based file transfer utility that connects to the Atari Portfolio
  pocket computer over wifi. It communicates with the built-in file transfer software of the Portfolio.

  Version history:
  0.1 First release, only list folder and cat(type) files to Serial, protocol based on Transfolio v1.0.1 (2019-05-26)
  0.2 Format SPIFF if it does not work, fix setupPort

*/

/*
  USAGE:
  List directory:
  GET http://IP/list?dir=A:\*.*
    param dir=PATH/FILTER

  POST http://IP/upload
    param file

  curl -X POST -F "file=@fileondrive" http://IP/upload

*/

#define PAYLOAD_BUFSIZE   60000
#define CONTROL_BUFSIZE     100
#define LIST_BUFSIZE       2000
#define MAX_FILENAME_LEN     79

// Output pins
#define outDataPIN   5 // LPT Data 0 (pin 2)
#define outClockPIN 18 // LPT Data 1 (pin 3)

// Input pins
#define inClockPIN  21 // LPT Paper Error (pin 12, byte S5)
#define inDataPIN   19 // LPT Select      (pin 13, byte S4)

#define DBG_OUTPUT_PORT Serial

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);
#endif

#include <SPIFFS.h>
File fsUploadFile;

#define FILESYSTEM SPIFFS

const char* ssid = "";
const char* password = "";
const char* host = "portfolioesplink";

typedef enum {
  VERB_QUIET = 0,
  VERB_ERRORS,
  VERB_COUNTER,
  VERB_FLOWCONTROL
} VERBOSITY;


int force = 0;
int sourcecount = 0;

unsigned char *payload;
unsigned char *controlData;
unsigned char *list;

unsigned char transmitInit[90] =
{ /* Offset 0: Funktion */
  0x03, 0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,
  /* Offset 7: Dateilaenge */
  0, 0, 0, 0
  /* Offset 11: Pfad */
};

const unsigned char transmitOverwrite[3] = { 0x05, 0x00, 0x70 };
const unsigned char transmitCancel[3] = { 0x00, 0x00, 0x00 };

unsigned char receiveInit[82] =
{ 0x06,         /* Offset 0: Funktion */
  0x00, 0x70    /* Offset 2: Puffergroesse = 28672 Byte */
  /* Offset 3: Pfad */
};

const unsigned char receiveFinish[3] = { 0x20, 0x00, 0x03 };

// WEB server functions

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".exe")) {
    return "application/x-msdownload";
  } else if (filename.endsWith(".txt")) {
    return "text/plain";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "application/octet-stream";
}


void handleFileUpload() {
  int val, len, blocksize;

  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = FILESYSTEM.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      String filename = fsUploadFile.name();
      fsUploadFile.close();

      // Transmit file to Atari
      fsUploadFile = FILESYSTEM.open(filename, "r");

      if (!transmitFile(&fsUploadFile, String("C:\\" + upload.filename).c_str())) {
        DBG_OUTPUT_PORT.println("Upload file to Atari failed");
      }
    }
    DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }

}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  int i, num;
  char *name;

  String path = server.arg("dir");
  DBG_OUTPUT_PORT.println("handleFileList: " + path);
  DBG_OUTPUT_PORT.printf("Fetching directory listing for %s\n", path.c_str());

  strncpy((char*)receiveInit + 3, path.c_str(), MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_ERRORS);

  num = payload[0] + (payload[1] << 8);

  if (num == 0) {
    DBG_OUTPUT_PORT.printf("No files.\n");
    server.send(200, "text/plain", "No files");
    return;
  }

  name = (char*)payload + 2;

  String output = "[";

  for (i = 0; i < num; i++) {
    DBG_OUTPUT_PORT.printf("%s\n", name);
    if (output != "[") {
      output += ',';
    }
    output += "{\"name\":\"";
    output += name;
    output += "\"}";

    name += strlen(name) + 1;
  }

  output += "]";
  server.send(200, "text/json", output);
}

/*
  Prepare ESP Pins
*/
void setupPort() {
  pinMode(outDataPIN, OUTPUT);
  pinMode(outClockPIN, OUTPUT);

  pinMode(inClockPIN, INPUT);
  pinMode(inDataPIN, INPUT);
}


/*
  Output a byte to the data register of the parallel port
*/
static inline void writePort(const unsigned char data) {
  digitalWrite(outDataPIN, data & 1);
  digitalWrite(outClockPIN, data & 2);
}

static inline void waitClockHigh(void)
{
  while (!digitalRead(inClockPIN)) {
    //delay(1); // TODO: Do not block read?
  }
}

static inline void waitClockLow(void)
{
  while (digitalRead(inClockPIN)) {
    //delay(1); // TODO: Do not block read?
  }
}


static inline unsigned char getBit(void)
{
  return digitalRead(inDataPIN);
}


/*
  Receives one byte serially, MSB first
  One bit is read on every falling and every rising slope of the clock signal.
*/
unsigned char receiveByte(void)
{
  int i;
  unsigned char recv;

  for (i = 0; i < 4; i++) {
    waitClockLow();
    recv = (recv << 1) | getBit();
    writePort(0);                   /* Clear clock */
    waitClockHigh();
    recv = (recv << 1) | getBit();
    writePort(2);                   /* Set clock */
  }

  return recv;
}


/*
  Transmits one byte serially, MSB first
  One bit is transmitted on every falling and every rising slope of the clock signal.
*/
void sendByte(unsigned char data)
{
  int i;
  unsigned char b;

  usleep(250);
  for (i = 0; i < 4; i++) {
    b = ((data & 0x80) >> 7) | 2;     /* Output data bit */
    writePort(b);
    b = (data & 0x80) >> 7;           /* Set clock low   */
    writePort(b);

    data = data << 1;
    waitClockLow();

    b = (data & 0x80) >> 7;           /* Output data bit */
    writePort(b);
    b = ((data & 0x80) >> 7) | 2;     /* Set clock high  */
    writePort(b);

    data = data << 1;
    waitClockHigh();
  }
}


/*
  This function transmits a block of data.
  Call int 61h with AX=3002 (open) and AX=3001 (receive) on the Portfolio
*/
void sendBlock(const unsigned char *pData, const unsigned int len, const VERBOSITY verbosity)
{
  unsigned char recv;
  unsigned int  i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;

  if (len) {
    recv = receiveByte();

    if (recv == 'Z') {
      if (verbosity >= VERB_FLOWCONTROL) {
        DBG_OUTPUT_PORT.printf("Portfolio ready for receiving.\n");
      }
    }
    else {
      if (verbosity >= VERB_ERRORS) {
        DBG_OUTPUT_PORT.printf( "Portfolio not ready!\n");
        exit(EXIT_FAILURE);
      }
    }

    usleep(50000);
    sendByte(0x0a5);

    lenH = len >> 8;
    lenL = len & 255;
    sendByte(lenL); checksum -= lenL;
    sendByte(lenH); checksum -= lenH;

    for (i = 0; i < len; i++) {
      recv = pData[i];
      sendByte(recv); checksum -= recv;

      if (verbosity >= VERB_COUNTER)
        DBG_OUTPUT_PORT.printf("Sent %d of %d bytes.\r", i + 1, len);
    }
    sendByte(checksum);

    if (verbosity >= VERB_COUNTER)
      DBG_OUTPUT_PORT.printf("\n");

    recv = receiveByte();

    if (recv == checksum) {
      if (verbosity >= VERB_FLOWCONTROL) {
        DBG_OUTPUT_PORT.printf( "checksum OK\n");
      }
    }
    else {
      if (verbosity >= VERB_ERRORS) {
        DBG_OUTPUT_PORT.printf( "checksum ERR: %d\n", recv);
        exit(EXIT_FAILURE);
      }
    }
  }
}


/*
   This function receives a block of data and returns its length in bytes.
   Call int 61h with AX=3002 (open) and AX=3000 (transmit) on the Portfolio.
*/
int receiveBlock(unsigned char *pData, const int maxLen, const VERBOSITY verbosity)
{
  unsigned int len, i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;
  unsigned char recv;

  sendByte('Z');

  recv = receiveByte();

  if (recv == 0x0a5) {
    if (verbosity >= VERB_FLOWCONTROL) {
      DBG_OUTPUT_PORT.printf( "Acknowledge OK\n");
    }
  }
  else {
    if (verbosity >= VERB_ERRORS) {
      DBG_OUTPUT_PORT.printf( "Acknowledge ERROR (received %2X instead of A5)\n", recv);
      exit(EXIT_FAILURE);
    }
  }

  lenL = receiveByte();  checksum += lenL;
  lenH = receiveByte();  checksum += lenH;
  len = (lenH << 8) | lenL;

  if (len > maxLen) {
    if (verbosity >= VERB_ERRORS) {
      DBG_OUTPUT_PORT.printf( "Receive buffer too small (%d instead of %d bytes).\n", maxLen, len);
    }
    return 0;
  }

  for (i = 0; i < len; i++) {
    unsigned char recv = receiveByte();
    checksum += recv;
    pData[i] = recv;

    if (verbosity >= VERB_COUNTER)
      DBG_OUTPUT_PORT.printf(".");
  }

  if (verbosity >= VERB_COUNTER)
    DBG_OUTPUT_PORT.printf("\n");

  recv = receiveByte();

  if ((unsigned char)(256 - recv) == checksum) {
    if (verbosity >= VERB_FLOWCONTROL) {
      DBG_OUTPUT_PORT.printf( "checksum OK\n");
    }
  }
  else {
    if (verbosity >= VERB_ERRORS) {
      DBG_OUTPUT_PORT.printf( "checksum ERR %d %d\n", (unsigned char)(256 - recv), checksum);
      exit(EXIT_FAILURE);
    }
  }

  usleep(100);
  sendByte((unsigned char)(256 - checksum));

  return len;
}


/*
  Read source file and transmit it to the Portfolio (/t)
  TODO: ESP Link handle
*/
bool transmitFile(File *file, const char * dest) {
  int val, len, blocksize;

  if (file == NULL) {
    DBG_OUTPUT_PORT.printf("File not found: %s\n", file->name());
    return false;
  }

  /*
    Dateigroesse ermitteln
  */
  len = file->size();
  DBG_OUTPUT_PORT.printf("File length: %d\n", len);
  if (len == -1 || len > 32 * 1024 * 1024) {
    /* Directories and huge files (>32 MB) are skipped */
    DBG_OUTPUT_PORT.printf("Skipping %s.\n", file->name());
    return false;
  }
  file->seek(0, SeekSet);

  transmitInit[7] = len & 255;
  transmitInit[8] = (len >> 8) & 255;
  transmitInit[9] = (len >> 16) & 255;

  strncpy((char*)transmitInit + 11, dest, MAX_FILENAME_LEN);

  sendBlock(transmitInit, sizeof(transmitInit), VERB_ERRORS);
  receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

  if (controlData[0] == 0x10) {
    DBG_OUTPUT_PORT.printf("Invalid destination file!\n");
    return false;
  }

  if (controlData[0] == 0x20) {
    DBG_OUTPUT_PORT.printf("File exists on Portfolio");
    if (force) {
      DBG_OUTPUT_PORT.printf(" and is being overwritten.\n");
      sendBlock(transmitOverwrite, sizeof(transmitOverwrite), VERB_ERRORS);
    }
    else {
      DBG_OUTPUT_PORT.printf("! Use -f to force overwriting.\n");
      sendBlock(transmitCancel, sizeof(transmitCancel), VERB_ERRORS);
      return false; /* proceed to next file */
    }
  }

  blocksize = controlData[1] + (controlData[2] << 8);
  if (blocksize > PAYLOAD_BUFSIZE) {
    DBG_OUTPUT_PORT.printf("Payload buffer too small!\n");
    return false;
  }

  if (len > blocksize) {
    DBG_OUTPUT_PORT.printf("Transmission consists of %d blocks of payload.\n", (len + blocksize - 1) / blocksize);
  }
  while (len > blocksize) {
    file->readBytes((char*)payload, sizeof(char) * blocksize);
    sendBlock(payload, blocksize, VERB_COUNTER);
    len -= blocksize;
  }

  file->readBytes((char*)payload, sizeof(char) * len);
  if (len)
    sendBlock(payload, len, VERB_COUNTER);
  receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

  if (controlData[0] != 0x20) {
    DBG_OUTPUT_PORT.printf("Transmission failed!\nPossilby disk full on Portfolio or directory does not exist.\n");
    return false;
  }

  return true;
}


/*
  Print content file to serial line
*/
void catFile(const char * source) {
  static int nReceivedFiles = 0;
  int i, num, len, total;
  int blocksize = 0x7000;   /* TODO: Check if this is always the same */
  char startdir[256];
  char *basename;
  char *pos;

  /* Get list of matching files */
  receiveInit[0] = 6;
  strncpy((char*)receiveInit + 3, source, MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock((unsigned char*)list, 2000, VERB_ERRORS);

  num = list[0] + (list[1] << 8);

  if (num == 0) {
    DBG_OUTPUT_PORT.printf("File not found on Portfolio: %s\n", source);
    exit(EXIT_FAILURE);
  }

  basename = (char*)list + 2;

  /* Transfer each file from the list */
  for (i = 1; i <= num; i++) {

    DBG_OUTPUT_PORT.printf("Transferring file %d", nReceivedFiles + i);
    if (sourcecount == 1) {
      /* We know the total number of files only if a single source item
        has been specified (potentially using wildcards). */
      DBG_OUTPUT_PORT.printf(" of %d", num);
    }
    DBG_OUTPUT_PORT.printf(": %s\n", basename);

    /* Request Portfolio to send file */
    receiveInit[0] = 2;
    sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);

    /* Get file length information */
    receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

    if (controlData[0] != 0x20) {
      DBG_OUTPUT_PORT.printf("Unknown protocol error! \n");
      exit(EXIT_FAILURE);
    }

    total = controlData[7] + ((int)controlData[8] << 8) + ((int)controlData[9] << 16);

    if (total > blocksize) {
      DBG_OUTPUT_PORT.printf("Transmission consists of %d blocks of payload.\n", (total + blocksize - 1) / blocksize);
    }

    /* Receive and save actual payload */
    while (total > 0) {
      len = receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_COUNTER);
      for (int i = 0; i < len; i++) {
        DBG_OUTPUT_PORT.write(payload[i]);
      }
      total -= len;
    }

    /* Close connection and destination file */
    sendBlock(receiveFinish, sizeof(receiveFinish), VERB_ERRORS);

    basename += strlen(basename) + 1;
  }

  nReceivedFiles += num;
}

/*
  Receive source file(s) from the Portfolio and save it on the PC (/r)
  TODO: ESP Link handle
*/
void receiveFile(const char * source, const char * dest) {
  static int nReceivedFiles = 0;
  FILE * file;
  int i, num, len, total;
  int destIsDir = 0;
  int blocksize = 0x7000;   /* TODO: Check if this is always the same */
  char startdir[256];
  char *namebase;
  char *basename;
  char *pos;

  /* Check if the destination parameter specifies a directory */
  if (!getcwd(startdir, sizeof(startdir))) {
    DBG_OUTPUT_PORT.printf("Unexpected error: getcwd(%s) failed!\n", dest);
    exit(EXIT_FAILURE);
  }
  if (chdir(dest) == 0) {
    destIsDir = 1;
  }

  /* Get list of matching files */
  receiveInit[0] = 6;
  strncpy((char*)receiveInit + 3, source, MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock((unsigned char*)list, 2000, VERB_ERRORS);

  num = list[0] + (list[1] << 8);

  if (num == 0) {
    DBG_OUTPUT_PORT.printf("File not found on Portfolio: %s\n", source);
    exit(EXIT_FAILURE);
  }

  /* Set up pointer to behind the path where basename shall be appended */
  namebase = (char*)receiveInit + 3;
  pos = strrchr(namebase, ':');
  if (pos) {
    namebase = pos + 1;
  }
  pos = strrchr(namebase, '\\');
  if (pos) {
    namebase = pos + 1;
  }

  basename = (char*)list + 2;

  /* Transfer each file from the list */
  for (i = 1; i <= num; i++) {

    DBG_OUTPUT_PORT.printf("Transferring file %d", nReceivedFiles + i);
    if (sourcecount == 1) {
      /* We know the total number of files only if a single source item
        has been specified (potentially using wildcards). */
      DBG_OUTPUT_PORT.printf(" of %d", num);
    }
    DBG_OUTPUT_PORT.printf(": %s\n", basename);

    if (destIsDir)
      dest = basename;

    /* Check if destination file exists */
    file = fopen(dest, "rb");
    if (file != NULL) {
      fclose(file);
      if (!force) {
        DBG_OUTPUT_PORT.printf("File exists! Use -f to force overwriting.\n");
        if (i < num)
          DBG_OUTPUT_PORT.printf("Remaining files are not copied!\n");
        exit(EXIT_FAILURE);
      }
    }

    /* Open destination file */
    file = fopen(dest, "wb");
    if (file == NULL) {
      DBG_OUTPUT_PORT.printf("Cannot create file: %s\n", dest);
      exit(EXIT_FAILURE);
    }

    /* Request Portfolio to send file */
    receiveInit[0] = 2;
    strncpy(namebase, basename, MAX_FILENAME_LEN);
    sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);

    /* Get file length information */
    receiveBlock(controlData, CONTROL_BUFSIZE, VERB_ERRORS);

    if (controlData[0] != 0x20) {
      DBG_OUTPUT_PORT.printf("Unknown protocol error! \n");
      exit(EXIT_FAILURE);
    }

    total = controlData[7] + ((int)controlData[8] << 8) + ((int)controlData[9] << 16);

    if (total > blocksize) {
      DBG_OUTPUT_PORT.printf("Transmission consists of %d blocks of payload.\n", (total + blocksize - 1) / blocksize);
    }

    /* Receive and save actual payload */
    while (total > 0) {
      len = receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_COUNTER);
      fwrite(payload, 1, len, file);
      total -= len;
    }

    /* Close connection and destination file */
    sendBlock(receiveFinish, sizeof(receiveFinish), VERB_ERRORS);
    fclose(file);

    basename += strlen(basename) + 1;
  }

  /* Change back to original directory */
  if (destIsDir) {
    if (chdir(startdir) != 0) {
      DBG_OUTPUT_PORT.printf("Unexpected error: chdir(%s) failed!\n", startdir);
      exit(EXIT_FAILURE);
    }
  }

  nReceivedFiles += num;
}


/*
  Get directory listing from the Portfolio and display it on Serial
*/
void listFiles(const char * pattern) {
  int i, num;
  char *name;

  DBG_OUTPUT_PORT.printf("Fetching directory listing for %s\n", pattern);

  strncpy((char*)receiveInit + 3, pattern, MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit), VERB_ERRORS);
  receiveBlock(payload, PAYLOAD_BUFSIZE, VERB_ERRORS);

  num = payload[0] + (payload[1] << 8);
  if (num == 0)
    DBG_OUTPUT_PORT.printf("No files.\n");

  name = (char*)payload + 2;

  for (i = 0; i < num; i++) {
    DBG_OUTPUT_PORT.printf("%s\n", name);
    name += strlen(name) + 1;
  }
}


/*
  Assemble full destination path and name if only the destination directory is given.
  The current source file name is appended to the destination directory and modified
  to fulfill the (most important) DOS file naming restrictions.
*/
void composePofoName(char *source, char * dest, char *pofoName, int sourcecount)
{
  char *pos;
  char *ext;
  char  lastChar;

  /* Exchange Slash by Backslash (Unix path -> DOS path) */
  while (pos = strchr(dest, '/')) {
    *pos = '\\';
  }

  strncpy(pofoName, dest, MAX_FILENAME_LEN);

  lastChar = pofoName[strlen(pofoName) - 1];

  if (sourcecount > 1 || lastChar == '\\' || lastChar == ':') {
    /* "dest" is a directory. */
    int len;

    /* Append Backslash: */
    if (lastChar != '\\')
      strncat(pofoName, "\\", MAX_FILENAME_LEN - strlen(pofoName));

    /* Skip path part in source: */
    pos = strrchr(source, '/');
    if (!pos)
      pos = strrchr(source, '\\');
    if (pos)
      source = pos + 1;

    ext = strrchr(source, '.');
    if (ext) {
      /* Replace dots before extension by underscores */
      while ((pos = strchr(source, '.')) != ext) {
        *pos = '_';
      }

      /* Append file name without extension: */
      len = ext - source;
      if (len > 8)
        len = 8;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, source, len);

      /* Append file name extension */
      len = 4;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, ext, len);
    }
    else {
      /* There is no extension */
      len = 8;
      if (len > MAX_FILENAME_LEN - strlen(pofoName))
        len = MAX_FILENAME_LEN - strlen(pofoName);
      strncat(pofoName, source, len);
    }
  }
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}


void setup()
{
  DBG_OUTPUT_PORT.begin(115200);
  delay(250);

  DBG_OUTPUT_PORT.printf("PortfolioESPLink 0.2 - (c) 2023 by Petr Kracik\n");
  DBG_OUTPUT_PORT.printf("based on Transfolio 1.0.1 - (c) 2018 by Klaus Peichl\n");

  /*
    Memory allocation
  */

  DBG_OUTPUT_PORT.println("Allocating mempory");
  payload = (unsigned char *)malloc(PAYLOAD_BUFSIZE);
  controlData = (unsigned char *)malloc(CONTROL_BUFSIZE);
  list = (unsigned char *)malloc(LIST_BUFSIZE);

  if (payload == NULL || controlData == NULL || list == NULL) {
    DBG_OUTPUT_PORT.printf( "Out of memory!\n");
    exit(EXIT_FAILURE);
  }

  DBG_OUTPUT_PORT.println("Setting up port");
  setupPort();

  DBG_OUTPUT_PORT.println("Opening filesystem");
  if (!FILESYSTEM.begin()) {
    DBG_OUTPUT_PORT.println("Corrupted or empty filesystem, formatting");
    FILESYSTEM.format();
    FILESYSTEM.begin();
  }

  File root = FILESYSTEM.open("/");
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    file = root.openNextFile();
  }
  DBG_OUTPUT_PORT.printf("\n");

  // Setup WiFi

  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }


  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }

  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  server.on("/list", HTTP_GET, handleFileList);

  // Setup HTTP Server
  server.begin();


}

void syncTick() {
  waitClockLow();
  writePort(0);
  waitClockHigh();
  writePort(2);
}

bool detectPortfolio() {
  byte recv;

  syncTick();
  recv = receiveByte();

  return recv == 90;
}

bool portfolioConnected = false;
void loop() {

  server.handleClient();

  DBG_OUTPUT_PORT.print("Waiting for connection");
  while (!detectPortfolio()) {
    delay(1);
  }

  DBG_OUTPUT_PORT.println("... Connected");

  delay(1);
}
