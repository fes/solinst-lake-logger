#pragma once

#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_INA228.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "BlockDevice.h"
#include "MBRBlockDevice.h"
#include "LittleFileSystem.h"
#include "FATFileSystem.h"

// ============================================================
// User configuration defaults
//
// These values are used only as placeholders/fallbacks. At startup,
// loadRuntimeConfig() will try to read /user/config.ini from the Opta
// QSPI user-data partition and override them.
// ============================================================
char WIFI_SSID[64] = "YOUR_WIFI_SSID";
char WIFI_PASS[64] = "YOUR_WIFI_PASSWORD";
char DEVICE_ID[64] = "opta-well-01";
char SHARED_SECRET[128] = "PUT_A_LONG_RANDOM_SECRET_HERE";
char POST_DEPLOYMENT_ID[128] = "PUT_YOUR_DEPLOYMENT_ID_HERE";

// Google Apps Script Web App:
// https://script.google.com/macros/s/DEPLOYMENT_ID/exec
const char* POST_HOST = "script.google.com";
const int   POST_PORT = 443;
const char* POST_PATH_PREFIX = "/macros/s/";
const char* POST_PATH_SUFFIX = "/exec";
char POST_PATH[256] = "/macros/s/PUT_YOUR_DEPLOYMENT_ID_HERE/exec";

// Local HTTP server port
constexpr uint16_t HTTP_PORT = 80;

// Timing
constexpr unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr int LOG_INTERVAL_MINUTES = 15;
constexpr int LOG_BOUNDARY_WINDOW_SECONDS = 5;

// Solinst defaults
constexpr uint32_t WLTS_BAUD = 19200;
constexpr auto     WLTS_SERIAL_CFG = SERIAL_8E1;

// Startup Modbus scan range
constexpr uint8_t SCAN_START_ID = 1;
constexpr uint8_t SCAN_END_ID   = 10;

// Retry settings
constexpr int READ_RETRIES = 4;
constexpr unsigned long INITIAL_BACKOFF_MS = 250;
constexpr unsigned long MAX_BACKOFF_MS     = 4000;

constexpr int POST_RETRIES = 3;
constexpr unsigned long POST_RETRY_DELAY_MS = 2000;

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

String lastSuccessfulProbeReadUtc = "";
String lastSuccessfulUploadUtc = "";

uint32_t successfulProbeReads = 0;
uint32_t failedProbeReads = 0;
uint32_t successfulUploads = 0;
uint32_t failedUploads = 0;
uint32_t droppedBacklogEntries = 0;

// Boundary logging state
int lastLoggedTmYear = -1;
int lastLoggedTmYDay = -1;
int lastLoggedTmHour = -1;
int lastLoggedTmMin  = -1;

// Runtime config / user-data filesystem state
mbed::BlockDevice* qspiRoot = mbed::BlockDevice::get_default_instance();
mbed::MBRBlockDevice user_data_p4(qspiRoot, 4);
mbed::MBRBlockDevice user_data_p3(qspiRoot, 3);
mbed::LittleFileSystem user_lfs("user");
mbed::FATFileSystem user_fatfs("user");
mbed::FileSystem* user_fs = nullptr;
bool userFsMounted = false;
String userFsType = "unmounted";
int userFsPartition = -1;
String configLoadStatus = "not attempted";
String configSource = "defaults";

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

void sendHttpJson(WiFiClient &client, int statusCode, const String &body);
String probeJson(const ProbeReading &r);
String statusJson();
void handleHttpClient();

bool mountUserFileSystem();
void buildPostPath();
bool loadRuntimeConfig();
void printRuntimeConfigSummary();

void initUserInterface();
void updateUserInterface();
void handleUserButton();
