#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#define LGFX_USE_V1

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <LovyanGFX.hpp>
#include <SPI.h>
#include "secret/credentials.h"

// Pins used by the KY-040 Encoder and the Display
#define ENCODER_CLK 27
#define ENCODER_DT  32
#define ENCODER_SW  33

// Pins used by the SPI Display 1.8" 128x160 (ST7735)
#define TFT_SCK  18   // SCK
#define TFT_SDA  23   // SDA/MOSI
#define TFT_MISO 19   // MISO
#define TFT_A0   16   // A0 / DC
#define TFT_RST  04   // RESET
#define TFT_CS   17   // Display CS
#define TFT_LED  25   // LED / backlight (PWM controlled)

// Other definitions
#define max_retries      3 // max times to try to connect to an SSID
#define conn_timeout   500 // time-out for connecting to peripherals (in ms)
#define BUFFER_LIMIT 40000 // Image buffer size

class LGFX_ESP32_Shield : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX_ESP32_Shield() {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 27000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = TFT_SCK;
      cfg.pin_mosi = TFT_SDA;
      cfg.pin_miso = TFT_MISO;
      cfg.pin_dc   = TFT_A0;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = TFT_CS;
      cfg.pin_rst  = TFT_RST;
      cfg.pin_busy = -1;

      cfg.panel_width  = 128;
      cfg.panel_height = 160;
      cfg.offset_x = 0;   // Some ST7735 Chinese modules require offset_x/offset_y
      cfg.offset_y = 0;   // (e.g.: 2 and 1) -- adjust it if the image appears cropped

      cfg.bus_shared = true; // Change to true if you intend to use the SD Card on the display

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = TFT_LED;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

LGFX_ESP32_Shield tft; 

// Network/Wifi credentials (from secret/credendials.h)
#define WIFI_SSID     WIFI_SSID1
#define WIFI_PASSWORD WIFI_PASSWORD1

// Firebase credentials (from secret/credendials.h)
#define API_KEY       MY_API_KEY
#define DATABASE_URL  MY_DATABASE_URL
#define USER_EMAIL    MY_USER_EMAIL
#define USER_PASS     MY_USER_PASS

// Base URL for item pictures (.jpg)
// Ex.: Firebase Hosting: "https://your-project.web.app/images/"
// This firmware concatenates IMAGES_BASE_URL + the item Part. No. as the filename of an item's photo.
#define IMAGES_BASE_URL "https://SEU-PROJETO.web.app/images/"

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASS);

// Firebase components
FirebaseApp database_app;

// Realtime Database Class Architecture (v2 stable)
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client); 

// In the v2 stable, the internal core class for RTDB ops is instantiated using RealtimeDatabase namespace
RealtimeDatabase rtdb; 
AsyncResult fb_result;

enum MenuState { MENU_CLASS, MENU_CATEGORY, MENU_SUBCATEGORY, MENU_ITEM, MENU_QTY_ADJUST };
MenuState currentState = MENU_CLASS;

String idClass = "", idCat = "", idSub = "", idItem = "";
String currentItemName = "", currentItemPict = "";
int currentInventory = 0;

std::vector<String> MenuEntries; 
std::vector<String> MenuValues; 
int selectedIndex = 0;
int retries = 0;

volatile bool encoderTurned = false;
volatile int turnDir = 0; 
int lastCLKState;
bool buttonPressed = false;

void IRAM_ATTR Read_Encoder() {
  int CLKStatus = digitalRead(ENCODER_CLK);
  if (CLKStatus != lastCLKState && CLKStatus == LOW) {
    if (digitalRead(ENCODER_DT) != CLKStatus) turnDir = 1;
    else turnDir = -1;
    encoderTurned = true;
  }
  lastCLKState = CLKStatus;
}

void setup() {
  Serial.begin(115200);
  
  // Encoder
  Serial.println ("Inicializing Encoder");
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT,  INPUT_PULLUP);
  pinMode(ENCODER_SW,  INPUT_PULLUP);

  lastCLKState = digitalRead(ENCODER_CLK);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), Read_Encoder, CHANGE);

  // Display
  Serial.println ("Inicializing Display");
  tft.init();
  tft.setRotation(1); 
  
  tft.fillScreen  (TFT_BLACK);
  tft.setCursor   (2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize (1);

  // Wifi
  retries = 0;
  Serial.printf("Conecting to Wi-Fi %s\n", WIFI_SSID);
  tft.printf("Conecting to Wi-Fi %s\n", WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long Auth_Start;
  
  Auth_Start = millis();
  while (WiFi.status() != WL_CONNECTED) { 
    if (millis() - Auth_Start > (conn_timeout * 40)) {
      Serial.printf("ERROR 01: Couldn't connect to Wi-Fi %s\n", WIFI_SSID);
      Serial.print("Device halted!\n");
      tft.printf("ERROR 01: Couldn't connect to Wi-Fi %s\n", WIFI_SSID);
      tft.print("Device halted\n");
      esp_deep_sleep_start(); 
    }
    delay(conn_timeout);
  }

  tft.printf("Connected to %s\n", WIFI_SSID);
  Serial.printf("Connected to %s\n", WIFI_SSID);

  ssl_client.setInsecure(); 
  
  // Stabled initialization and credentials association
  initializeApp(aClient, database_app, getAuth(user_auth));
  
  // Official URL database link
  database_app.getApp<RealtimeDatabase>(rtdb);
  rtdb.url(DATABASE_URL);

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(2, 2);
  tft.print("Performing Firebase authentication...\n");
  Serial.print("Performing Firebase authentication...\n");
  
  Auth_Start = millis();
  while (!database_app.ready()) {
    database_app.loop();
    if (millis() - Auth_Start > (conn_timeout * 40)) {
      tft.print("ERROR 02: Firebase authentication timed-out!\n");
      tft.println("Device halted\n");
      Serial.println("ERROR 02: Firebase authentication timed-out");
      Serial.println("Device halted\n");
      break;
    }
    delay(conn_timeout);
  }

  tft.print("loading data...\n");
  Serial.print("loading data...\n");
  loadMenuData("/class");
}

void loop() {
  database_app.loop();

  if (encoderTurned) {
    encoderTurned = false; 
    if (currentState == MENU_QTY_ADJUST) {
      if (turnDir == 1) currentInventory++;
      else if (turnDir == -1 && currentInventory > 0) currentInventory--;
    } else {
      if (turnDir == 1) { 
        if (selectedIndex < (int)MenuEntries.size() - 1) selectedIndex++;
        else selectedIndex = 0;
      } else if (turnDir == -1) { 
        if (selectedIndex > 0) selectedIndex--;
        else selectedIndex = (int)MenuEntries.size() - 1;
      }
    }
    Update_Display();
  }

  if (digitalRead(ENCODER_SW) == LOW && !buttonPressed) {
    buttonPressed = true;
    delay(conn_timeout/10); 
    if (digitalRead(ENCODER_SW) == LOW) tratarConfirmacao();
  }
  if (digitalRead(ENCODER_SW) == HIGH) buttonPressed = false; 
}

void loadMenuData(String path) {
  tft.fillScreen(TFT_BLACK); 
  tft.setCursor(2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.print("Fetching data from the cloud...");
  Serial.print("Fetching data from the cloud...");

  MenuEntries.clear();
  MenuValues.clear();
  selectedIndex = 0;

  // Async fetching call
  rtdb.get(aClient, path, fb_result);

  if (!fb_result.isError()) {
    String jsonStr = fb_result.c_str(); 
    JsonDocument doc;
    deserializeJson(doc, jsonStr);
    JsonObject obj = doc.as<JsonObject>();

    for (JsonPair kv : obj) {
      MenuEntries.push_back(kv.key().c_str());
      MenuValues.push_back(kv.value().as<String>());
    }
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(2, 30);
    tft.printf("ERROR 03: Could fetch data from the cloud:\n%s\n", fb_result.error().message().c_str());
    delay(conn_timeout * 4);
  }
  Update_Display();
}

// Downloads a JPEG (item thumbnail) from IMAGES_BASE_URL and draws it on the display.
// Returns false if there's no photo, download fails or the file is bigger than the
// buffer size (IMG_BUFFER_LIMIT) -- in these cases, Update_Display()
// draws a void picture instead.
bool Fetch_n_Show_Item_Picture(const String& pictureFileName, int x, int y, int w, int h) {
  if (pictureFileName.length() == 0) return false;

  const size_t IMG_BUFFER_LIMIT = BUFFER_LIMIT; // 160x160 thumbnails are usually smaller than this
  String url = String(IMAGES_BASE_URL) + pictureFileName;

  HTTPClient http;
  bool ok = false;

  if (url.startsWith("https://")) {
    if (!http.begin(ssl_client, url)) return false;
  } else {
    if (!http.begin(url)) return false;
  }

  int HTTP_code = http.GET();
  if (HTTP_code == HTTP_CODE_OK) {
    int pictureSize = http.getSize();
    if (pictureSize > 0 && (size_t)pictureSize <= IMG_BUFFER_LIMIT) {
      uint8_t* buf = (uint8_t*)malloc(pictureSize);
      if (buf) {
        WiFiClient* stream = http.getStreamPtr();
        size_t bytes_read = 0;
        unsigned long time_Start = millis();
        while (bytes_read < (size_t)pictureSize && http.connected() && (millis() - time_Start) < conn_timeout * 10) {
          size_t stream_available = stream->available();
          if (stream_available) {
            int bytes_read_now = stream->readBytes(buf + bytes_read, min(stream_available, (size_t)(pictureSize - bytes_read)));
            bytes_read += bytes_read_now;
          } else {
            delay(1);
          }
        }
        if (bytes_read == (size_t)pictureSize) {
          tft.drawJpg(buf, pictureSize, x, y, w, h);
          ok = true;
        }
        free(buf);
      }
    }
  }
  http.end();
  return ok;
}

// Corta uma string para no máximo maxLen caracteres, adicionando ".." se cortar.
String truncar(const String& s, int maxLen) {
  if ((int)s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 2) + "..";
}

void Update_Display() {
  tft.fillScreen(TFT_BLACK);

  if (currentState == MENU_QTY_ADJUST) {
    // --- Item details page (160x128): item_pic_ (top-left), name (top-right), inventory (botton) ---
    const int item_pic_X = 2, item_pic_Y = 2, item_pic_W = 44, item_pic_H = 44;
    const int text_X = item_pic_X + item_pic_W + 4;
    const int text_W = 160 - text_X - 2;

    if (!Fetch_n_Show_Item_Picture(currentItemPict, item_pic_X, item_pic_Y, item_pic_W, item_pic_H)) {
      tft.drawRect(item_pic_X, item_pic_Y, item_pic_W, item_pic_H, TFT_DARKGREY);
      tft.setTextColor(TFT_DARKGREY);
      tft.setTextSize(1);
      tft.setCursor(item_pic_X + 4, item_pic_Y + 20);
      tft.print("no item_pic_");
    }

    // Item description - constrained, preserving the rest of the screen
    tft.setClipRect(text_X, item_pic_Y, text_W, item_pic_H);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(text_X, item_pic_Y);
    tft.setTextWrap(true, true);
    tft.print(currentItemName);
    tft.setTextWrap(false, false);
    tft.clearClipRect();

    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(1);
    tft.setCursor(2, item_pic_Y + item_pic_H + 4);
    tft.print("QTY:");

    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(2, item_pic_Y + item_pic_H + 14);
    tft.printf("< %d >", currentInventory);

    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(2, 118);
    tft.print("Press to confirm");
    return;
  }

  // Menu screens (class / category / subcategory / item), with scrolling
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.setTextColor(TFT_GREEN);

  if (currentState == MENU_CLASS) tft.println("CLASS:");
  else if (currentState == MENU_CATEGORY) tft.println("CATEGORY:");
  else if (currentState == MENU_SUBCATEGORY) tft.println("SUBCATEGORY:");
  else if (currentState == MENU_ITEM) tft.println("ITEM:");

  tft.setTextColor(TFT_WHITE);
  tft.println("----------------------------");

  const int Line_Height   = 10;
  const int Visible_Lines = 10; // lines that fit inside y=22 and y=118 (~96px / 10px)

  int window_begin = 0;
  if (selectedIndex >= Visible_Lines) window_begin = selectedIndex - Visible_Lines + 1;
  int window_end = min((int)MenuValues.size(), window_begin + Visible_Lines);

  int y = 22;
  for (int i = window_begin; i < window_end; i++) {
    tft.setCursor(2, y);
    if (i == selectedIndex) {
      tft.setTextColor(TFT_RED);
      tft.print(">");
    } else {
      tft.setTextColor(TFT_WHITE);
      tft.print(" ");
    }
    tft.println(truncar(MenuValues[i], 25));
    y += Line_Height;
  }
}

// Fetch /itens/{idItem} (item_name + item_pic) and /inventory/{idItem}/qty,
// and then switches to item details (MENU_QTY_ADJUST).
void carregarDetalheItem(String idItemSelecionado) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.print("Loading item...\n");
  Serial.print("Loading item...\n");

  idItem = idItemSelecionado;
  currentItemName = "";
  currentItemPict = "";
  currentInventory = 0;

  rtdb.get(aClient, "/itens/" + idItem, fb_result);
  if (!fb_result.isError()) {
    JsonDocument doc;
    deserializeJson(doc, fb_result.c_str());
    currentItemName = doc["item_name"].as<String>();
    currentItemPict = doc["item_pic_"].as<String>();
  } else {
    currentItemName = "(Error loading item)";
    Serial.printf("Error loading /itens/%s: %s\n", idItem.c_str(), fb_result.error().message().c_str());
  }

  rtdb.get(aClient, "/inventory/" + idItem + "/qty", fb_result);
  if (!fb_result.isError()) {
    currentInventory = String(fb_result.c_str()).toInt();
  } else {
    Serial.printf("Erro ao ler /inventory/%s/qty: %s\n", idItem.c_str(), fb_result.error().message().c_str());
  }

  currentState = MENU_QTY_ADJUST;
  Update_Display();
}

void tratarConfirmacao() {
  if (MenuEntries.size() == 0 && currentState != MENU_QTY_ADJUST) return;

  switch (currentState) {
    case MENU_CATEGORY:
      idCat = MenuEntries[selectedIndex];
      currentState = MENU_SUBCATEGORY;
      loadMenuData("/subcategories/" + idCat);
      break;

    case MENU_SUBCATEGORY:
      idSub = MenuEntries[selectedIndex];
      currentState = MENU_ITEM;
      loadMenuData("/itens_per_subcategory/" + idSub);
      break;

    case MENU_ITEM:
      carregarDetalheItem(MenuEntries[selectedIndex]);
      break;

    case MENU_QTY_ADJUST: {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(2, 50);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(1);
      tft.print("Sending changes\nto Firebase...");

      String pathSalvar = "/inventory/" + idItem + "/qty";

      // Updates use .set method treating payload as String
      rtdb.set(aClient, pathSalvar, String(currentInventory), fb_result);

      if (!fb_result.isError()) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(2, 55);
        tft.print("Inventory updated!");
        delay(1200);
      } else {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(2, 40);
        tft.printf("Error saving:\n%s", fb_result.error().message().c_str());
        delay(conn_timeout * 5);
      }

      currentState = MENU_CATEGORY;
      loadMenuData("/categories");
      break;
    }
  }
}
