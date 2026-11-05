// Heltec Heatmap — Wi-Fi survey logger for Heltec WiFi LoRa 32 V3 / V4
//
// The board runs its own Wi-Fi hotspot ("Heltec-Heatmap-XXXX") and scans the
// 2.4 GHz band over and over, saving every network it hears to flash.
// Your iPhone sets the clock once (tap Start on http://192.168.4.1), then
// records GPS in the Heltec Heatmap web app. Afterwards you download the log
// here and import it into the app, which lines the two up by time.
//
// Button (PRG): short press = start/stop logging without a phone (unsynced clock).
//               hold 3 s     = toggle the OLED off/on to save power.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <U8g2lib.h>
#include <vector>
#include <algorithm>

// ---------- Board pins (same on V3 and V4) ----------
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_OLED_RST 21
#define PIN_VEXT     36   // LOW = power the OLED
#define PIN_LED      35
#define PIN_BUTTON   0

// ---------- Settings ----------
static const char* AP_PASSWORD   = "heatmap123";   // change if you like (8+ chars)
static const int   MIN_RSSI      = -95;            // ignore anything weaker
static const uint32_t SCAN_GAP_MS = 400;           // pause between scans
static const size_t MAX_NETS     = 8000;           // network table cap (fits V3 RAM)
static const size_t MIN_FREE_FS  = 24 * 1024;      // stop logging below this

static const char* NETS_FILE  = "/nets.csv";
static const char* SCANS_FILE = "/scans.txt";

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, PIN_OLED_RST, PIN_OLED_SCL, PIN_OLED_SDA);
WebServer server(80);

struct NetRef { uint64_t bssid; uint16_t id; };
std::vector<NetRef> nets;          // sorted by bssid
uint16_t nextNetId = 0;

bool logging = false;
bool synced = false;
uint64_t epochBase = 0;            // phone time (ms) at syncMillis
uint32_t syncMillis = 0;
uint32_t sessionScans = 0;
uint32_t totalScans = 0;
uint32_t lastScanDone = 0;
bool scanRunning = false;
bool oledOn = true;
bool fsFull = false;
String apName;

// Last scan, for the screen and the live page
struct Seen { String ssid; int rssi; int ch; bool open; };
std::vector<Seen> lastSeen;
int lastCount = 0;

// ---------- helpers ----------
uint64_t macToU64(const uint8_t* m) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) v = (v << 8) | m[i];
  return v;
}

String macToStr(uint64_t v) {
  char b[18];
  snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X",
           (int)((v >> 40) & 255), (int)((v >> 32) & 255), (int)((v >> 24) & 255),
           (int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255));
  return String(b);
}

uint64_t parseMac(const String& s) {
  uint64_t v = 0;
  int n = 0;
  for (size_t i = 0; i < s.length() && n < 6; i += 3, n++) {
    v = (v << 8) | strtoul(s.substring(i, i + 2).c_str(), nullptr, 16);
  }
  return v;
}

const char* authName(wifi_auth_mode_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA3";
    default: return "OTHER";
  }
}

uint64_t nowStamp() {
  // Synced: real phone time in ms. Unsynced: ms since boot.
  if (synced) return epochBase + (uint64_t)(millis() - syncMillis);
  return millis();
}

size_t fsFree() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

int findNet(uint64_t b) {
  auto it = std::lower_bound(nets.begin(), nets.end(), b,
                             [](const NetRef& r, uint64_t v) { return r.bssid < v; });
  if (it != nets.end() && it->bssid == b) return it->id;
  return -1;
}

int addNet(uint64_t b, int ch, const char* auth, String ssid) {
  if (nets.size() >= MAX_NETS) return -1;
  ssid.replace("\n", " ");
  ssid.replace("\r", " ");
  uint16_t id = nextNetId++;
  auto it = std::lower_bound(nets.begin(), nets.end(), b,
                             [](const NetRef& r, uint64_t v) { return r.bssid < v; });
  nets.insert(it, NetRef{b, id});
  File f = LittleFS.open(NETS_FILE, FILE_APPEND);
  if (f) {
    f.printf("%u,%s,%d,%s,%s\n", id, macToStr(b).c_str(), ch, auth, ssid.c_str());
    f.close();
  }
  return id;
}

void loadNets() {
  nets.clear();
  nextNetId = 0;
  File f = LittleFS.open(NETS_FILE, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int c1 = line.indexOf(',');
    if (c1 < 0) continue;
    int c2 = line.indexOf(',', c1 + 1);
    if (c2 < 0) continue;
    uint16_t id = line.substring(0, c1).toInt();
    uint64_t b = parseMac(line.substring(c1 + 1, c2));
    nets.push_back(NetRef{b, id});
    if (id >= nextNetId) nextNetId = id + 1;
  }
  f.close();
  std::sort(nets.begin(), nets.end(), [](const NetRef& a, const NetRef& b) { return a.bssid < b.bssid; });
}

void startSession(bool withClock, uint64_t phoneMs, const String& label) {
  if (withClock) {
    synced = true;
    epochBase = phoneMs;
    syncMillis = millis();
  }
  File f = LittleFS.open(SCANS_FILE, FILE_APPEND);
  if (f) {
    String safe = label;
    safe.replace(",", " ");
    safe.replace("\n", " ");
    f.printf("#S,%d,%llu,%s\n", synced ? 1 : 0, (unsigned long long)nowStamp(), safe.c_str());
    f.close();
  }
  sessionScans = 0;
  fsFull = false;
  logging = true;
}

void stopSession() { logging = false; }

// ---------- scanning ----------
void startScan() {
  // async, include hidden networks, active scan, 100 ms per channel
  WiFi.scanNetworks(true, true, false, 100);
  scanRunning = true;
}

void handleScanResult(int n) {
  scanRunning = false;
  lastScanDone = millis();
  if (n < 0) return;

  lastSeen.clear();
  lastCount = n;
  String line;
  if (logging) {
    line.reserve(16 + n * 9);
    line += String((unsigned long long)nowStamp());
  }

  for (int i = 0; i < n; i++) {
    int rssi = WiFi.RSSI(i);
    if (rssi < MIN_RSSI) continue;
    String ssid = WiFi.SSID(i);
    int ch = WiFi.channel(i);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    if (lastSeen.size() < 12) lastSeen.push_back(Seen{ssid, rssi, ch, auth == WIFI_AUTH_OPEN});

    if (logging) {
      uint64_t b = macToU64(WiFi.BSSID(i));
      int id = findNet(b);
      if (id < 0) id = addNet(b, ch, authName(auth), ssid);
      if (id < 0) continue;
      line += ';';
      line += id;
      line += ':';
      line += rssi;
    }
  }
  std::sort(lastSeen.begin(), lastSeen.end(), [](const Seen& a, const Seen& b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();

  if (logging) {
    if (fsFree() < MIN_FREE_FS) {
      fsFull = true;
      logging = false;
      return;
    }
    File f = LittleFS.open(SCANS_FILE, FILE_APPEND);
    if (f) {
      f.println(line);
      f.close();
    }
    sessionScans++;
    totalScans++;
    digitalWrite(PIN_LED, HIGH);
    delay(15);
    digitalWrite(PIN_LED, LOW);
  }
}

// ---------- screen ----------
void drawScreen() {
  if (!oledOn) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  char b[40];

  if (logging) {
    oled.drawBox(0, 0, 128, 11);
    oled.setDrawColor(0);
    snprintf(b, sizeof b, "REC %s  %lu scans", synced ? "GPS-sync" : "no-sync", (unsigned long)sessionScans);
    oled.drawStr(2, 9, b);
    oled.setDrawColor(1);
  } else {
    oled.drawStr(0, 9, fsFull ? "STOPPED: flash full" : "READY - not logging");
  }

  snprintf(b, sizeof b, "Heard %d  Known %u", lastCount, (unsigned)nets.size());
  oled.drawStr(0, 22, b);

  if (!lastSeen.empty()) {
    String s = lastSeen[0].ssid.length() ? lastSeen[0].ssid : String("(hidden)");
    if (s.length() > 14) s = s.substring(0, 14);
    snprintf(b, sizeof b, "%-14s %4ddBm", s.c_str(), lastSeen[0].rssi);
    oled.drawStr(0, 33, b);
    // signal bar: -95..-30
    int w = map(constrain(lastSeen[0].rssi, -95, -30), -95, -30, 2, 128);
    oled.drawFrame(0, 36, 128, 6);
    oled.drawBox(0, 36, w, 6);
  }

  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(0, 53, apName.c_str());
  snprintf(b, sizeof b, "192.168.4.1  free %uKB", (unsigned)(fsFree() / 1024));
  oled.drawStr(0, 62, b);
  oled.sendBuffer();
}

// ---------- web ----------
extern const char CONTROL_PAGE[];

void sendStatus() {
  String j;
  j.reserve(1200);
  j += "{\"logging\":"; j += logging ? "true" : "false";
  j += ",\"synced\":"; j += synced ? "true" : "false";
  j += ",\"sessionScans\":"; j += sessionScans;
  j += ",\"totalScans\":"; j += totalScans;
  j += ",\"known\":"; j += nets.size();
  j += ",\"heard\":"; j += lastCount;
  j += ",\"fsFull\":"; j += fsFull ? "true" : "false";
  j += ",\"freeKB\":"; j += fsFree() / 1024;
  j += ",\"totalKB\":"; j += LittleFS.totalBytes() / 1024;
  j += ",\"board\":\"";
#ifdef HELTEC_V4
  j += "V4";
#else
  j += "V3";
#endif
  j += "\",\"top\":[";
  for (size_t i = 0; i < lastSeen.size(); i++) {
    if (i) j += ',';
    String s = lastSeen[i].ssid;
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    j += "{\"ssid\":\""; j += s;
    j += "\",\"rssi\":"; j += lastSeen[i].rssi;
    j += ",\"ch\":"; j += lastSeen[i].ch;
    j += ",\"open\":"; j += lastSeen[i].open ? "true" : "false";
    j += '}';
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void streamFile(const char* path, size_t limit) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return;
  static uint8_t buf[1024];
  size_t left = limit;
  while (left > 0 && f.available()) {
    size_t n = f.read(buf, std::min(left, sizeof buf));
    if (n == 0) break;
    server.sendContent((const char*)buf, n);
    left -= n;
  }
  f.close();
}

size_t fileSize(const char* path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

void handleExport() {
  // Wait for any scan in progress so the files are not mid-write.
  uint32_t t0 = millis();
  while (scanRunning && WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < 3000) delay(20);

  const String h1 = "HELTECHEATMAP 1\n[nets]\n";
  const String h2 = "[scans]\n";
  size_t nSize = fileSize(NETS_FILE);
  size_t sSize = fileSize(SCANS_FILE);
  server.setContentLength(h1.length() + nSize + h2.length() + sSize);
  server.sendHeader("Content-Disposition", "attachment; filename=\"heltec-heatmap-log.txt\"");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", "");
  server.sendContent(h1);
  streamFile(NETS_FILE, nSize);
  server.sendContent(h2);
  streamFile(SCANS_FILE, sSize);
}

void setupWeb() {
  server.on("/", [] { server.send_P(200, "text/html", CONTROL_PAGE); });
  server.on("/api/status", sendStatus);
  server.on("/api/start", [] {
    String t = server.arg("t");
    String label = server.arg("label");
    if (t.length() > 9) startSession(true, strtoull(t.c_str(), nullptr, 10), label);
    else startSession(false, 0, label);
    sendStatus();
  });
  server.on("/api/stop", [] { stopSession(); sendStatus(); });
  server.on("/api/clear", HTTP_POST, [] {
    logging = false;
    LittleFS.remove(NETS_FILE);
    LittleFS.remove(SCANS_FILE);
    nets.clear();
    nextNetId = 0;
    totalScans = 0;
    sessionScans = 0;
    fsFull = false;
    sendStatus();
  });
  server.on("/export.txt", handleExport);
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
}

// ---------- button ----------
void checkButton() {
  static bool was = false;
  static uint32_t downAt = 0;
  static bool longDone = false;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (down && !was) { downAt = millis(); longDone = false; }
  if (down && !longDone && millis() - downAt > 3000) {
    longDone = true;
    oledOn = !oledOn;
    oled.setPowerSave(oledOn ? 0 : 1);
  }
  if (!down && was && !longDone && millis() - downAt > 40) {
    if (logging) stopSession();
    else startSession(false, 0, "button");
  }
  was = down;
}

// ---------- main ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(50);

  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
  oled.clearBuffer();
  oled.drawStr(0, 12, "Heltec Heatmap");
  oled.drawStr(0, 28, "starting...");
  oled.sendBuffer();

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  loadNets();

  uint8_t mac[6];
  WiFi.mode(WIFI_AP_STA);
  WiFi.macAddress(mac);
  char nm[32];
  snprintf(nm, sizeof nm, "Heltec-Heatmap-%02X%02X", mac[4], mac[5]);
  apName = nm;
  WiFi.softAP(apName.c_str(), AP_PASSWORD, 1, 0, 2);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // phone is right next to it; less self-noise
  setupWeb();

  Serial.printf("AP %s  pass %s  http://192.168.4.1\n", apName.c_str(), AP_PASSWORD);
  startScan();
}

void loop() {
  server.handleClient();
  checkButton();

  int r = WiFi.scanComplete();
  if (scanRunning && r != WIFI_SCAN_RUNNING) {
    handleScanResult(r);
    drawScreen();
  }
  if (!scanRunning && millis() - lastScanDone > (logging ? SCAN_GAP_MS : 2500)) startScan();

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw > 1000) { lastDraw = millis(); drawScreen(); }
  delay(2);
}
