float regsToFloat(uint16_t hi, uint16_t lo) {
  uint32_t raw = ((uint32_t)hi << 16) | lo;
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

uint32_t regsToUint32(uint16_t hi, uint16_t lo) {
  return ((uint32_t)hi << 16) | lo;
}

bool isLeapYear(int year) {
  return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

String epochToIso8601UTC(unsigned long epoch) {
  unsigned long t = epoch;

  int second = t % 60; t /= 60;
  int minute = t % 60; t /= 60;
  int hour   = t % 24; t /= 24;

  int year = 1970;
  while (true) {
    int daysInYear = isLeapYear(year) ? 366 : 365;
    if (t >= (unsigned long)daysInYear) {
      t -= daysInYear;
      year++;
    } else {
      break;
    }
  }

  const int monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int month = 0;
  while (true) {
    int dim = monthDays[month];
    if (month == 1 && isLeapYear(year)) dim = 29;
    if (t >= (unsigned long)dim) {
      t -= dim;
      month++;
    } else {
      break;
    }
  }

  int day = (int)t + 1;

  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           year, month + 1, day, hour, minute, second);
  return String(buf);
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

String millisAgeString(unsigned long sinceMs) {
  if (sinceMs == 0) return "never";
  unsigned long ageSec = (millis() - sinceMs) / 1000UL;
  return String(ageSec) + "s";
}