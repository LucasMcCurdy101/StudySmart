/*
  StudySmart - noise sensor (Step 5)
  Somers Hall, Group Study Room 312  ->  ThingSpeak channel 3577145

  What it does:
    - Measures the room's noise level once a second (INMP441 microphone)
    - Keeps a rolling average of the last 5 minutes
    - Every 30 seconds, sends that 5-minute average to ThingSpeak (Field 1)
    - Only measures loudness. It never records or sends audio.

  Wiring (INMP441 -> ESP32):
    VDD -> 3V3        GND -> GND        L/R -> GND
    SCK -> GPIO 32    WS  -> GPIO 25    SD  -> GPIO 33

  Blue light on the board:
    blinking = connecting to Wi-Fi
    on       = connected
    off      = not connected (it keeps measuring and retries)

  Serial Monitor: 115200 baud
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_I2S.h>

// =====================  FILL THESE IN  =====================
// Never upload this file to GitHub with your real details in it.
const char* WIFI_NAME     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* WRITE_API_KEY = "YOUR_WRITE_API_KEY";   // ThingSpeak > API Keys > Write API Key
// ===========================================================

// ---------- Settings ----------
float CALIBRATION_OFFSET = -30.0;            // from Step 3
const int QUIET_BELOW    = 45;               // under this = Quiet
const int LOUD_FROM      = 60;               // this or higher = Loud
const int AVERAGE_SECONDS          = 300;    // average over the last 5 minutes
const unsigned long SEND_EVERY_MS  = 30000;  // send every 30 seconds (ThingSpeak minimum is 15)

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
int historyCount = 0;     // how many slots are filled (max 300)
int historyNext  = 0;     // where the next reading goes

unsigned long lastSend = 0;
unsigned long lastWifiTry = 0;

// ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  Serial.println();
  Serial.println("StudySmart noise sensor - Room 312");

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

  // Wi-Fi
  connectWifi();

  lastSecond = millis();
  lastSend = millis();
}

void loop() {
  readMicrophone();

  // Once a second: turn the sound into a dB reading and add it to the history
  if (millis() - lastSecond >= 1000) {
    if (zeroBlocks == totalBlocks) {
      Serial.println("No sound data from the microphone. Check SD -> 33, L/R -> GND and VDD -> 3V3.");
    } else {
      float db = currentDb();
      addToHistory(db);
      Serial.printf("Now: %5.1f dB  %-8s   5-min average: %5.1f dB (%d s of data)\n",
                    db, levelName(db), averageDb(), historyCount);
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
                      avg, levelName(avg), ok ? "OK" : "FAILED");
      } else {
        Serial.println(">> Not connected to Wi-Fi, reading not sent.");
      }
    }
  }

  // If Wi-Fi drops, try again every 30 seconds without stopping the measurements
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);
    if (millis() - lastWifiTry >= 30000) {
      lastWifiTry = millis();
      Serial.println("Wi-Fi lost, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
    }
  } else {
    digitalWrite(LED_PIN, HIGH);
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

// ---------- Wi-Fi + ThingSpeak ----------

void connectWifi() {
  Serial.printf("Connecting to Wi-Fi \"%s\"", WIFI_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));   // blink while connecting
    Serial.print(".");
    delay(250);
  }
  lastWifiTry = millis();
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("\nConnected! IP address: %s\n", WiFi.localIP().toString().c_str());
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("\nCould not connect yet. Check the Wi-Fi name and password. Will keep trying.");
  }
}

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
