#pragma once

#include <Arduino.h>
#if defined(LOGGER_BOARD_OPTA)
#include <ArduinoRS485.h>
#endif
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <Adafruit_INA228.h>
#if defined(LOGGER_BOARD_OPTA)
#include <U8g2lib.h>
#endif
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "board_profile.h"
#include "rs485_channel.h"
#include <logger_core/domain_logic.h>
#include <logger_core/http_parser.h>
#include <logger_core/modbus_codec.h>
#include <logger_core/ring_buffer.h>
#include <logger_core/rolling_extrema.h>
#include <logger_core/site_presentation.h>

#if __has_include("secrets_local.h")
  #include "secrets_local.h"
#elif defined(ALLOW_PLACEHOLDER_SECRETS)
  #include "secrets_example.h"
#else
  #error "Missing secrets_local.h. Copy secrets_example.h to secrets_local.h and fill in real values. Define ALLOW_PLACEHOLDER_SECRETS only for non-production placeholder builds."
#endif

extern char WIFI_SSID[64];
extern char WIFI_PASS[64];
extern char DEVICE_ID[64];
extern char SHARED_SECRET[128];
extern char POST_DEPLOYMENT_ID[128];

using UploadEndpointMode = logger_core::UploadEndpointMode;
using UploadOutcome = logger_core::UploadOutcome;

// The deployed default is the authenticated fesLabs ingest service. The
// Google Apps Script mode remains available for legacy installations.
constexpr UploadEndpointMode UPLOAD_ENDPOINT_MODE = UploadEndpointMode::FESLABS_INGEST;

extern const char* GOOGLE_APPS_SCRIPT_HOST;
constexpr uint16_t GOOGLE_APPS_SCRIPT_PORT = 443;
extern const char* GOOGLE_APPS_SCRIPT_PATH_PREFIX;
extern const char* GOOGLE_APPS_SCRIPT_PATH_SUFFIX;
extern char GOOGLE_APPS_SCRIPT_PATH[256];

// Production Firebase Hosting route for the dedicated fesLabs ingest Function.
// Port 80/plain HTTP remains available for deliberate local testing only.
extern const char* FESLABS_INGEST_HOST;
extern const char* FESLABS_INGEST_PATH;
constexpr uint16_t FESLABS_INGEST_PORT = 443;
constexpr bool FESLABS_INGEST_USE_HTTPS = true;

constexpr uint16_t HTTP_PORT = 80;
constexpr unsigned long NTP_CLIENT_UPDATE_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr unsigned long NTP_RESYNC_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long NTP_INVALID_CLOCK_RETRY_INITIAL_MS = 30UL * 1000UL;
constexpr unsigned long NTP_INVALID_CLOCK_RETRY_MAX_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long NTP_VALID_CLOCK_RETRY_INITIAL_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long NTP_VALID_CLOCK_RETRY_MAX_MS = 60UL * 60UL * 1000UL;
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
constexpr unsigned long SENSOR_DISCOVERY_RETRY_INITIAL_MS = 30UL * 1000UL;
constexpr unsigned long SENSOR_DISCOVERY_RETRY_MAX_MS = 15UL * 60UL * 1000UL;

// Optional DFRobot SEN0657 7-in-1 RS-485/Modbus weather sensor.
// Its factory address is 0x01, which conflicts with the Solinst 301. Configure
// the weather station to a unique address before connecting both devices.
constexpr bool WEATHER_SENSOR_ENABLED = ACTIVE_BOARD_PROFILE.weatherEnabled;
constexpr uint8_t WEATHER_MODBUS_ID = 2;
constexpr uint32_t WEATHER_BAUD = WLTS_BAUD;
// DFRobot documents 8N1 framing. Opta switches its shared bus per request;
// Giga keeps this framing on its independent weather channel.
constexpr auto WEATHER_SERIAL_CFG = SERIAL_8N1;
constexpr unsigned long WEATHER_SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long WEATHER_STALE_AFTER_MS =
    3UL * WEATHER_SAMPLE_INTERVAL_MS;

constexpr int READ_RETRIES = 4;
constexpr unsigned long INITIAL_BACKOFF_MS = 250;
constexpr unsigned long MAX_BACKOFF_MS     = 4000;
constexpr int POST_RETRIES = 3;
constexpr unsigned long POST_RETRY_DELAY_MS = 2000;
constexpr unsigned long NETWORK_SOCKET_TIMEOUT_MS = 5000UL;
constexpr unsigned long HTTP_RESPONSE_TIMEOUT_MS = 10000UL;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_INITIAL_MS = 30000UL;
constexpr unsigned long UPLOAD_RETRY_COOLDOWN_MAX_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t SYSTEM_WATCHDOG_TIMEOUT_MS = 30000UL;

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
constexpr unsigned long POWER_MONITOR_POLL_INTERVAL_MS = 10UL * 1000UL;

constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

extern const char* LEVEL_UNITS;
extern const char* TEMP_UNITS;

struct SensorIdentity {
  uint32_t serialNumber = 0;
  uint8_t fwMajor = 0;
  uint8_t fwMinor = 0;
  uint16_t fwBeta = 0;
};

using PowerMonitorSnapshot = logger_core::PowerMonitorSnapshot;
using PowerSnapshot = logger_core::PowerSnapshot;

struct WeatherSummary : logger_core::WeatherSummary {
  String startUtc = "";
  String endUtc = "";
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

struct SiteReading {
  String timestampUtc;
  uint8_t modbusId = 0;
  SensorIdentity sensorIdentity;
  float level = NAN;
  float temperature = NAN;
  bool valid = false;
  PowerMonitorSnapshot batteryOutput;
  PowerMonitorSnapshot solarInput;
  WeatherReading weather;
};

using ProbeReading = SiteReading;

enum class BacklogClearIntent : uint8_t {
  UNCONFIRMED,
  CONFIRMED
};

class ReadingStorage {
 public:
  virtual ~ReadingStorage() = default;
  virtual bool enqueue(const SiteReading& reading) = 0;
  virtual bool peek(SiteReading& reading) const = 0;
  virtual bool dequeue(SiteReading& reading) = 0;
  virtual bool get(size_t index, SiteReading& reading) const = 0;
  virtual size_t count() const = 0;
  virtual size_t capacity() const = 0;
  virtual size_t clear(BacklogClearIntent intent) = 0;
  virtual String oldestTimestampUtc() const = 0;
  virtual String newestTimestampUtc() const = 0;
};

template <size_t Capacity>
class RamReadingStorage final : public ReadingStorage {
 public:
  bool enqueue(const SiteReading& reading) override {
    return readings_.push(reading);
  }

  bool peek(SiteReading& reading) const override {
    return readings_.front(reading);
  }

  bool dequeue(SiteReading& reading) override {
    return readings_.pop(reading);
  }

  bool get(size_t index, SiteReading& reading) const override {
    return readings_.get(index, reading);
  }

  size_t count() const override {
    return readings_.size();
  }

  size_t capacity() const override {
    return readings_.capacity();
  }

  size_t clear(BacklogClearIntent intent) override {
    if (intent != BacklogClearIntent::CONFIRMED) return 0;
    const size_t cleared = readings_.size();
    readings_.clear();
    return cleared;
  }

  String oldestTimestampUtc() const override {
    SiteReading reading;
    return readings_.front(reading) ? reading.timestampUtc : String("");
  }

  String newestTimestampUtc() const override {
    SiteReading reading;
    return readings_.back(reading) ? reading.timestampUtc : String("");
  }

 private:
  logger_core::RingBuffer<SiteReading, Capacity> readings_;
};

extern WiFiServer server;
extern bool httpServerStarted;
extern WiFiClient wifiClient;
extern WiFiSSLClient wifiSslClient;
extern HttpClient googleAppsScriptHttpClient;
extern HttpClient fesLabsHttpsClient;
extern HttpClient fesLabsHttpClient;
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;

extern Adafruit_INA228 batteryOutputMonitor;
extern Adafruit_INA228 solarInputMonitor;
extern bool batteryOutputMonitorPresent;
extern bool solarInputMonitorPresent;
extern String powerMonitorInitStatus;
extern unsigned long lastSuccessfulBatteryOutputReadMs;
extern unsigned long lastSuccessfulSolarInputReadMs;
extern String lastSuccessfulBatteryOutputReadUtc;
extern String lastSuccessfulSolarInputReadUtc;
extern PowerSnapshot latestPowerSnapshot;
extern logger_core::PeriodicPollState powerPollState;

extern bool weatherSensorPresent;
extern bool weatherSensorValid;
extern String weatherSensorInitStatus;
extern unsigned long lastWeatherAttemptMs;
extern unsigned long lastSuccessfulWeatherReadMs;
extern String lastSuccessfulWeatherReadUtc;
extern String lastWeatherError;
extern String lastWeatherErrorUtc;
extern WeatherReading lastWeatherReading;
extern WeatherSummary weatherSummary;
extern uint32_t weatherReadingRevision;

#if defined(LOGGER_BOARD_OPTA)
extern U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display;
#endif
extern bool displayPresent;
extern bool displayAwake;
extern unsigned long displayOnSeconds;
extern unsigned long displayWakeUntilMs;
extern bool userButtonPresent;
extern unsigned long lastUserButtonPressMs;
extern String lastUserButtonPressUtc;

constexpr size_t BACKLOG_CAPACITY = ACTIVE_BOARD_PROFILE.backlogCapacity;
extern RamReadingStorage<BACKLOG_CAPACITY> backlogStorage;

extern uint8_t detectedSensorId;
extern SensorIdentity detectedIdentity;
extern logger_core::SensorDiscoveryState sensorDiscoveryState;

extern ProbeReading lastProbeReading;
extern bool clockValid;

extern unsigned long bootMs;
extern unsigned long lastNtpSyncMs;
extern logger_core::NtpSyncState ntpSyncState;
extern unsigned long lastSuccessfulProbeReadMs;
extern unsigned long lastSuccessfulUploadMs;
extern unsigned long lastProbeAttemptMs;
extern unsigned long lastUploadAttemptMs;
extern unsigned long nextUploadAllowedMs;

extern String lastSuccessfulProbeReadUtc;
extern String lastSuccessfulUploadUtc;
extern String lastUploadError;
extern String lastUploadErrorUtc;
extern int lastPermanentUploadRejectionStatus;
extern String lastPermanentUploadRejectionError;
extern String lastPermanentUploadRejectionUtc;
extern String lastPermanentUploadRejectedReadingUtc;

extern uint32_t successfulProbeReads;
extern uint32_t failedProbeReads;
extern uint32_t successfulUploads;
extern uint32_t failedUploads;
extern uint32_t droppedBacklogEntries;
extern uint32_t consecutiveUploadFailures;
extern uint32_t permanentUploadRejections;
extern uint32_t permanentBacklogDrops;

extern logger_core::LogScheduleState logScheduleState;
extern uint32_t siteReadingRevision;

extern String configLoadStatus;
extern String configSource;

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
void kickSystemWatchdog();
const char* lastSystemResetReasonName();

bool initPowerMonitors();
void pollPowerMonitorsIfDue(bool force = false);
void refreshPowerForReading(ProbeReading &reading);
void copyLatestPowerSnapshotToReading(ProbeReading &reading);
void printPowerMonitorSummary();
bool batteryVoltage24hExtrema(float &minimumV, float &maximumV);

float approximateBatteryChargePercent(const ProbeReading &reading);
bool solarChargingBatteryNow(const ProbeReading &reading);
void appendPowerMonitorJson(String &body, const char *prefix, const PowerMonitorSnapshot &snapshot);

void initWeatherSensor();
bool readWeatherNow(WeatherReading &weather);
void readWeatherForReading(ProbeReading &reading);
void pollWeatherIfDue(bool force = false);
bool latestWeatherSampleFresh();
void resetWeatherSummaryForNextInterval();
void appendWeatherJson(String &body, const char *prefix, const WeatherReading &weather);
void beginSolinstRs485();
void beginWeatherRs485();

bool readInputRegister(uint8_t slaveId, uint16_t reg, uint16_t &value);
uint16_t modbusCrc16(const uint8_t *data, size_t len);
void printModbusBytes(const char *label, const uint8_t *data, size_t len);
const char *validateModbusResponse(const uint8_t *response, size_t length,
                                   uint8_t slaveId, uint8_t functionCode,
                                   uint8_t expectedByteCount);
int findValidResponseOffset(const uint8_t *buffer, size_t length,
                            uint8_t slaveId, uint8_t functionCode,
                            uint8_t expectedByteCount);
void clearRs485ReceiveBuffer(Rs485Channel &channel);
bool rs485WriteBytes(Rs485Channel &channel, const uint8_t *data, size_t len);
size_t readRawResponseUntilCandidate(Rs485Channel &channel,
                                     uint8_t *buffer, size_t bufferSize,
                                     unsigned long timeoutMs,
                                     uint8_t slaveId, uint8_t functionCode,
                                     uint8_t expectedByteCount,
                                     int &responseOffset);
bool readInputRegisterWithRetry(uint8_t slaveId, uint16_t reg, uint16_t &value);
bool readRegisterPairWithRetry(uint8_t slaveId, uint16_t regHi, uint16_t regLo, uint16_t &hi, uint16_t &lo);
bool readSensorIdentity(uint8_t slaveId, SensorIdentity &id);
bool identityLooksValid(const SensorIdentity &id);
bool scanForSensor(uint8_t startId, uint8_t endId, uint8_t &foundId, SensorIdentity &foundIdentity);
bool attemptSensorDiscovery();
void maintainSensorDiscovery();
bool readLevelAndTemperature(uint8_t slaveId, ProbeReading &reading);
bool probeNow(ProbeReading &reading);
void printIdentity(uint8_t slaveId, const SensorIdentity &id);

String makePayload(const ProbeReading &r);
struct UploadResult {
  UploadOutcome outcome = UploadOutcome::DEFERRED;
  int statusCode = 0;
  String error = "";
};
const char* uploadEndpointModeName();
const char* uploadEndpointHost();
const char* uploadEndpointPath();
uint16_t uploadEndpointPort();
bool uploadEndpointUsesHttps();
HttpClient& uploadHttpClient();
UploadResult postJson(const String &payload);
UploadResult postReadingWithRetry(const ProbeReading &r);
void flushBacklogOnce();
bool performProbeAndUpload(const char *reason);
logger_core::SiteSnapshot currentSiteSnapshot();
void recordUploadFailure(const String &reason);
void recordPermanentUploadRejection(const UploadResult &result,
                                    const ProbeReading &reading);
void clearUploadFailureState();

void sendHttpJson(WiFiClient &client, int statusCode, const String &body,
                  const char* extraHeader = nullptr);
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
