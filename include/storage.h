#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <algorithm>
#include <vector>

constexpr uint32_t MAX_CARDS = 50000;
constexpr uint32_t MAX_TRANSACTIONS = 100000;
constexpr uint8_t FLAG_ENABLED = 1, FLAG_SUSPENDED = 2, FLAG_BLACKLISTED = 4;

struct __attribute__((packed)) CardRecord {
  uint32_t assetId;
  uint64_t cardNumber;
  uint32_t fromEpoch;
  uint32_t toEpoch;
  uint8_t flags;
  uint32_t checksum;
};
struct __attribute__((packed)) TransactionRecord {
  uint64_t sequence;
  uint32_t assetId;
  uint32_t epoch;
  uint8_t state; // 0 empty, 1 awaiting server ACK, 2 acknowledged
  uint32_t checksum;
};
static_assert(sizeof(CardRecord) == 25, "Unexpected card record size");
static_assert(sizeof(TransactionRecord) == 21, "Unexpected transaction record size");

// FNV1a integrity detection; not authentication or encryption.
inline uint32_t recordHash(const void* buffer, size_t size) {
  uint32_t hash = 2166136261UL;
  const auto* bytes = static_cast<const uint8_t*>(buffer);
  for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}
inline void signCard(CardRecord& r) { r.checksum = recordHash(&r, offsetof(CardRecord, checksum)); }
inline void signTransaction(TransactionRecord& r) { r.checksum = recordHash(&r, offsetof(TransactionRecord, checksum)); }
inline bool validCard(const CardRecord& r) { return r.assetId && r.checksum == recordHash(&r, offsetof(CardRecord, checksum)); }
inline bool validTransaction(const TransactionRecord& r) {
  return r.state >= 1 && r.state <= 2 && r.checksum == recordHash(&r, offsetof(TransactionRecord, checksum));
}
struct CardIndex { uint64_t card; uint32_t asset; uint32_t slot; };
class Store {
public:
  bool begin();
  bool upsert(CardRecord record);
  bool remove(uint32_t assetId);
  bool lookup(uint64_t cardNumber, uint32_t now, uint32_t& assetId);
  bool append(uint32_t assetId, uint32_t epoch);
  bool oldest(TransactionRecord& record);
  bool acknowledge(uint64_t sequence);
  uint32_t cards() const { return activeCards; }
  uint32_t pending() const { return pendingCount; }
private:
  bool readCard(uint32_t slot, CardRecord& record);
  bool writeCard(uint32_t slot, CardRecord record);
  bool readTransaction(uint32_t slot, TransactionRecord& record);
  bool writeTransaction(uint32_t slot, TransactionRecord record);
  void rebuildIndex();
  void rebuildTransactions();
  int32_t findAsset(uint32_t assetId);
  std::vector<CardIndex> index; // approx 800 KB; allocate in PSRAM in future if needed.
  uint32_t activeCards = 0, pendingCount = 0, nextSlot = 0;
  uint64_t nextSequence = 1;
};
