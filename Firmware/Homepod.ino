#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "ESP_I2S.h"

// Fill these in for your network and API.
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
const char* API_URL = "http://192.168.1.10:8000/voice";
const char* API_TOKEN = "YOUR_API_TOKEN";

constexpr int PIN_TOUCH_CS = 14;
constexpr int PIN_TOUCH_IRQ = 7;
constexpr int PIN_PTT = 16;
constexpr int PIN_BUTTON_2 = 17;

constexpr int PIN_I2S_BCLK = 4;
constexpr int PIN_I2S_WS = 5;
constexpr int PIN_I2S_DATA = 6;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr size_t RECORD_SECONDS = 3;
constexpr size_t SAMPLE_COUNT = SAMPLE_RATE * RECORD_SECONDS;

TFT_eSPI display;
XPT2046_Touchscreen touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
I2SClass i2s;

int16_t pcm[SAMPLE_COUNT];

void showStatus(const char* line1, const char* line2 = "") {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.setCursor(12, 18);
  display.println(line1);
  display.setCursor(12, 52);
  display.println(line2);
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showStatus("Connecting Wi-Fi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool recordAudio() {
  showStatus("Listening...", "Speak now");
  size_t written = 0;

  while (written < SAMPLE_COUNT) {
    int32_t sample32[128];
    size_t bytesRead = i2s.readBytes(
      reinterpret_cast<char*>(sample32),
      sizeof(sample32)
    );

    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead && written < SAMPLE_COUNT; i++) {
      // INMP441 provides 24-bit audio in a 32-bit I²S slot.
      pcm[written++] = static_cast<int16_t>(sample32[i] >> 14);
    }
  }
  return true;
}

void sendAudio() {
  if (WiFi.status() != WL_CONNECTED) {
    showStatus("Wi-Fi disconnected");
    return;
  }

  showStatus("Sending audio...");
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Audio-Format", "pcm_s16le;rate=16000;channels=1");
  http.addHeader("Authorization", String("Bearer ") + API_TOKEN);

  int status = http.POST(
    reinterpret_cast<uint8_t*>(pcm),
    sizeof(pcm)
  );

  if (status > 0) {
    String response = http.getString();
    showStatus("API response:", response.c_str());
  } else {
    showStatus("API request failed", http.errorToString(status).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);

  display.init();
  display.setRotation(1);
  touch.begin();
  touch.setRotation(1);

  // Set the I²S pins: BCLK, WS, data out (unused), data in.
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, -1, PIN_I2S_DATA);

  // The mic sends 24-bit samples in 32-bit slots.
  if (!i2s.begin(
        I2S_MODE_STD,
        SAMPLE_RATE,
        I2S_DATA_BIT_WIDTH_32BIT,
        I2S_SLOT_MODE_MONO,
        I2S_STD_SLOT_LEFT)) {
    showStatus("I2S microphone error");
    while (true) delay(1000);
  }

  if (!connectWiFi()) {
    showStatus("Wi-Fi connection failed");
  } else {
    showStatus("Ready", "Hold button to talk");
  }
}

void loop() {
  if (digitalRead(PIN_PTT) == LOW) {
    recordAudio();
    sendAudio();
    delay(500);
    showStatus("Ready", "Hold button to talk");
  }

  // Button 2 and touchscreen can be assigned to API actions next.
  delay(20);
}
