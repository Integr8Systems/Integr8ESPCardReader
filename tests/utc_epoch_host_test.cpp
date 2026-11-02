// Run on a PC with: g++ -std=c++17 -Wall -Wextra -Iinclude tests/utc_epoch_host_test.cpp -o utc_epoch_test && ./utc_epoch_test
#include "UtcEpoch.h"
#include <cassert>
#include <iostream>
#include <string>

bool parse(const std::string& date, uint32_t& epoch) {
  return Integr8Time::parseUtcDateTime(date.c_str(), date.length(), epoch);
}
int main() {
  uint32_t actual = 0;
  assert(parse("1970-01-01 00:00:00", actual) && actual == 0);
  assert(parse("2000-01-01 00:00:00", actual) && actual == 946684800U);
  assert(parse("2024-02-29 12:34:56", actual) && actual == 1709210096U);
  assert(parse("2050-01-01 00:00:00", actual) && actual == 2524608000U);
  assert(parse("2106-02-07 06:28:15", actual) && actual == UINT32_MAX);
  assert(!parse("2106-02-07 06:28:16", actual));
  assert(!parse("2025-02-29 00:00:00", actual));
  assert(!parse("2026-13-01 00:00:00", actual));
  assert(!parse("2026-12-32 00:00:00", actual));
  assert(!parse("2026-11-02 25:00:00", actual));
  assert(!parse("2026/11/02 11:30:00", actual));
  assert(!parse("2026-11-02 11:30:00Z", actual));
  assert(Integr8Time::parseEpochDigits("2524608000", 10, actual) &&
         actual == 2524608000U);
  assert(!Integr8Time::parseEpochDigits("4294967296", 10, actual));
  assert(!Integr8Time::parseEpochDigits("2x", 2, actual));
  std::cout << "UTC epoch date conversion tests passed\n";
}
