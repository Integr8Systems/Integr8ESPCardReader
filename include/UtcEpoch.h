#pragma once
#include <cstddef>
#include <cstdint>

// Small, portable UTC-to-epoch conversion for the ESP32 Arduino toolchain.
// Avoids timegm() (not declared in some ESP32/newlib builds) and avoids mktime()
// (which interprets dates using the configured local timezone).
namespace Integr8Time {
inline bool isLeapYear(int year) {
  return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

inline int daysInMonth(int year, int month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

inline bool readTwoDigits(const char* text, size_t offset, int& value) {
  const char a = text[offset];
  const char b = text[offset + 1];
  if (a < '0' || a > '9' || b < '0' || b > '9') return false;
  value = (a - '0') * 10 + (b - '0');
  return true;
}

// Strict yyyy-MM-dd HH:mm:ss. Rejects invalid calendar dates and the part of
// year 2106 that cannot fit in a 32-bit unsigned Unix epoch.
inline bool parseUtcDateTime(const char* text, size_t length, uint32_t& epoch) {
  if (!text || length != 19 ||
      text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
      text[13] != ':' || text[16] != ':') return false;

  int year = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    year = year * 10 + text[i] - '0';
  }

  int month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!readTwoDigits(text, 5, month) ||
      !readTwoDigits(text, 8, day) ||
      !readTwoDigits(text, 11, hour) ||
      !readTwoDigits(text, 14, minute) ||
      !readTwoDigits(text, 17, second)) return false;

  if (year < 1970 || year > 2106 ||
      month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month) ||
      hour > 23 || minute > 59 || second > 59) return false;

  uint64_t days = 0;
  for (int y = 1970; y < year; ++y) {
    days += isLeapYear(y) ? 366 : 365;
  }
  for (int m = 1; m < month; ++m) {
    days += daysInMonth(year, m);
  }
  days += static_cast<uint64_t>(day - 1);

  const uint64_t seconds =
      days * 86400ULL +
      static_cast<uint64_t>(hour) * 3600ULL +
      static_cast<uint64_t>(minute) * 60ULL +
      static_cast<uint64_t>(second);
  if (seconds > UINT32_MAX) return false;
  epoch = static_cast<uint32_t>(seconds);
  return true;
}

// Optional epoch-string input (for example 2524608000). Reject overflow.
inline bool parseEpochDigits(const char* text, size_t length, uint32_t& epoch) {
  if (!text || !length) return false;
  uint32_t result = 0;
  for (size_t i = 0; i < length; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(text[i] - '0');
    if (result > (UINT32_MAX - digit) / 10U) return false;
    result = result * 10U + digit;
  }
  epoch = result;
  return true;
}
} // namespace Integr8Time
