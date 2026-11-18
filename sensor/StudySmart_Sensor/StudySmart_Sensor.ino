/*
  StudySmart - noise sensor
  Somers Hall, Group Study Room 312  ->  ThingSpeak channel 3577145

  What it does:
    - Measures the room's noise level once a second (INMP441 microphone)
    - Keeps a rolling average of the last 5 minutes
    - Every 30 seconds, sends that 5-minute average to ThingSpeak (Field 1)
    - Only measures loudness. It never records or sends audio.

  Wi-Fi:
    It tries StFX eduroam first, using your StFX login in the formats StFX
    accepts. If eduroam won't connect, it tries your phone hotspot as a backup.
    It keeps cycling through these until one works, and measures the whole time.

  Wiring (INMP441 -> ESP32):
    VDD -> 3V3        GND -> GND        L/R -> GND
    SCK -> GPIO 32    WS  -> GPIO 25    SD  -> GPIO 33

  Blue light on the board:
    blinking = connecting
    on       = connected
    off      = not connected (it keeps measuring and retries)

  Serial Monitor: 115200 baud
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_I2S.h>
#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#endif

// =====================  FILL THESE IN  =====================
// Never upload this file to GitHub with your real details in it.

// StFX eduroam
const char* EDU_EMAIL     = "x2025abc@stfx.ca";      // your full StFX email address
const char* EDU_PASSWORD  = "YOUR_STFX_PASSWORD";    // same password as Moodle / email

// Backup: phone hotspot (leave as-is if you don't want a backup)
const char* HOTSPOT_NAME     = "YOUR_HOTSPOT_NAME";
const char* HOTSPOT_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// ThingSpeak > your channel > API Keys > Write API Key
const char* WRITE_API_KEY = "YOUR_WRITE_API_KEY";
// ===========================================================

// ---------- Settings ----------
float CALIBRATION_OFFSET = -30.0;            // from Step 3
const int QUIET_BELOW    = 45;               // under this = Quiet
const int LOUD_FROM      = 60;               // this or higher = Loud
const int AVERAGE_SECONDS          = 300;    // average over the last 5 minutes
const unsigned long SEND_EVERY_MS  = 30000;  // send every 30 seconds (ThingSpeak minimum is 15)
const unsigned long WIFI_TRY_MS    = 30000;  // how long to give each Wi-Fi option

// ---------- Pins ----------
const int MIC_SCK = 32;
const int MIC_WS  = 25;
const int MIC_SD  = 33;
const int LED_PIN = 2;

// ---------- Microphone ----------
const int SAMPLE_RATE = 16000;
const int BLOCK       = 512;
int32_t samples[BLOCK];
I2SClass mic;

double   sumSquares  = 0;
uint32_t sampleCount = 0;
uint32_t zeroBlocks  = 0;
uint32_t totalBlocks = 0;
unsigned long lastSecond = 0;

// ---------- 5-minute history (one reading per second) ----------
float history[AVERAGE_SECONDS];
int historyCount = 0;
int historyNext  = 0;
unsigned long lastSend = 0;

// ---------- Wi-Fi options ----------
// 0 = eduroam, login "x2025abc@stfx.ca"
// 1 = eduroam, outer login "x2025abc@stfx.ca", inner login "x2025abc"
// 2 = eduroam, login "x2025abc"
// 3 = phone hotspot
const int WIFI_OPTIONS = 4;
int  wifiOption = -1;
unsigned long wifiOptionStarted = 0;
bool wasConnected = false;
char eduShort[65];   // StFX username without "@stfx.ca"

// ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  Serial.println();
  Serial.println("StudySmart noise sensor - Room 312");

  // Make the short username, e.g. "x2025abc@stfx.ca" -> "x2025abc"
  strncpy(eduShort, EDU_EMAIL, sizeof(eduShort) - 1);
  eduShort[sizeof(eduShort) - 1] = '\0';
  char* at = strchr(eduShort, '@');
  if (at) *at = '\0';

  // Microphone
  mic.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  if (!mic.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("Could not start the microphone. Check the wiring, then press EN to restart.");
    while (true) delay(1000);
  }
  unsigned long start = millis();
  while (millis() - start < 300) {          // throw away start-up noise
    mic.readBytes((char*)samples, sizeof(samples));
  }

  // Wi-Fi: start the first option, then keep measuring while it connects
  WiFi.mode(WIFI_STA);
  nextWifiOption();

  lastSecond = millis();
  lastSend = millis();
}

void loop() {
  readMicrophone();
  handleWifi();

  // Once a second: turn the sound into a dB reading and add it to the history
  if (millis() - lastSecond >= 1000) {
    if (zeroBlocks == totalBlocks) {
      Serial.println("No sound data from the microphone. Check SD -> 33, L/R -> GND and VDD -> 3V3.");
    } else {
      float db = currentDb();
      addToHistory(db);
      Serial.printf("Now: %5.1f dB  %-8s   5-min average: %5.1f dB   Wi-Fi: %s\n",
                    db, levelName(db), averageDb(),
                    WiFi.status() == WL_CONNECTED ? "connected" : "connecting...");
    }
    sumSquares = 0; sampleCount = 0; zeroBlocks = 0; totalBlocks = 0;
    lastSecond = millis();
  }

  // Every 30 seconds: send the 5-minute average
  if (millis() - lastSend >= SEND_EVERY_MS) {
    lastSend = millis();
    if (historyCount > 0) {
      float avg = averageDb();
      if (WiFi.status() == WL_CONNECTED) {
        bool ok = sendToThingSpeak(avg);
        Serial.printf(">> Sent 5-min average %.1f dB (%s) to ThingSpeak: %s\n",
                      avg, levelName(avg), ok ? "OK" : "FAILED (check WRITE_API_KEY)");
      } else {
        Serial.println(">> Not connected to Wi-Fi yet, reading not sent.");
      }
    }
  }
}

// ---------- Wi-Fi ----------

bool wifiOptionUsable(int option) {
  bool hasAt = strchr(EDU_EMAIL, '@') != NULL;
  bool eduFilledIn = strcmp(EDU_PASSWORD, "YOUR_STFX_PASSWORD") != 0;
  bool hotspotFilledIn = strcmp(HOTSPOT_NAME, "YOUR_HOTSPOT_NAME") != 0;
  if (option == 0) return eduFilledIn;
  if (option == 1 || option == 2) return eduFilledIn && hasAt;
  if (option == 3) return hotspotFilledIn;
  return false;
}

void startWifiOption(int option) {
  WiFi.disconnect();
  delay(100);

  if (option <= 2) {
    const char* outer = (option == 2) ? eduShort : EDU_EMAIL;
    const char* inner = (option == 0) ? EDU_EMAIL : eduShort;
    Serial.printf("\nTrying eduroam (login: %s)...\n", inner);
    WiFi.begin("eduroam", WPA2_AUTH_PEAP, outer, inner, EDU_PASSWORD);
  } else {
#if __has_include("esp_eap_client.h")
    esp_wifi_sta_enterprise_disable();   // switch off the eduroam login mode
#endif
    Serial.printf("\nTrying hotspot \"%s\"...\n", HOTSPOT_NAME);
    WiFi.begin(HOTSPOT_NAME, HOTSPOT_PASSWORD);
  }
  wifiOptionStarted = millis();
}

void nextWifiOption() {
  for (int step = 1; step <= WIFI_OPTIONS; step++) {
    int option = (wifiOption + step + WIFI_OPTIONS) % WIFI_OPTIONS;
    if (wifiOptionUsable(option)) {
      wifiOption = option;
      startWifiOption(option);
      return;
    }
  }
  Serial.println("\nNo Wi-Fi details filled in. Add your StFX login or hotspot at the top of the code.");
  wifiOptionStarted = millis();
}

void handleWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
    if (!wasConnected) {
      wasConnected = true;
      Serial.printf("Connected to %s! IP address: %s\n",
                    wifiOption == 3 ? HOTSPOT_NAME : "eduroam",
                    WiFi.localIP().toString().c_str());
      if (wifiOption <= 2) {
        Serial.printf("(This login format worked: option %d)\n", wifiOption);
      }
    }
    return;
  }

  if (wasConnected) {
    wasConnected = false;
    Serial.println("Wi-Fi lost. Reconnecting...");
    startWifiOption(wifiOption);        // retry the one that worked first
    return;
  }

  // Blink while connecting
  digitalWrite(LED_PIN, (millis() / 250) % 2);

  // Give each option 30 seconds, then try the next one
  if (millis() - wifiOptionStarted >= WIFI_TRY_MS) {
    Serial.println("No luck with that one.");
    nextWifiOption();
  }
}

// ---------- Microphone helpers ----------

void readMicrophone() {
  size_t bytes = mic.readBytes((char*)samples, sizeof(samples));
  int n = bytes / sizeof(int32_t);
  if (n == 0) return;

  // INMP441 sends 24-bit samples inside 32-bit numbers; remove any steady offset.
  double mean = 0;
  bool allZero = true;
  for (int i = 0; i < n; i++) {
    samples[i] >>= 8;
    if (samples[i] != 0) allZero = false;
    mean += samples[i];
  }
  mean /= n;
  for (int i = 0; i < n; i++) {
    double v = samples[i] - mean;
    sumSquares += v * v;
  }
  sampleCount += n;
  totalBlocks++;
  if (allZero) zeroBlocks++;
}

float currentDb() {
  double rms = sqrt(sumSquares / sampleCount);
  if (rms < 1) rms = 1;
  // INMP441: a 94 dB sound reads 26 dB below full scale, so dB = level + 120.
  return 20.0 * log10(rms / 8388607.0) + 120.0 + CALIBRATION_OFFSET;
}

void addToHistory(float db) {
  history[historyNext] = db;
  historyNext = (historyNext + 1) % AVERAGE_SECONDS;
  if (historyCount < AVERAGE_SECONDS) historyCount++;
}

float averageDb() {
  if (historyCount == 0) return 0;
  double total = 0;
  for (int i = 0; i < historyCount; i++) total += history[i];
  return total / historyCount;
}

const char* levelName(float db) {
  if (db >= LOUD_FROM) return "LOUD";
  if (db >= QUIET_BELOW) return "MODERATE";
  return "QUIET";
}

// ---------- ThingSpeak ----------

bool sendToThingSpeak(float value) {
  HTTPClient http;
  String url = String("http://api.thingspeak.com/update?api_key=") + WRITE_API_KEY +
               "&field1=" + String(value, 1);
  http.setTimeout(8000);
  http.begin(url);
  int code = http.GET();
  String reply = http.getString();   // ThingSpeak replies with the entry number, or 0 if rejected
  http.end();
  return code == 200 && reply.toInt() > 0;
}
