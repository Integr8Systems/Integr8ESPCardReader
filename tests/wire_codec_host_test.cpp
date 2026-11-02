// Native host test, no ESP32 required:
// g++ -std=c++17 -Wall -Wextra -Iinclude tests/wire_codec_host_test.cpp -o /tmp/wire_test && /tmp/wire_test
#include "Integr8Wire.h"
#include <cassert>
#include <iostream>
#include <string>
static std::string encode(const std::string& text) {
  std::string wire;
  assert(Integr8Wire::encode(reinterpret_cast<const uint8_t*>(text.data()), text.size(), wire));
  return wire;
}
static std::string decode(const std::string& wire) {
  std::string plain;
  assert(Integr8Wire::decode(reinterpret_cast<const uint8_t*>(wire.data()), wire.size(), plain));
  return plain;
}
int main() {
  assert(encode("Hi") == "9684");
  assert(decode("9684") == "Hi");
  assert(encode("{}") == "D7B7");
  assert(decode("D7B7") == "{}");
  const std::string json = R"({"Command":"CardUpsert","AssetID":123,"Enabled":true})";
  assert(decode(encode(json)) == json);
  const std::string utf8 = "{\"Message\":\"\xC3\xA9\"}";
  assert(decode(encode(utf8)) == utf8);
  assert(decode(" 9684 \r\n") == "Hi");
  const std::string odd = "968";
  std::string out;
  assert(!Integr8Wire::decode(reinterpret_cast<const uint8_t*>(odd.data()), odd.size(), out));
  const std::string bad = "ZZ";
  assert(!Integr8Wire::decode(reinterpret_cast<const uint8_t*>(bad.data()), bad.size(), out));
  std::cout << "Integr8 reverse-hex codec tests passed\n";
}
