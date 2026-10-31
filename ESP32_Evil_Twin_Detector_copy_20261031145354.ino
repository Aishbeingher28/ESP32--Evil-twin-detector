#include <WiFi.h>
#include <WebServer.h>

const int SAFE_LED  = 26;
const int ALERT_LED = 27;

String trusted[] = {"Amrita_CHN2", "Amrita", "Amrita_CH", "Amrita_Guest", "ESP32_Demo"};
const int NT = 5;

String knownPrefix[40];
int knownCount = 0;

const int MAX_LOG = 20;
String logSsid[MAX_LOG];
String logMac[MAX_LOG];
unsigned long logTime[MAX_LOG];
int logCount = 0;

String prevFake[10];
int prevFakeCount = 0;

bool alertNow = false;
int scanCount = 0;
bool scanning = false;
unsigned long lastScan = 0;

WebServer server(80);

bool isTrustedName(String s) {
  for (int i = 0; i < NT; i++) if (s == trusted[i]) return true;
  return false;
}

String prefixOf(String mac) {
  return mac.substring(0, 11);
}

bool isKnown(String p) {
  for (int i = 0; i < knownCount; i++) if (knownPrefix[i] == p) return true;
  return false;
}

bool inPrev(String m) {
  for (int i = 0; i < prevFakeCount; i++) if (prevFake[i] == m) return true;
  return false;
}

String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

void addLog(String ssid, String mac) {
  if (logCount == MAX_LOG) {
    for (int i = 1; i < MAX_LOG; i++) {
      logSsid[i - 1] = logSsid[i];
      logMac[i - 1] = logMac[i];
      logTime[i - 1] = logTime[i];
    }
    logCount--;
  }
  logSsid[logCount] = ssid;
  logMac[logCount] = mac;
  logTime[logCount] = millis() / 1000;
  logCount++;
}

void learnScan() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (!isTrustedName(WiFi.SSID(i))) continue;
    String p = prefixOf(WiFi.BSSIDstr(i));
    if (!isKnown(p) && knownCount < 40) knownPrefix[knownCount++] = p;
  }
  WiFi.scanDelete();
}

void processScan(int n) {
  String nowMac[10];
  String nowSsid[10];
  int nowCount = 0;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!isTrustedName(ssid)) continue;
    String mac = WiFi.BSSIDstr(i);
    if (!isKnown(prefixOf(mac)) && nowCount < 10) {
      nowMac[nowCount] = mac;
      nowSsid[nowCount] = ssid;
      nowCount++;
    }
  }

  for (int j = 0; j < nowCount; j++) {
    Serial.println("ALERT! Fake network: " + nowSsid[j] + " | " + nowMac[j]);
    if (!inPrev(nowMac[j])) addLog(nowSsid[j], nowMac[j]);
  }

  prevFakeCount = nowCount;
  for (int j = 0; j < nowCount; j++) prevFake[j] = nowMac[j];

  alertNow = (nowCount > 0);
  scanCount++;

  if (alertNow) {
    digitalWrite(SAFE_LED, LOW);
    digitalWrite(ALERT_LED, HIGH);
  } else {
    Serial.println("All safe.");
    digitalWrite(SAFE_LED, HIGH);
    digitalWrite(ALERT_LED, LOW);
  }
}

void handleRoot() {
  String h = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  h += "<meta http-equiv='refresh' content='3'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<title>Evil Twin Detector</title><style>";
  h += "body{font-family:Arial;margin:20px;background:#111;color:#eee}";
  h += ".box{padding:16px;border-radius:8px;font-size:22px;font-weight:bold;margin-bottom:16px}";
  h += ".safe{background:#1b5e20}.alert{background:#b71c1c}";
  h += "table{border-collapse:collapse;width:100%}";
  h += "th,td{border:1px solid #444;padding:8px;text-align:left}th{background:#222}";
  h += "</style></head><body>";
  h += "<h2>ESP32 Evil Twin Detector</h2>";

  if (alertNow) h += "<div class='box alert'>ALERT: fake network detected</div>";
  else h += "<div class='box safe'>All safe</div>";

  h += "<p>Trusted MAC prefixes learned: " + String(knownCount) + "<br>";
  h += "Scans done: " + String(scanCount) + "<br>";
  h += "Uptime: " + String(millis() / 1000) + " s</p>";

  h += "<h3>Event log</h3><table><tr><th>#</th><th>Time since start (s)</th><th>Network name</th><th>Fake MAC</th></tr>";
  if (logCount == 0) {
    h += "<tr><td colspan='4'>No events yet</td></tr>";
  } else {
    for (int i = logCount - 1; i >= 0; i--) {
      h += "<tr><td>" + String(i + 1) + "</td><td>" + String(logTime[i]) + "</td><td>" + esc(logSsid[i]) + "</td><td>" + logMac[i] + "</td></tr>";
    }
  }
  h += "</table></body></html>";
  server.send(200, "text/html", h);
}

void setup() {
  Serial.begin(115200);
  pinMode(SAFE_LED, OUTPUT);
  pinMode(ALERT_LED, OUTPUT);
  digitalWrite(SAFE_LED, HIGH);
  digitalWrite(ALERT_LED, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Learning trusted networks (about 25 s)...");
  for (int r = 0; r < 8; r++) learnScan();
  Serial.println("Learned " + String(knownCount) + " trusted MAC prefixes. Monitoring.");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32-Detector", "CHANGE_THIS_PASSWORD");
  Serial.print("Web page at http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();

  if (!scanning && millis() - lastScan > 5000) {
    WiFi.scanNetworks(true);
    scanning = true;
  }

  if (scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      // still scanning
    } else if (n < 0) {
      scanning = false;
      lastScan = millis();
    } else {
      Serial.println("---- Scan ----");
      processScan(n);
      WiFi.scanDelete();
      scanning = false;
      lastScan = millis();
    }
  }
}