#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// EXACT counterpart of the Integr8 C# transport:
// UTF8 JSON -> BitConverter.ToString(bytes).Replace("-", "") -> Reverse(hex)
// The reversed characters are sent as ASCII UTF8 bytes, NOT raw hex bytes.
// The reverse transformation must run before decoding pairs of hex digits.
// WARNING: This is encoding/obfuscation, NOT cryptographic security.
namespace Integr8Wire {
inline uint8_t hexDigit(uint8_t nibble) {
  return static_cast<uint8_t>("0123456789ABCDEF"[nibble & 15]);
}
inline int hexValue(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
inline bool whitespace(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
         c == '\v' || c == '\f';
}
inline bool encode(const uint8_t* utf8, size_t length, std::string& wire) {
  if ((!utf8 && length) || length > wire.max_size() / 2) return false;
  wire.clear();
  wire.reserve(length * 2);
  for (size_t i = length; i > 0; --i) {
    const uint8_t byte = utf8[i - 1];
    // Reverse the two hex CHARACTERS of every byte, and reverse byte order.
    wire.push_back(static_cast<char>(hexDigit(byte & 15)));
    wire.push_back(static_cast<char>(hexDigit(byte >> 4)));
  }
  return true;
}
inline bool decode(const uint8_t* wire, size_t length, std::string& json) {
  if (!wire && length) return false;
  // Equivalent to C# Reverse(data).Trim(), for ASCII hex + edge whitespace.
  size_t start = 0, end = length;
  while (start < end && whitespace(wire[start])) ++start;
  while (end > start && whitespace(wire[end - 1])) --end;
  if ((end - start) & 1U) return false;
  json.clear();
  json.reserve((end - start) / 2);
  for (size_t i = end; i > start; i -= 2) {
    const int hi = hexValue(wire[i - 1]);
    const int lo = hexValue(wire[i - 2]);
    if (hi < 0 || lo < 0) {
      json.clear();
      return false;
    }
    json.push_back(static_cast<char>((hi << 4) | lo));
  }
  return true;
}
}
