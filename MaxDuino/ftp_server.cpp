#include "ftp_server.h"

#if defined(FTP_SERVER_ENABLED) && defined(ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include "file_utils.h"

#ifndef FTP_SERVER_USER
#define FTP_SERVER_USER "maxduino"
#endif

#ifndef FTP_SERVER_PASSWORD
#define FTP_SERVER_PASSWORD "maxduino"
#endif

#ifndef FTP_CONTROL_PORT
#define FTP_CONTROL_PORT 21
#endif

#ifndef FTP_DATA_PORT
#define FTP_DATA_PORT 50009
#endif

#ifndef FTP_DATA_TIMEOUT_MS
#define FTP_DATA_TIMEOUT_MS 30000UL
#endif

#ifndef FTP_IDLE_TIMEOUT_MS
#define FTP_IDLE_TIMEOUT_MS 300000UL
#endif

#define FTP_CMD_BUFFER_LEN 160
#define FTP_TRANSFER_BUFFER_LEN 1024
#define FTP_PATH_BUFFER_LEN 256

static WiFiServer controlServer(FTP_CONTROL_PORT, 1);
static WiFiServer dataServer(FTP_DATA_PORT, 1);
static WiFiClient controlClient;
static bool controlServerStarted = false;
static bool authenticated = false;
static bool userAccepted = false;
static bool passiveReady = false;
static bool activeReady = false;
static IPAddress activeIp;
static uint16_t activePort = 0;
static String cwd = "/";
static String renameFrom;
static uint32_t restartOffset = 0;
static char commandLine[FTP_CMD_BUFFER_LEN];
static size_t commandPos = 0;
static unsigned long lastControlActivity = 0;
static uint8_t transferBuffer[FTP_TRANSFER_BUFFER_LEN];

static void reply(int code, const char *message)
{
  if (!controlClient) return;
  controlClient.print(code);
  controlClient.print(' ');
  controlClient.print(message);
  controlClient.print("\r\n");
}

static String clean_arg(String value)
{
  value.trim();
  if (value.length() >= 2) {
    char first = value[0];
    char last = value[value.length() - 1];
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      value = value.substring(1, value.length() - 1);
    }
  }
  value.replace('\\', '/');
  return value;
}

static String join_path(const String &base, const char *name)
{
  if (base == "/") {
    return String("/") + name;
  }
  return base + "/" + name;
}

static String basename_for_path(const String &path)
{
  if (path == "/") return "/";
  int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  return path.substring(slash + 1);
}

static bool normalize_path(const String &arg, String &out)
{
  String value = clean_arg(arg);
  String path;

  if (value.length() == 0) {
    path = cwd;
  } else if (value[0] == '/') {
    path = value;
  } else if (cwd == "/") {
    path = "/" + value;
  } else {
    path = cwd + "/" + value;
  }

  String result = "/";
  int pos = 0;
  while (pos < path.length()) {
    while (pos < path.length() && path[pos] == '/') pos++;
    int next = path.indexOf('/', pos);
    if (next < 0) next = path.length();

    String part = path.substring(pos, next);
    if (part == "..") {
      if (result.length() > 1) {
        int slash = result.lastIndexOf('/');
        result = (slash <= 0) ? "/" : result.substring(0, slash);
      }
    } else if (part.length() > 0 && part != ".") {
      if (part.length() >= FTP_PATH_BUFFER_LEN) return false;
      if (result != "/") result += "/";
      result += part;
      if (result.length() >= FTP_PATH_BUFFER_LEN) return false;
    }

    pos = next + 1;
  }

  out = result;
  return true;
}

static String list_path_arg(String arg)
{
  arg.trim();
  while (arg.startsWith("-")) {
    int space = arg.indexOf(' ');
    if (space < 0) return "";
    arg = arg.substring(space + 1);
    arg.trim();
  }
  return arg;
}

static bool path_is_dir(const String &path)
{
  SdBaseFile file;
  bool ok = file.open(path.c_str(), O_RDONLY) && file.isDir();
  file.close();
  return ok;
}

static void begin_passive()
{
  dataServer.stop();
  dataServer.begin(FTP_DATA_PORT);
  dataServer.setNoDelay(true);
  passiveReady = true;
  activeReady = false;
}

static IPAddress control_local_ip()
{
  IPAddress ip = controlClient.localIP();
  if (ip == IPAddress(0, 0, 0, 0)) {
    ip = WiFi.localIP();
  }
  if (ip == IPAddress(0, 0, 0, 0)) {
    ip = WiFi.softAPIP();
  }
  return ip;
}

static WiFiClient open_data_connection()
{
  if (passiveReady) {
    unsigned long startMillis = millis();
    while (millis() - startMillis < FTP_DATA_TIMEOUT_MS) {
      WiFiClient data = dataServer.available();
      if (data) {
        data.setNoDelay(true);
        passiveReady = false;
        dataServer.stop();
        return data;
      }
      delay(1);
    }
    passiveReady = false;
    dataServer.stop();
  } else if (activeReady) {
    WiFiClient data;
    if (data.connect(activeIp, activePort, FTP_DATA_TIMEOUT_MS)) {
      data.setNoDelay(true);
      return data;
    }
  }

  return WiFiClient();
}

static bool send_all(WiFiClient &client, const uint8_t *buffer, size_t length)
{
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < length) {
    if (!client.connected()) return false;
    size_t written = client.write(buffer + sent, length - sent);
    if (written > 0) {
      sent += written;
      lastProgress = millis();
    } else if (millis() - lastProgress > FTP_DATA_TIMEOUT_MS) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

static void send_list_entry(WiFiClient &data, const char *name, bool isDir, uint32_t size, bool namesOnly)
{
  if (namesOnly) {
    data.print(name);
    data.print("\r\n");
    return;
  }

  data.print(isDir ? "drwxr-xr-x" : "-rw-r--r--");
  data.print(" 1 maxduino maxduino ");
  data.print(size);
  data.print(" Jan 01  1980 ");
  data.print(name);
  data.print("\r\n");
}

static bool send_listing(WiFiClient &data, const String &path, bool namesOnly)
{
  SdBaseFile target;
  if (!target.open(path.c_str(), O_RDONLY)) return false;

  if (!target.isDir()) {
    String name = basename_for_path(path);
    send_list_entry(data, name.c_str(), false, target.fileSize(), namesOnly);
    target.close();
    return true;
  }

  target.rewind();
  while (true) {
    SdBaseFile child;
    if (!child.openNext(&target, O_RDONLY)) break;

    char name[FTP_PATH_BUFFER_LEN];
    child.getName(name, sizeof(name));
    bool childIsDir = child.isDir();
    uint32_t childSize = child.fileSize();
    child.close();

    if (name[0] != 0 && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      send_list_entry(data, name, childIsDir, childSize, namesOnly);
      yield();
    }
  }

  target.close();
  return true;
}

static bool remove_recursive(const String &path)
{
  if (path == "/") return false;

  SdBaseFile target;
  if (!target.open(path.c_str(), O_RDONLY)) return false;
  bool isDir = target.isDir();
  target.close();

  if (!isDir) {
    return sd.remove(path.c_str());
  }

  SdBaseFile dir;
  if (!dir.open(path.c_str(), O_RDONLY)) return false;
  bool ok = true;

  while (ok) {
    SdBaseFile child;
    if (!child.openNext(&dir, O_RDONLY)) break;

    char name[FTP_PATH_BUFFER_LEN];
    child.getName(name, sizeof(name));
    bool childIsDir = child.isDir();
    child.close();

    if (name[0] == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

    String childPath = join_path(path, name);
    if (childIsDir) {
      ok = remove_recursive(childPath);
    } else {
      ok = sd.remove(childPath.c_str());
    }
    yield();
  }

  dir.close();
  return ok && sd.rmdir(path.c_str());
}

static void handle_pasv()
{
  begin_passive();
  IPAddress ip = control_local_ip();
  String message = "Entering Passive Mode (";
  message += ip[0];
  message += ",";
  message += ip[1];
  message += ",";
  message += ip[2];
  message += ",";
  message += ip[3];
  message += ",";
  message += FTP_DATA_PORT / 256;
  message += ",";
  message += FTP_DATA_PORT % 256;
  message += ").";
  reply(227, message.c_str());
}

static void handle_epsv()
{
  begin_passive();
  String message = "Entering Extended Passive Mode (|||";
  message += FTP_DATA_PORT;
  message += "|).";
  reply(229, message.c_str());
}

static void handle_port(String arg)
{
  int values[6];
  int start = 0;
  for (int i = 0; i < 6; i++) {
    int comma = arg.indexOf(',', start);
    String token = (comma < 0) ? arg.substring(start) : arg.substring(start, comma);
    token.trim();
    if (token.length() == 0) {
      reply(501, "Bad PORT address.");
      return;
    }
    values[i] = token.toInt();
    if (values[i] < 0 || values[i] > 255) {
      reply(501, "Bad PORT address.");
      return;
    }
    start = comma + 1;
    if (comma < 0 && i < 5) {
      reply(501, "Bad PORT address.");
      return;
    }
  }

  activeIp = IPAddress(values[0], values[1], values[2], values[3]);
  activePort = (uint16_t)((values[4] << 8) | values[5]);
  activeReady = true;
  passiveReady = false;
  dataServer.stop();
  reply(200, "PORT command successful.");
}

static void handle_list(String arg, bool namesOnly)
{
  String path;
  if (!normalize_path(list_path_arg(arg), path)) {
    reply(550, "Invalid path.");
    return;
  }

  reply(150, "Opening data connection.");
  WiFiClient data = open_data_connection();
  if (!data) {
    reply(425, "Can't open data connection.");
    return;
  }

  bool ok = send_listing(data, path, namesOnly);
  data.stop();
  reply(ok ? 226 : 550, ok ? "Transfer complete." : "Directory listing failed.");
}

static void handle_retr(String arg)
{
  String path;
  if (!normalize_path(arg, path)) {
    reply(550, "Invalid path.");
    return;
  }

  SdBaseFile file;
  if (!file.open(path.c_str(), O_RDONLY) || file.isDir()) {
    file.close();
    reply(550, "File not available.");
    return;
  }

  if (restartOffset > 0 && !file.seekSet(restartOffset)) {
    file.close();
    restartOffset = 0;
    reply(550, "Invalid restart offset.");
    return;
  }
  restartOffset = 0;

  reply(150, "Opening data connection.");
  WiFiClient data = open_data_connection();
  if (!data) {
    file.close();
    reply(425, "Can't open data connection.");
    return;
  }

  bool ok = true;
  while (ok) {
    int count = file.read(transferBuffer, sizeof(transferBuffer));
    if (count < 0) {
      ok = false;
      break;
    }
    if (count == 0) break;
    ok = send_all(data, transferBuffer, (size_t)count);
    yield();
  }

  file.close();
  data.stop();
  reply(ok ? 226 : 426, ok ? "Transfer complete." : "Transfer aborted.");
}

static void handle_stor(String arg, bool append)
{
  String path;
  if (!normalize_path(arg, path) || path == "/") {
    reply(550, "Invalid path.");
    return;
  }

  SdBaseFile existing;
  if (existing.open(path.c_str(), O_RDONLY) && existing.isDir()) {
    existing.close();
    reply(550, "Path is a directory.");
    return;
  }
  existing.close();

  oflag_t flags = append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
  SdBaseFile file;
  if (!file.open(path.c_str(), flags)) {
    reply(550, "Could not open file for writing.");
    return;
  }

  reply(150, "Opening data connection.");
  WiFiClient data = open_data_connection();
  if (!data) {
    file.close();
    reply(425, "Can't open data connection.");
    return;
  }

  bool ok = true;
  unsigned long lastData = millis();
  while (data.connected() || data.available()) {
    int available = data.available();
    if (available > 0) {
      int want = available;
      if (want > (int)sizeof(transferBuffer)) want = sizeof(transferBuffer);
      int count = data.read(transferBuffer, want);
      if (count > 0) {
        if (file.write(transferBuffer, (size_t)count) != (size_t)count) {
          ok = false;
          break;
        }
        lastData = millis();
      }
    } else if (millis() - lastData > FTP_DATA_TIMEOUT_MS) {
      break;
    } else {
      delay(1);
    }
    yield();
  }

  ok = file.sync() && ok;
  file.close();
  data.stop();
  restartOffset = 0;
  reply(ok ? 226 : 426, ok ? "Transfer complete." : "Transfer aborted.");
}

static void handle_size(String arg)
{
  String path;
  if (!normalize_path(arg, path)) {
    reply(550, "Invalid path.");
    return;
  }

  SdBaseFile file;
  if (!file.open(path.c_str(), O_RDONLY) || file.isDir()) {
    file.close();
    reply(550, "File not available.");
    return;
  }

  String message = String(file.fileSize());
  file.close();
  reply(213, message.c_str());
}

static void handle_cwd(String arg)
{
  String path;
  if (!normalize_path(arg, path) || !path_is_dir(path)) {
    reply(550, "Directory not available.");
    return;
  }

  cwd = path;
  reply(250, "Directory changed.");
}

static void handle_mkd(String arg)
{
  String path;
  if (!normalize_path(arg, path) || path == "/") {
    reply(550, "Invalid path.");
    return;
  }

  if (sd.mkdir(path.c_str(), true)) {
    String message = "\"" + path + "\" created.";
    reply(257, message.c_str());
  } else {
    reply(550, "Create directory failed.");
  }
}

static void handle_dele(String arg)
{
  String path;
  if (!normalize_path(arg, path) || path == "/") {
    reply(550, "Invalid path.");
    return;
  }

  SdBaseFile file;
  if (!file.open(path.c_str(), O_RDONLY) || file.isDir()) {
    file.close();
    reply(550, "File not available.");
    return;
  }
  file.close();

  bool ok = sd.remove(path.c_str());
  reply(ok ? 250 : 550, ok ? "File deleted." : "Delete failed.");
}

static void handle_rmd(String arg)
{
  String path;
  if (!normalize_path(arg, path) || path == "/") {
    reply(550, "Invalid path.");
    return;
  }

  if (!path_is_dir(path)) {
    reply(550, "Directory not available.");
    return;
  }

  bool ok = remove_recursive(path);
  reply(ok ? 250 : 550, ok ? "Directory deleted." : "Directory delete failed.");
}

static void handle_rnto(String arg)
{
  if (renameFrom.length() == 0) {
    reply(503, "Use RNFR first.");
    return;
  }

  String toPath;
  if (!normalize_path(arg, toPath) || toPath == "/") {
    renameFrom = "";
    reply(550, "Invalid path.");
    return;
  }

  bool ok = sd.rename(renameFrom.c_str(), toPath.c_str());
  renameFrom = "";
  reply(ok ? 250 : 550, ok ? "Rename successful." : "Rename failed.");
}

static void handle_command(char *line)
{
  String raw(line);
  raw.trim();
  if (raw.length() == 0) return;

  int space = raw.indexOf(' ');
  String command = (space < 0) ? raw : raw.substring(0, space);
  String arg = (space < 0) ? "" : raw.substring(space + 1);
  command.toUpperCase();
  arg.trim();

  if (command == "USER") {
    userAccepted = (arg == FTP_SERVER_USER);
    authenticated = false;
    reply(userAccepted ? 331 : 530, userAccepted ? "Password required." : "Invalid user.");
    return;
  }

  if (command == "PASS") {
    authenticated = userAccepted && (arg == FTP_SERVER_PASSWORD);
    reply(authenticated ? 230 : 530, authenticated ? "Login successful." : "Login incorrect.");
    return;
  }

  if (command == "QUIT") {
    reply(221, "Goodbye.");
    controlClient.stop();
    return;
  }

  if (command == "NOOP") {
    reply(200, "OK.");
    return;
  }

  if (command == "SYST") {
    reply(215, "UNIX Type: L8");
    return;
  }

  if (command == "FEAT") {
    controlClient.print("211-Features\r\n");
    controlClient.print(" UTF8\r\n");
    controlClient.print(" PASV\r\n");
    controlClient.print(" EPSV\r\n");
    controlClient.print(" SIZE\r\n");
    controlClient.print(" REST STREAM\r\n");
    controlClient.print("211 End\r\n");
    return;
  }

  if (command == "AUTH") {
    reply(502, "TLS is not supported.");
    return;
  }

  if (!authenticated) {
    reply(530, "Please login with USER and PASS.");
    return;
  }

  if (command == "OPTS") {
    reply(200, "Option accepted.");
  } else if (command == "PWD" || command == "XPWD") {
    String message = "\"" + cwd + "\" is current directory.";
    reply(257, message.c_str());
  } else if (command == "CWD") {
    handle_cwd(arg);
  } else if (command == "CDUP" || command == "XCUP") {
    handle_cwd("..");
  } else if (command == "TYPE") {
    reply(200, "Type set.");
  } else if (command == "MODE" || command == "STRU") {
    reply(200, "Command okay.");
  } else if (command == "PASV") {
    handle_pasv();
  } else if (command == "EPSV") {
    handle_epsv();
  } else if (command == "PORT") {
    handle_port(arg);
  } else if (command == "LIST") {
    handle_list(arg, false);
  } else if (command == "NLST") {
    handle_list(arg, true);
  } else if (command == "RETR") {
    handle_retr(arg);
  } else if (command == "STOR") {
    handle_stor(arg, false);
  } else if (command == "APPE") {
    handle_stor(arg, true);
  } else if (command == "DELE") {
    handle_dele(arg);
  } else if (command == "MKD" || command == "XMKD") {
    handle_mkd(arg);
  } else if (command == "RMD" || command == "XRMD") {
    handle_rmd(arg);
  } else if (command == "RNFR") {
    if (normalize_path(arg, renameFrom)) {
      reply(350, "Ready for RNTO.");
    } else {
      renameFrom = "";
      reply(550, "Invalid path.");
    }
  } else if (command == "RNTO") {
    handle_rnto(arg);
  } else if (command == "SIZE") {
    handle_size(arg);
  } else if (command == "REST") {
    restartOffset = (uint32_t)arg.toInt();
    reply(350, "Restart position accepted.");
  } else if (command == "MDTM") {
    reply(213, "19800101000000");
  } else if (command == "ABOR") {
    dataServer.stop();
    passiveReady = false;
    reply(226, "Abort successful.");
  } else {
    reply(502, "Command not implemented.");
  }
}

void ftp_server_begin()
{
  if (controlServerStarted) return;
  controlServer.begin(FTP_CONTROL_PORT);
  controlServer.setNoDelay(true);
  controlServerStarted = true;
}

void ftp_server_loop()
{
  if (!controlServerStarted) return;

  if (controlClient && !controlClient.connected()) {
    controlClient.stop();
  }

  WiFiClient newClient = controlServer.available();
  if (newClient) {
    if (controlClient && controlClient.connected()) {
      newClient.print("421 Only one FTP session is supported.\r\n");
      newClient.stop();
    } else {
      controlClient = newClient;
      controlClient.setNoDelay(true);
      authenticated = false;
      userAccepted = false;
      passiveReady = false;
      activeReady = false;
      cwd = "/";
      renameFrom = "";
      restartOffset = 0;
      commandPos = 0;
      lastControlActivity = millis();
      reply(220, "MaxDuino FTP server ready.");
    }
  }

  if (!(controlClient && controlClient.connected())) return;

  if (millis() - lastControlActivity > FTP_IDLE_TIMEOUT_MS) {
    reply(421, "Idle timeout.");
    controlClient.stop();
    return;
  }

  while (controlClient.available()) {
    char c = (char)controlClient.read();
    lastControlActivity = millis();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      commandLine[commandPos] = 0;
      handle_command(commandLine);
      commandPos = 0;
      if (!(controlClient && controlClient.connected())) break;
    } else if (commandPos < sizeof(commandLine) - 1) {
      commandLine[commandPos++] = c;
    } else {
      commandPos = 0;
      reply(500, "Command line too long.");
    }
  }
}

#endif // defined(FTP_SERVER_ENABLED) && defined(ESP32)
