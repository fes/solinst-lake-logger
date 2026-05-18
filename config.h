#pragma once

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_INA228.h>
#include <U8g2lib.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#if __has_include("secrets_local.h")
  #include "secrets_local.h"
#elif defined(ALLOW_PLACEHOLDER_SECRETS)
  #include "secrets_example.h"
#else
  #error "Missing secrets_local.h. Copy secrets_example.h to secrets_local.h and fill in real values. Define ALLOW_PLACEHOLDER_SECRETS only for non-production placeholder builds."
#endif

// ============================================================
// Compile-time configuration
//
// Recommended workflow:
//   1. Copy secrets_example.h to secrets_local.h
//   2. Fill in real local values in secrets_local.h
//   3. Keep secrets_local.h out of git
//
// Set ALLOW_PLACEHOLDER_SECRETS only when you intentionally want a
// placeholder/non-production build.
// ============================================================
char WIFI_SSID[64] = WIFI_SSID_VALUE;
char WIFI_PASS[64] = WIFI_PASS_VALUE;
char DEVICE_ID[64] = DEVICE_ID_VALUE;
char SHARED_SECRET[128] = SHARED_SECRET_VALUE;
char POST_DEPLOYMENT_ID[128] = DEPLOYMENT_ID_VALUE;

// Google Apps Script Web App:
// https://script.google.com/macros/s/DEPLOYMENT_ID/exec
const char* POST_HOST = "script.google.com";
const int   POST_PORT = 443;
const char* POST_PATH_PREFIX = "/macros/s/";
const char* POST_PATH_SUFFIX = "/exec";
char POST_PATH[256] = "";

// Local HTTP server port
constexpr uint16_t HTTP_PORT = 80;

// Timing
constexpr unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr int LOG_INTERVAL_MINUTES = 15;
constexpr int LOG_BOUNDARY_WINDOW_SECONDS = 5;
constexpr unsigned long DISPLAY_ON_SECONDS_DEFAULT = 15UL;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 500UL;

// Solinst defaults
constexpr uint32_t WLTS_BAUD = 19200;
constexpr auto     WLTS_SERIAL_CFG = SERIAL_8E1;
constexpr bool     WLTS_USE_FIXED_MODBUS_ID = true;
constexpr uint8_t  WLTS_FIXED_MODBUS_ID = 1;

// Opta RS-485 timing discovered during Solinst 301 bring-up. The Opta RS485
// object echoes TX bytes into RX, and the 301 response may arrive after that
// echo. probe_modbus.ino forces receive mode, parses past TX echo, and keeps
// waiting up to this timeout for a valid response frame.
constexpr unsigned long WLTS_RS485_PRE_DELAY_US  = 1000UL;
constexpr unsigned long WLTS_RS485_POST_DELAY_US = 1000UL;
constexpr unsigned long WLTS_RESPONSE_TIMEOUT_MS = 1500UL;

// Startup Modbus scan range used only when WLTS_USE_FIXED_MODBUS_ID is false.
constexpr uint8_t SCAN_START_ID = 1;
constexpr uint8_t SCAN_END_ID   = 10;

// Retry settings
constexpr int READ_RETRIES = 4;
constexpr unsigned long INITIAL_BACKOFF_MS = 250;
constexpr unsigned long MAX_BACKOFF_MS     = 4000;

constexpr int POST_RETRIES = 3;
constexpr unsigned long POST_RETRY_DELAY_MS = 2000;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_INITIAL_MS = 30000UL;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_MAX_MS = 15UL * 60UL * 1000UL;

// Solinst 301 input registers
constexpr uint16_t REG_FW_VERSION = 0x0000;
constexpr uint16_t REG_FW_BETA    = 0x0001;
constexpr uint16_t REG_SERIAL_HI  = 0x0004;
constexpr uint16_t REG_SERIAL_LO  = 0x0005;
constexpr uint16_t REG_LEVEL_HI   = 0x0006;
constexpr uint16_t REG_LEVEL_LO   = 0x0007;
constexpr uint16_t REG_TEMP_HI    = 0x0008;
constexpr uint16_t REG_TEMP_LO    = 0x0009;

// INA228 power monitor defaults
constexpr uint8_t INA228_BATTERY_OUTPUT_ADDR = 0x40;
constexpr uint8_t INA228_SOLAR_INPUT_ADDR    = 0x41;
constexpr float   INA228_SHUNT_OHMS          = 0.015f;
constexpr float   INA228_MAX_CURRENT_AMPS    = 10.0f;

// SSD1309 display defaults (2.42 inch 128x64, I2C)
constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

const char* LEVEL_UNITS = "m";
const char* TEMP_UNITS  = "C";

// ============================================================
// Types
// ============================================================
struct SensorIdentity {
  uint32_t serialNumber = 0;
  uint8_t fwMajor = 0;
  uint8_t fwMinor = 0;
  uint16_t fwBeta = 0;
};

struct PowerMonitorSnapshot {
  bool present = false;
  bool valid = false;
  float busVoltageV = NAN;
  float currentA = NAN;
  float powerW = NAN;
};

struct ProbeReading {
  String timestampUtc;
  float level = NAN;
  float temperature = NAN;
  bool valid = false;
  PowerMonitorSnapshot batteryOutput;
  PowerMonitorSnapshot solarInput;
};

// ============================================================
// Globals
// ============================================================
WiFiServer server(HTTP_PORT);
WiFiSSLClient wifiSslClient;
HttpClient httpClient(wifiSslClient, POST_HOST, POST_PORT);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 3600000);

Adafruit_INA228 batteryOutputMonitor;
Adafruit_INA228 solarInputMonitor;
bool batteryOutputMonitorPresent = false;
bool solarInputMonitorPresent = false;
String powerMonitorInitStatus = "not initialized";

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
bool displayPresent = false;
bool displayAwake = false;
unsigned long displayOnSeconds = DISPLAY_ON_SECONDS_DEFAULT;
unsigned long displayWakeUntilMs = 0;

constexpr size_t BACKLOG_CAPACITY = 32;
ProbeReading backlog[BACKLOG_CAPACITY];
size_t backlogCount = 0;

uint8_t detectedSensorId = 0;
SensorIdentity detectedIdentity;

ProbeReading lastProbeReading;
bool clockValid = false;

unsigned long bootMs = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long lastSuccessfulProbeReadMs = 0;
unsigned long lastSuccessfulUploadMs = 0;
unsigned long lastProbeAttemptMs = 0;
unsigned long lastUploadAttemptMs = 0;
unsigned long nextUploadAllowedMs = 0;

String lastSuccessfulProbeReadUtc = "";
String lastSuccessfulUploadUtc = "";
String lastUploadError = "";
String lastUploadErrorUtc = "";

uint32_t successfulProbeReads = 0;
uint32_t failedProbeReads = 0;
uint32_t successfulUploads = 0;
uint32_t failedUploads = 0;
uint32_t droppedBacklogEntries = 0;
uint32_t consecutiveUploadFailures = 0;

// Boundary logging state
int lastLoggedTmYear = -1;
int lastLoggedTmYDay = -1;
int lastLoggedTmHour = -1;
int lastLoggedTmMin  = -1;

// Compile-time config summary state
String configLoadStatus = "not attempted";
String configSource = "compiled secrets";

// ============================================================
// Prototypes
// ============================================================
float regsToFloat(uint16_t hi, uint16_t lo);
uint32_t regsToUint32(uint16_t hi, uint16_t lo);
bool isLeapYear(int year);
String epochToIso8601UTC(unsigned long epoch);
String jsonEscape(const String& s);
String millisAgeString(unsigned long sinceMs);

bool enqueueReading(const ProbeReading& r);
bool dequeueReading(ProbeReading& out);
bool peekBacklog(ProbeReading& out);

bool connectWiFi();
bool syncClockFromNtp();
void maintainClockSync();
bool isClockValid();
String nowUtcString();
bool shouldLogNow();

bool initPowerMonitors();
void readPowerMonitors(ProbeReading &reading);
void printPowerMonitorSummary();

float approximateBatteryChargePercent(const ProbeReading &reading);
bool solarChargingBatteryNow(const ProbeReading &reading);
void appendPowerMonitorJson(String &body, const char *prefix, const PowerMonitorSnapshot &snapshot);

bool readInputRegister(uint8_t slaveId, uint16_t reg, uint16_t &value);
bool readInputRegisterWithRetry(uint8_t slaveId, uint16_t reg, uint16_t &value);
bool readRegisterPairWithRetry(uint8_t slaveId, uint16_t regHi, uint16_t regLo, uint16_t &hi, uint16_t &lo);
bool readSensorIdentity(uint8_t slaveId, SensorIdentity &id);
bool identityLooksValid(const SensorIdentity &id);
bool scanForSensor(uint8_t startId, uint8_t endId, uint8_t &foundId, SensorIdentity &foundIdentity);
bool readLevelAndTemperature(uint8_t slaveId, ProbeReading &reading);
bool probeNow(ProbeReading &reading);
void printIdentity(uint8_t slaveId, const SensorIdentity &id);

String makePayload(const ProbeReading &r);
bool postJson(const String &payload);
bool postReadingWithRetry(const ProbeReading &r);
void flushBacklogOnce();
bool performProbeAndUpload(const char *reason);
void recordUploadFailure(const String &reason);
void clearUploadFailureState();

void sendHttpJson(WiFiClient &client, int statusCode, const String &body);
String probeJson(const ProbeReading &r);
String statusJson();
void handleHttpClient();

void buildPostPath();
bool loadRuntimeConfig();
void printRuntimeConfigSummary();

void initUserInterface();
void updateUserInterface();
void handleUserButton();

bool initDisplay();
void wakeDisplayForTimeout();
void updateDisplay();
void sleepDisplay();
