/*
  WOKWI AI ANSWER MACHINE
  ESP32 + ILI9341 color TFT + XPT2046 touch controller

  IMPORTANT:
  - Add your Wi-Fi credentials and API key below.
  - This calls an OpenAI-compatible Chat Completions endpoint.
  - Keep your API key private; do not publish/share this project with a real key.
  - If API_KEY is blank, the screen demonstrates the UI without calling an AI.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ---------- WIFI + AI SETTINGS ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* API_KEY = "";  // Paste your API key here; keep it private.
const char* AI_ENDPOINT = "https://api.openai.com/v1/chat/completions";
const char* AI_MODEL = "gpt-4o-mini";

// ---------- DISPLAY PINS ----------
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TOUCH_CS 5
#define TOUCH_IRQ 21

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

const uint16_t BG       = 0x0861;
const uint16_t PANEL    = 0x10A3;
const uint16_t CYAN     = 0x07FF;
const uint16_t WHITE    = 0xFFFF;
const uint16_t MUTED    = 0xBDF7;
const uint16_t GREEN    = 0x07E0;
const uint16_t ORANGE   = 0xFD20;
const uint16_t RED      = 0xF800;

String question = "";
String answer = "Hello! Tap the button and enter your question.";
String statusLine = "ready";
bool busy = false;
unsigned long lastTouch = 0;

const char* keyRows[] = {
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM"
};

void drawButton(int x, int y, int w, int h, const String &label,
                uint16_t fill, uint16_t ink = WHITE) {
  tft.fillRoundRect(x, y, w, h, 4, fill);
  tft.drawRoundRect(x, y, w, h, 4, CYAN);
  tft.setTextSize(1);
  tft.setTextColor(ink);
  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor(x + (w - bw) / 2, y + (h - bh) / 2);
  tft.print(label);
}

void drawWrappedText(const String &text, int x, int y, int maxWidth,
                     int maxLines, uint16_t color, uint8_t size = 1) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int charsPerLine = maxWidth / (6 * size);
  if (charsPerLine < 1) charsPerLine = 1;
  int line = 0;
  int start = 0;
  while (start < (int)text.length() && line < maxLines) {
    int end = min(start + charsPerLine, (int)text.length());
    if (end < (int)text.length()) {
      int space = text.lastIndexOf(' ', end);
      if (space >= start) end = space;
    }
    if (end <= start) end = min(start + charsPerLine, (int)text.length());
    tft.setCursor(x, y + line * (8 * size + 2));
    tft.print(text.substring(start, end));
    start = end;
    while (start < (int)text.length() && text[start] == ' ') start++;
    line++;
  }
  if (start < (int)text.length() && maxLines > 0) {
    tft.setCursor(x + maxWidth - 12, y + (maxLines - 1) * (8 * size + 2));
    tft.print("...");
  }
}

void redraw() {
  tft.fillScreen(BG);
  tft.fillRect(0, 0, 320, 27, 0x0194);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(8, 6);
  tft.print("AI MINI");
  tft.setTextSize(1);
  tft.setTextColor(CYAN);
  tft.setCursor(224, 10);
  tft.print("ESP32 / TOUCH");

  tft.fillRoundRect(5, 31, 310, 29, 5, PANEL);
  tft.drawRoundRect(5, 31, 310, 29, 5, CYAN);
  tft.setTextSize(1);
  tft.setTextColor(MUTED);
  tft.setCursor(10, 35);
  tft.print("CAU HOI:");
  tft.setTextColor(WHITE);
  String shown = question;
  if (shown.length() > 44) shown = shown.substring(shown.length() - 44);
  tft.setCursor(10, 47);
  tft.print(shown.length() ? shown : "Cham phim ben duoi de nhap...");

  tft.fillRoundRect(5, 64, 310, 75, 5, 0x0000);
  tft.drawRoundRect(5, 64, 310, 75, 5, 0x4D13);
  tft.setTextColor(GREEN);
  tft.setCursor(10, 68);
  tft.print("TRA LOI AI");
  drawWrappedText(answer, 10, 81, 296, 5, WHITE, 1);

  int y1 = 143;
  for (int i = 0; i < 10; i++) {
    int x = 5 + i * 31;
    drawButton(x, y1, 28, 20, String(keyRows[0][i]), 0x2128);
  }
  for (int i = 0; i < 9; i++) {
    int x = 20 + i * 31;
    drawButton(x, 166, 28, 20, String(keyRows[1][i]), 0x2128);
  }
  for (int i = 0; i < 7; i++) {
    int x = 38 + i * 35;
    drawButton(x, 189, 31, 20, String(keyRows[2][i]), 0x2128);
  }

  drawButton(5, 213, 48, 23, "SPACE", 0x3186);
  drawButton(57, 213, 48, 23, "DEL", 0xA145);
  drawButton(109, 213, 48, 23, "CLEAR", 0xA145);
  drawButton(161, 213, 154, 23, busy ? "REPLYING..." : "SEND A QUESTION  >", 0x03E0, 0x0000);

  tft.fillRect(0, 238, 320, 2, BG);
}

void showStatus(const String &s, uint16_t color = ORANGE) {
  statusLine = s;
  Serial.println("[AI] " + s);
  tft.fillRect(0, 238, 320, 2, color);
}

String demoAnswer(const String &q) {
  if (q.length() == 0) return "You haven't entered a question yet. Please tap the buttons to enter!";
  return "THIS LACHED BY DEMO. Luckily, I asked: \"" + q +
         "\". Or add Wi-Fi and API key to Sketch.ino to receive that AI query.";
}

String askAI(const String &q) {
  if (String(API_KEY).length() < 10) {
    return demoAnswer(q);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startWait = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWait < 12000) {
      delay(250);
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    return "Unable to connect to Wi-Fi. Please check WIFI_SSID and WIFI_PASSWORD.";
  }

  WiFiClientSecure client;
  client.setInsecure(); // Demo convenience only; production should validate TLS certificates.
  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(client, AI_ENDPOINT)) {
    return "Unable to establish AI connection.";
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + API_KEY);

  JsonDocument doc;
  doc["model"] = AI_MODEL;
  doc["messages"][0]["role"] = "system";
  doc["messages"][0]["content"] =
      "You are a friendly AI assistant, responding clearly and helpfully in English. "
      "If you're not sure, please state that clearly.";
  doc["messages"][1]["role"] = "user";
  doc["messages"][1]["content"] = q;
  doc["max_tokens"] = 220;
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  String result;
  if (code > 0) {
    String payload = http.getString();
    if (code == 200) {
      JsonDocument response;
      DeserializationError err = deserializeJson(response, payload);
      if (!err) {
        result = response["choices"][0]["message"]["content"] | "";
      } else {
        result = "The AI responded but couldn't read the JSON data.";
      }
    } else {
      Serial.println(payload);
      result = "Error API HTTP " + String(code) +
               ". Check the API key, limit, and model name in Serial Monitor..";
    }
  } else {
    result = "Error Connect: " + http.errorToString(code);
  }
  http.end();
  result.replace("\r", " ");
  result.replace("\n", " ");
  if (result.length() == 0) result = "The AI did not send back the reply.";
  return result;
}

void sendQuestion() {
  if (busy) return;
  if (question.length() == 0) {
    answer = "Please enter your question before clicking Send.";
    redraw();
    return;
  }
  busy = true;
  answer = "Connecting to AI... Please wait.";
  redraw();
  showStatus("Submitting a question");
  answer = askAI(question);
  busy = false;
  redraw();
  showStatus("Received feedback", GREEN);
}

void handleKey(int x, int y) {
  if (y >= 143 && y <= 163) {
    int col = (x - 5) / 31;
    if (col >= 0 && col < 10) question += keyRows[0][col];
  } else if (y >= 166 && y <= 186) {
    int col = (x - 20) / 31;
    if (col >= 0 && col < 9) question += keyRows[1][col];
  } else if (y >= 189 && y <= 209) {
    int col = (x - 38) / 35;
    if (col >= 0 && col < 7) question += keyRows[2][col];
  } else if (y >= 213 && y <= 237) {
    if (x < 55) question += ' ';
    else if (x < 108) {
      if (question.length()) question.remove(question.length() - 1);
    } else if (x < 159) {
      question = "";
      answer = "Deleted. Please enter a new question!";
    } else {
      sendQuestion();
      return;
    }
  }
  if (question.length() > 120) question.remove(120);
  redraw();
}

void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23);
  tft.begin();
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(1);
  redraw();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  showStatus("UI ready - enter questions", GREEN);
  Serial.println("AI ANSWER MACHINE");
  Serial.println("Touch the virtual screen to type. API key is optional for UI demo.");
}

void loop() {
  if (ts.touched() && millis() - lastTouch > 180) {
    TS_Point p = ts.getPoint();
    lastTouch = millis();

    // Common calibration for landscape rotation. If touch coordinates are offset,
    // adjust the four calibration endpoints below.
    int x = map(p.y, 200, 3900, 0, 320);
    int y = map(p.x, 200, 3900, 240, 0);
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);
    Serial.printf("Touch: raw(%d,%d) -> (%d,%d)\n", p.x, p.y, x, y);
    handleKey(x, y);
  }
  delay(5);
}
