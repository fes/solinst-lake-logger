#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <gdeq/GxEPD2_426_GDEQ0426T82.h>

namespace {

constexpr int EPAPER_CS_PIN = 10;
constexpr int EPAPER_DC_PIN = 9;
constexpr int EPAPER_RST_PIN = 8;
constexpr int EPAPER_BUSY_PIN = 7;
constexpr int EPAPER_POWER_PIN = 6;
constexpr uint16_t PAGE_HEIGHT = 40;
constexpr uint16_t EPAPER_BLACK = GxEPD_WHITE;
constexpr uint16_t EPAPER_WHITE = GxEPD_BLACK;

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, PAGE_HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(
        EPAPER_CS_PIN, EPAPER_DC_PIN, EPAPER_RST_PIN, EPAPER_BUSY_PIN));

void reportBusy(const char* phase) {
  Serial.print(phase);
  Serial.print(" BUSY=");
  Serial.println(digitalRead(EPAPER_BUSY_PIN));
}

void fill(uint16_t color, const char* phase) {
  Serial.println(phase);
  reportBusy("before");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(color);
  } while (display.nextPage());
  reportBusy("after");
}

void drawPattern() {
  Serial.println("Drawing final bordered test pattern");
  reportBusy("before");
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(EPAPER_WHITE);
    display.drawRect(
        4, 4, display.width() - 8, display.height() - 8, EPAPER_BLACK);
    display.drawLine(
        0, 0, display.width() - 1, display.height() - 1, EPAPER_BLACK);
    display.drawLine(
        display.width() - 1, 0, 0, display.height() - 1, EPAPER_BLACK);
    display.setTextColor(EPAPER_BLACK);
    display.setTextSize(4);
    display.setCursor(210, 245);
    display.print("GIGA EPAPER TEST");
  } while (display.nextPage());
  reportBusy("after");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStartedMs = millis();
  while (!Serial && millis() - serialWaitStartedMs < 5000U) delay(10);

  digitalWrite(EPAPER_POWER_PIN, HIGH);
  pinMode(EPAPER_POWER_PIN, OUTPUT);
  digitalWrite(EPAPER_CS_PIN, HIGH);
  pinMode(EPAPER_CS_PIN, OUTPUT);
  pinMode(EPAPER_BUSY_PIN, INPUT_PULLDOWN);

  Serial.println("GIGA GDEQ0426T82 panel-only HIL");
  reportBusy("power-on");
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 10, false);
  display.setRotation(0);
  display.setTextWrap(false);
  reportBusy("initialized");

  fill(EPAPER_BLACK, "Filling panel black");
  delay(3000);
  fill(EPAPER_WHITE, "Filling panel white");
  delay(3000);
  drawPattern();
  display.powerOff();
  digitalWrite(EPAPER_POWER_PIN, LOW);
  Serial.println("EPAPER HIL COMPLETE");
}

void loop() {
  delay(1000);
}
