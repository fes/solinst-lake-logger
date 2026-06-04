/**
 * @OnlyCurrentDoc
 */

const SHARED_SECRET = 'PUT_A_LONG_RANDOM_SECRET_HERE';

const HEADERS = [
  'received_at_utc',
  'timestamp_utc',
  'device_id',
  'modbus_id',
  'serial_number',
  'firmware',
  'water_level_m',
  'temperature_c',
  'battery_output_monitor_present',
  'battery_output_monitor_valid',
  'battery_output_voltage_v',
  'battery_output_current_a',
  'battery_output_power_w',
  'solar_input_monitor_present',
  'solar_input_monitor_valid',
  'solar_input_voltage_v',
  'solar_input_current_a',
  'solar_input_power_w',
  'battery_charge_level_pct_approx',
  'solar_charging_battery',
  'weather_enabled',
  'weather_present',
  'weather_valid',
  'weather_modbus_id',
  'weather_read_utc',
  'weather_air_temperature_c',
  'weather_relative_humidity_pct',
  'weather_barometric_pressure_hpa',
  'weather_wind_speed_m_s',
  'weather_wind_direction_deg',
  'weather_rainfall_mm',
  'weather_light_lux',
  'weather_last_error',
  'status'
];

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonResponse({ ok: false, error: 'No POST body' });
    }

    const data = JSON.parse(e.postData.contents);

    if (data.secret !== SHARED_SECRET) {
      return jsonResponse({ ok: false, error: 'Unauthorized' });
    }

    const ss = SpreadsheetApp.getActiveSpreadsheet();
    const spreadsheetName = ss.getName();
    const receivedAtUtc = new Date().toISOString();

    const timestampUtc = asString(data.timestamp_utc) || receivedAtUtc;
    const sheetName = monthSheetNameFromTimestamp(timestampUtc);
    const sheet = getOrCreatePeriodSheet(ss, sheetName);
    const headers = ensureHeaderRow(sheet);

    const valuesByHeader = {
      received_at_utc: receivedAtUtc,
      timestamp_utc: timestampUtc,
      device_id: asString(data.device_id),
      modbus_id: asIntOrBlank(data.modbus_id),
      serial_number: asString(data.serial_number),
      firmware: asString(data.firmware),
      water_level_m: asNumberOrBlank(data.water_level_m),
      temperature_c: asNumberOrBlank(data.temperature_c),
      battery_output_monitor_present: asBooleanOrBlank(data.battery_output_monitor_present),
      battery_output_monitor_valid: asBooleanOrBlank(data.battery_output_monitor_valid),
      battery_output_voltage_v: asNumberOrBlank(data.battery_output_voltage_v),
      battery_output_current_a: asNumberOrBlank(data.battery_output_current_a),
      battery_output_power_w: asNumberOrBlank(data.battery_output_power_w),
      solar_input_monitor_present: asBooleanOrBlank(data.solar_input_monitor_present),
      solar_input_monitor_valid: asBooleanOrBlank(data.solar_input_monitor_valid),
      solar_input_voltage_v: asNumberOrBlank(data.solar_input_voltage_v),
      solar_input_current_a: asNumberOrBlank(data.solar_input_current_a),
      solar_input_power_w: asNumberOrBlank(data.solar_input_power_w),
      battery_charge_level_pct_approx: asNumberOrBlank(data.battery_charge_level_pct_approx),
      solar_charging_battery: asBooleanOrBlank(data.solar_charging_battery),
      weather_enabled: asBooleanOrBlank(data.weather_enabled),
      weather_present: asBooleanOrBlank(data.weather_present),
      weather_valid: asBooleanOrBlank(data.weather_valid),
      weather_modbus_id: asIntOrBlank(data.weather_modbus_id),
      weather_read_utc: asString(data.weather_read_utc),
      weather_air_temperature_c: asNumberOrBlank(data.weather_air_temperature_c),
      weather_relative_humidity_pct: asNumberOrBlank(data.weather_relative_humidity_pct),
      weather_barometric_pressure_hpa: asNumberOrBlank(data.weather_barometric_pressure_hpa),
      weather_wind_speed_m_s: asNumberOrBlank(data.weather_wind_speed_m_s),
      weather_wind_direction_deg: asNumberOrBlank(data.weather_wind_direction_deg),
      weather_rainfall_mm: asNumberOrBlank(data.weather_rainfall_mm),
      weather_light_lux: asNumberOrBlank(data.weather_light_lux),
      weather_last_error: asString(data.weather_last_error),
      status: asString(data.status || 'OK')
    };

    const row = headers.map(header => valuesByHeader[header] !== undefined ? valuesByHeader[header] : '');
    sheet.appendRow(row);

    return jsonResponse({
      ok: true,
      message: 'Row appended',
      spreadsheet_name: spreadsheetName,
      sheet_name: sheetName,
      received_at_utc: receivedAtUtc
    });
  } catch (err) {
    return jsonResponse({ ok: false, error: String(err) });
  }
}

function doGet() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  return jsonResponse({
    ok: true,
    message: 'Logger endpoint is running',
    spreadsheet_name: ss.getName()
  });
}

function getOrCreatePeriodSheet(ss, sheetName) {
  let sheet = ss.getSheetByName(sheetName);

  if (!sheet) {
    sheet = ss.insertSheet(sheetName);
    sheet.appendRow(HEADERS);
    formatHeaderRow(sheet);
    freezeHeaderRow(sheet);
    autoResizeColumns(sheet);
    return sheet;
  }

  ensureHeaderRow(sheet);
  return sheet;
}

function ensureHeaderRow(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
    formatHeaderRow(sheet);
    freezeHeaderRow(sheet);
    autoResizeColumns(sheet);
    return HEADERS.slice();
  }

  let existingHeaders = sheet.getRange(1, 1, 1, Math.max(sheet.getLastColumn(), 1)).getValues()[0]
    .map(h => String(h || '').trim());

  if (existingHeaders.length === 1 && existingHeaders[0] === '') {
    sheet.getRange(1, 1, 1, HEADERS.length).setValues([HEADERS]);
    formatHeaderRow(sheet);
    freezeHeaderRow(sheet);
    autoResizeColumns(sheet);
    return HEADERS.slice();
  }

  const missingHeaders = HEADERS.filter(header => existingHeaders.indexOf(header) === -1);
  if (missingHeaders.length > 0) {
    const startColumn = existingHeaders.length + 1;
    sheet.getRange(1, startColumn, 1, missingHeaders.length).setValues([missingHeaders]);
    existingHeaders = existingHeaders.concat(missingHeaders);
    formatHeaderRow(sheet);
    freezeHeaderRow(sheet);
    autoResizeColumns(sheet);
  }

  return existingHeaders;
}

function monthSheetNameFromTimestamp(timestampUtc) {
  const d = new Date(timestampUtc);
  const validDate = isNaN(d.getTime()) ? new Date() : d;
  const year = validDate.getUTCFullYear();
  const month = String(validDate.getUTCMonth() + 1).padStart(2, '0');
  return year + '-' + month;
}

function formatHeaderRow(sheet) {
  const range = sheet.getRange(1, 1, 1, Math.max(sheet.getLastColumn(), HEADERS.length));
  range.setFontWeight('bold');
}

function freezeHeaderRow(sheet) {
  sheet.setFrozenRows(1);
}

function autoResizeColumns(sheet) {
  sheet.autoResizeColumns(1, Math.max(sheet.getLastColumn(), HEADERS.length));
}

function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function asString(value) {
  return value === undefined || value === null ? '' : String(value);
}

function asNumberOrBlank(value) {
  if (value === undefined || value === null || value === '') return '';
  const n = Number(value);
  return Number.isFinite(n) ? n : '';
}

function asIntOrBlank(value) {
  if (value === undefined || value === null || value === '') return '';
  const n = parseInt(value, 10);
  return Number.isFinite(n) ? n : '';
}

function asBooleanOrBlank(value) {
  if (value === undefined || value === null || value === '') return '';
  if (typeof value === 'boolean') return value;
  if (typeof value === 'string') {
    const normalized = value.trim().toLowerCase();
    if (normalized === 'true') return true;
    if (normalized === 'false') return false;
  }
  return '';
}
