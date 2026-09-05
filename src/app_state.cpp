#include "config.h"

char WIFI_SSID[64] = WIFI_SSID_VALUE;
char WIFI_PASS[64] = WIFI_PASS_VALUE;
char DEVICE_ID[64] = DEVICE_ID_VALUE;
char SHARED_SECRET[128] = SHARED_SECRET_VALUE;
char POST_DEPLOYMENT_ID[128] = DEPLOYMENT_ID_VALUE;

const char* GOOGLE_APPS_SCRIPT_HOST = "script.google.com";
const char* GOOGLE_APPS_SCRIPT_PATH_PREFIX = "/macros/s/";
const char* GOOGLE_APPS_SCRIPT_PATH_SUFFIX = "/exec";
char GOOGLE_APPS_SCRIPT_PATH[256] = "";

const char* FESLABS_INGEST_HOST = "feslabs.com";
const char* FESLABS_INGEST_PATH = "/api/lake/ingest";

const char* LEVEL_UNITS = "m";
const char* TEMP_UNITS = "C";

WiFiServer server(HTTP_PORT);
bool httpServerStarted = false;
WiFiClient wifiClient;
WiFiSSLClient wifiSslClient;
HttpClient googleAppsScriptHttpClient(wifiSslClient, GOOGLE_APPS_SCRIPT_HOST, GOOGLE_APPS_SCRIPT_PORT);
HttpClient fesLabsHttpsClient(wifiSslClient, FESLABS_INGEST_HOST, FESLABS_INGEST_PORT);
HttpClient fesLabsHttpClient(wifiClient, FESLABS_INGEST_HOST, FESLABS_INGEST_PORT);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, NTP_CLIENT_UPDATE_INTERVAL_MS);

Adafruit_INA228 batteryOutputMonitor;
Adafruit_INA228 solarInputMonitor;
bool batteryOutputMonitorPresent = false;
bool solarInputMonitorPresent = false;
String powerMonitorInitStatus = "not initialized";
unsigned long lastSuccessfulBatteryOutputReadMs = 0;
unsigned long lastSuccessfulSolarInputReadMs = 0;
String lastSuccessfulBatteryOutputReadUtc = "";
String lastSuccessfulSolarInputReadUtc = "";
PowerSnapshot latestPowerSnapshot;
logger_core::PeriodicPollState powerPollState;

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
uint32_t weatherReadingRevision = 0;

#if defined(LOGGER_BOARD_OPTA)
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#endif
bool displayPresent = false;
bool displayAwake = false;
unsigned long displayOnSeconds = DISPLAY_ON_SECONDS_DEFAULT;
unsigned long displayWakeUntilMs = 0;
bool userButtonPresent = false;
unsigned long lastUserButtonPressMs = 0;
String lastUserButtonPressUtc = "";

RamReadingStorage<BACKLOG_CAPACITY> backlogStorage;

uint8_t detectedSensorId = 0;
SensorIdentity detectedIdentity;
logger_core::SensorDiscoveryState sensorDiscoveryState;

ProbeReading lastProbeReading;
bool clockValid = false;

unsigned long bootMs = 0;
unsigned long lastNtpSyncMs = 0;
logger_core::NtpSyncState ntpSyncState;
unsigned long lastSuccessfulProbeReadMs = 0;
unsigned long lastSuccessfulUploadMs = 0;
unsigned long lastProbeAttemptMs = 0;
unsigned long lastUploadAttemptMs = 0;
unsigned long nextUploadAllowedMs = 0;

String lastSuccessfulProbeReadUtc = "";
String lastSuccessfulUploadUtc = "";
String lastUploadError = "";
String lastUploadErrorUtc = "";
int lastPermanentUploadRejectionStatus = 0;
String lastPermanentUploadRejectionError = "";
String lastPermanentUploadRejectionUtc = "";
String lastPermanentUploadRejectedReadingUtc = "";

uint32_t successfulProbeReads = 0;
uint32_t failedProbeReads = 0;
uint32_t successfulUploads = 0;
uint32_t failedUploads = 0;
uint32_t droppedBacklogEntries = 0;
uint32_t consecutiveUploadFailures = 0;
uint32_t permanentUploadRejections = 0;
uint32_t permanentBacklogDrops = 0;

logger_core::LogScheduleState logScheduleState;
uint32_t siteReadingRevision = 0;

String configLoadStatus = "not attempted";
String configSource = "compiled secrets";
