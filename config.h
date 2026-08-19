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

#include "board_profile.h"
#include "runtime_boundaries.h"

#if __has_include("secrets_local.h")
  #include "secrets_local.h"
#elif defined(ALLOW_PLACEHOLDER_SECRETS)
  #include "secrets_example.h"
#else
  #error "Missing secrets_local.h. Copy secrets_example.h to secrets_local.h and fill in real values. Define ALLOW_PLACEHOLDER_SECRETS only for non-production placeholder builds."
#endif

char WIFI_SSID[64] = WIFI_SSID_VALUE;
char WIFI_PASS[64] = WIFI_PASS_VALUE;
char DEVICE_ID[64] = DEVICE_ID_VALUE;
char SHARED_SECRET[128] = SHARED_SECRET_VALUE;
char POST_DEPLOYMENT_ID[128] = DEPLOYMENT_ID_VALUE;

enum class UploadEndpointMode : uint8_t {
  GOOGLE_APPS_SCRIPT,
  FESLABS_INGEST
};

// Keep the deployed Google Apps Script behavior as the safe default. Switch
// this one value after the fesLabs ingest service is deployed and tested.
constexpr UploadEndpointMode UPLOAD_ENDPOINT_MODE = UploadEndpointMode::FESLABS_INGEST;

const char* GOOGLE_APPS_SCRIPT_HOST = "script.google.com";
constexpr uint16_t GOOGLE_APPS_SCRIPT_PORT = 443;
const char* GOOGLE_APPS_SCRIPT_PATH_PREFIX = "/macros/s/";
const char* GOOGLE_APPS_SCRIPT_PATH_SUFFIX = "/exec";
char GOOGLE_APPS_SCRIPT_PATH[256] = "";

// Production Firebase Hosting route for the dedicated fesLabs ingest Function.
// Port 80/plain HTTP remains available for deliberate local testing only.
const char* FESLABS_INGEST_HOST = "feslabs.com";
const char* FESLABS_INGEST_PATH = "/api/lake/ingest";
constexpr uint16_t FESLABS_INGEST_PORT = 443;
constexpr bool FESLABS_INGEST_USE_HTTPS = true;

constexpr uint16_t HTTP_PORT = 80;
constexpr unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr int LOG_INTERVAL_MINUTES = 60;
constexpr int LOG_BOUNDARY_WINDOW_SECONDS = 5;
constexpr unsigned long DISPLAY_ON_SECONDS_DEFAULT = 15UL;
constexpr unsigned long DISPLAY_REFRESH_INTERVAL_MS = 500UL;

constexpr uint32_t WLTS_BAUD = 19200;
constexpr auto     WLTS_SERIAL_CFG = SERIAL_8E1;
constexpr bool     WLTS_USE_FIXED_MODBUS_ID = true;
constexpr uint8_t  WLTS_FIXED_MODBUS_ID = 1;
constexpr unsigned long WLTS_RS485_PRE_DELAY_US  = 1000UL;
// Extra driver-enable time after one complete UART character.
constexpr unsigned long WLTS_RS485_POST_DELAY_MARGIN_US = 500UL;
constexpr unsigned long WLTS_RESPONSE_TIMEOUT_MS = 1500UL;
constexpr uint8_t SCAN_START_ID = 1;
constexpr uint8_t SCAN_END_ID   = 10;

// Weather is intentionally excluded from the supported Opta profile. The
// driver remains available for the future Giga site logger, where it will use a
// dedicated second RS-485 channel.
constexpr bool WEATHER_SENSOR_ENABLED = ACTIVE_BOARD_PROFILE.weatherEnabled;
constexpr uint8_t WEATHER_MODBUS_ID = 2;
constexpr uint32_t WEATHER_BAUD = WLTS_BAUD;
// Retained driver setting: DFRobot documents 8N1 framing. The planned Giga
// target will apply it only to the weather station's dedicated channel.
constexpr auto WEATHER_SERIAL_CFG = SERIAL_8N1;
constexpr unsigned long WEATHER_SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;

constexpr int READ_RETRIES = 4;
constexpr unsigned long INITIAL_BACKOFF_MS = 250;
constexpr unsigned long MAX_BACKOFF_MS     = 4000;
constexpr int POST_RETRIES = 3;
constexpr unsigned long POST_RETRY_DELAY_MS = 2000;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_INITIAL_MS = 30000UL;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_MAX_MS = 15UL * 60UL * 1000UL;

constexpr uint16_t REG_FW_VERSION = 0x0000;
constexpr uint16_t REG_FW_BETA    = 0x0001;
constexpr uint16_t REG_SERIAL_HI  = 0x0004;
constexpr uint16_t REG_SERIAL_LO  = 0x0005;
constexpr uint16_t REG_LEVEL_HI   = 0x0006;
constexpr uint16_t REG_LEVEL_LO   = 0x0007;
constexpr uint16_t REG_TEMP_HI    = 0x0008;
constexpr uint16_t REG_TEMP_LO    = 0x0009;

constexpr uint8_t INA228_BATTERY_OUTPUT_ADDR = 0x40;
constexpr uint8_t INA228_SOLAR_INPUT_ADDR    = 0x41;
constexpr float   INA228_SHUNT_OHMS          = 0.015f;
constexpr float   INA228_MAX_CURRENT_AMPS    = 10.0f;

constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

const char* LEVEL_UNITS = "m";
const char* TEMP_UNITS  = "C";

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

struct WeatherSummary {
  uint16_t sampleCount = 0;
  String startUtc = "";
  String endUtc = "";
  float airTemperatureSum = 0;
  float airTemperatureMin = NAN;
  float airTemperatureMax = NAN;
  float relativeHumiditySum = 0;
  float relativeHumidityMin = NAN;
  float relativeHumidityMax = NAN;
  float barometricPressureSum = 0;
  float barometricPressureMin = NAN;
  float barometricPressureMax = NAN;
  float windSpeedSum = 0;
  float windSpeedMin = NAN;
  float windSpeedMax = NAN;
  float lightSum = 0;
  float lightMin = NAN;
  float lightMax = NAN;
  float windDirectionSinSum = 0;
  float windDirectionCosSum = 0;
  float rainfallFirstMm = NAN;
  float rainfallLastMm = NAN;
  bool rainfallCounterReset = false;
};

struct WeatherReading {
  bool enabled = WEATHER_SENSOR_ENABLED;
  bool present = false;
  bool valid = false;
  uint8_t modbusId = WEATHER_MODBUS_ID;
  String readUtc = "";
  float airTemperatureC = NAN;
  float relativeHumidityPct = NAN;
  float barometricPressureHpa = NAN;
  float windSpeedMs = NAN;
  float windDirectionDeg = NAN;
  float rainfallAccumulatedMm = NAN;
  float lightLux = NAN;
  String lastError = "";
  WeatherSummary summary;
};

struct ProbeReading {
  String timestampUtc;
  float level = NAN;
  float temperature = NAN;
  bool valid = false;
  PowerMonitorSnapshot batteryOutput;
  PowerMonitorSnapshot solarInput;
  WeatherReading weather;
};

WiFiServer server(HTTP_PORT);
WiFiClient wifiClient;
WiFiSSLClient wifiSslClient;
HttpClient googleAppsScriptHttpClient(wifiSslClient, GOOGLE_APPS_SCRIPT_HOST, GOOGLE_APPS_SCRIPT_PORT);
HttpClient fesLabsHttpsClient(wifiSslClient, FESLABS_INGEST_HOST, FESLABS_INGEST_PORT);
HttpClient fesLabsHttpClient(wifiClient, FESLABS_INGEST_HOST, FESLABS_INGEST_PORT);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 3600000);

Adafruit_INA228 batteryOutputMonitor;
Adafruit_INA228 solarInputMonitor;
bool batteryOutputMonitorPresent = false;
bool solarInputMonitorPresent = false;
String powerMonitorInitStatus = "not initialized";
unsigned long lastSuccessfulBatteryOutputReadMs = 0;
unsigned long lastSuccessfulSolarInputReadMs = 0;
String lastSuccessfulBatteryOutputReadUtc = "";
String lastSuccessfulSolarInputReadUtc = "";

bool weatherSensorPresent = false;
bool weatherSensorValid = false;
String weatherSensorInitStatus = "not initialized";
unsigned long lastWeatherAttemptMs = 0;
unsigned long lastSuccessfulWeatherReadMs = 0;
String lastSuccessfulWeatherReadUtc = "";
String lastWeatherError = "";
String lastWeatherErrorUtc = "";
WeatherReading lastWeatherReading;
WeatherSummary weatherSummary;

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
bool displayPresent = false;
bool displayAwake = false;
unsigned long displayOnSeconds = DISPLAY_ON_SECONDS_DEFAULT;
unsigned long displayWakeUntilMs = 0;
bool userButtonPresent = false;
unsigned long lastUserButtonPressMs = 0;
String lastUserButtonPressUtc = "";

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

int lastLoggedTmYear = -1;
int lastLoggedTmYDay = -1;
int lastLoggedTmHour = -1;
int lastLoggedTmMin  = -1;

String configLoadStatus = "not attempted";
String configSource = "compiled secrets";

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

void initWeatherSensor();
bool readWeatherNow(WeatherReading &weather);
void readWeatherForReading(ProbeReading &reading);
void pollWeatherIfDue(bool force = false);
void resetWeatherSummaryForNextInterval();
void appendWeatherJson(String &body, const char *prefix, const WeatherReading &weather);
void beginSolinstRs485();
void beginWeatherRs485();

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
const char* uploadEndpointModeName();
const char* uploadEndpointHost();
const char* uploadEndpointPath();
uint16_t uploadEndpointPort();
bool uploadEndpointUsesHttps();
HttpClient& uploadHttpClient();
bool isSuccessfulUploadResponse(int statusCode);
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
String lastDisplayWakeRequestUtc();
String lastDisplayRefreshUtc();
String lastDisplayWakeRequestAge();
String lastDisplayRefreshAge();
uint32_t displayRefreshCount();
uint32_t displayI2cRecoveryCount();
