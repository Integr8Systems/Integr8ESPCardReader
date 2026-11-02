#include "../include/storage.h"
#include "../include/config.h"
#include <stddef.h>

namespace {
constexpr char CARD_FILE[] = "/cards.dat";
constexpr char TX_FILE[] = "/transactions.dat";
constexpr uint32_t CARD_BYTES = MAX_CARDS * sizeof(CardRecord);
constexpr uint32_t TX_BYTES = MAX_TRANSACTIONS * sizeof(TransactionRecord);
bool ensureFile(const char* name, uint32_t bytes) {
  File f = LittleFS.open(name, "r+");
  if (!f) f = LittleFS.open(name, "w+");
  if (!f) return false;
  // LittleFS files are not sparse. Extend sequentially; do not seek beyond EOF.
  if (f.size() < bytes) {
    uint32_t remaining = bytes - f.size();
    if (!f.seek(f.size(), SeekSet)) { f.close(); return false; }
    uint8_t zeros[512] = {};
    while (remaining) {
      size_t count = std::min<uint32_t>(remaining, sizeof(zeros));
      if (f.write(zeros, count) != count) { f.close(); return false; }
      remaining -= count;
      delay(0);
    }
    f.flush();
  }
  bool ok = f.size() == bytes;
  f.close();
  return ok;
}
}
bool Store::begin() {
  // The partition label is "storage" (see partitions.csv). The library default
  // is "spiffs", which would silently select the wrong partition.
  // A new board must be explicitly provisioned once; never wipe existing data
  // automatically just because a mount failed in the field.
  if (!LittleFS.begin(false, "/littlefs", 10, "storage")) {
    if (!Config::ALLOW_FIRST_BOOT_FORMAT ||
        !LittleFS.begin(true, "/littlefs", 10, "storage")) return false;
  }
  if (!ensureFile(CARD_FILE, CARD_BYTES) || !ensureFile(TX_FILE, TX_BYTES)) return false;
  rebuildIndex();
  rebuildTransactions();
  return true;
}
bool Store::readCard(uint32_t slot, CardRecord& r) {
  if (slot >= MAX_CARDS) return false;
  File f = LittleFS.open(CARD_FILE, "r");
  if (!f) return false;
  bool ok = f.seek(slot * sizeof(r), SeekSet) && f.readBytes((char*)&r, sizeof(r)) == sizeof(r);
  f.close();
  return ok;
}
bool Store::writeCard(uint32_t slot, CardRecord r) {
  if (slot >= MAX_CARDS) return false;
  signCard(r);
  File f = LittleFS.open(CARD_FILE, "r+");
  if (!f) return false;
  bool ok = f.seek(slot * sizeof(r), SeekSet) && f.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
  if (ok) f.flush();
  f.close();
  return ok;
}
bool Store::readTransaction(uint32_t slot, TransactionRecord& r) {
  if (slot >= MAX_TRANSACTIONS) return false;
  File f = LittleFS.open(TX_FILE, "r");
  if (!f) return false;
  bool ok = f.seek(slot * sizeof(r), SeekSet) && f.readBytes((char*)&r, sizeof(r)) == sizeof(r);
  f.close();
  return ok;
}
bool Store::writeTransaction(uint32_t slot, TransactionRecord r) {
  if (slot >= MAX_TRANSACTIONS) return false;
  signTransaction(r);
  File f = LittleFS.open(TX_FILE, "r+");
  if (!f) return false;
  bool ok = f.seek(slot * sizeof(r), SeekSet) && f.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
  if (ok) f.flush();
  f.close();
  return ok;
}
void Store::rebuildIndex() {
  index.clear(); activeCards = 0;
  File f = LittleFS.open(CARD_FILE, "r");
  if (!f) return;
  CardRecord r;
  for (uint32_t slot = 0; slot < MAX_CARDS; ++slot) {
    if (f.readBytes((char*)&r, sizeof(r)) != sizeof(r)) break;
    if (validCard(r)) { index.push_back({r.cardNumber, r.assetId, slot}); ++activeCards; }
  }
  f.close();
  std::sort(index.begin(), index.end(), [](const CardIndex& a, const CardIndex& b) { return a.card < b.card; });
}
int32_t Store::findAsset(uint32_t assetId) {
  for (const auto& e : index) if (e.asset == assetId) return e.slot;
  return -1;
}
bool Store::upsert(CardRecord record) {
  if (!record.assetId || !record.cardNumber || record.fromEpoch > record.toEpoch) return false;
  auto byCard = std::lower_bound(index.begin(), index.end(), record.cardNumber,
    [](const CardIndex& a, uint64_t number) { return a.card < number; });
  if (byCard != index.end() && byCard->card == record.cardNumber && byCard->asset != record.assetId) return false;
  int32_t slot = findAsset(record.assetId);
  if (slot < 0) {
    if (activeCards >= MAX_CARDS) return false;
    // Find an unused slot. Deleted entries are reusable.
    CardRecord candidate;
    for (uint32_t i = 0; i < MAX_CARDS; ++i) {
      if (!readCard(i, candidate)) return false;
      if (!validCard(candidate)) { slot = i; break; }
    }
  }
  if (slot < 0 || !writeCard(slot, record)) return false;
  rebuildIndex();
  return true;
}
bool Store::remove(uint32_t assetId) {
  int32_t slot = findAsset(assetId);
  if (slot < 0) return false;
  CardRecord empty = {};
  // Zero indicates deleted, and is invalid according to validation.
  File f = LittleFS.open(CARD_FILE, "r+");
  if (!f) return false;
  bool ok = f.seek(slot * sizeof(empty), SeekSet) &&
            f.write((const uint8_t*)&empty, sizeof(empty)) == sizeof(empty);
  if (ok) f.flush();
  f.close();
  if (ok) rebuildIndex();
  return ok;
}
bool Store::lookup(uint64_t number, uint32_t now, uint32_t& assetId) {
  auto it = std::lower_bound(index.begin(), index.end(), number,
    [](const CardIndex& a, uint64_t n) { return a.card < n; });
  if (it == index.end() || it->card != number) return false;
  CardRecord r;
  if (!readCard(it->slot, r) || !validCard(r)) return false;
  assetId = r.assetId;
  return (r.flags & FLAG_ENABLED) && !(r.flags & (FLAG_SUSPENDED | FLAG_BLACKLISTED))
    && now >= r.fromEpoch && now <= r.toEpoch;
}
void Store::rebuildTransactions() {
  pendingCount = 0; nextSequence = 1; nextSlot = 0;
  File f = LittleFS.open(TX_FILE, "r");
  if (!f) return;
  TransactionRecord r; uint64_t maxSequence = 0; uint32_t lastSlot = 0;
  for (uint32_t i = 0; i < MAX_TRANSACTIONS; ++i) {
    if (f.readBytes((char*)&r, sizeof(r)) != sizeof(r)) break;
    if (!validTransaction(r)) continue;
    if (r.state == 1) ++pendingCount;
    if (r.sequence > maxSequence) { maxSequence = r.sequence; lastSlot = i; }
  }
  f.close();
  nextSequence = maxSequence + 1;
  nextSlot = maxSequence ? ((lastSlot + 1) % MAX_TRANSACTIONS) : 0;
}
bool Store::append(uint32_t assetId, uint32_t epoch) {
  TransactionRecord old = {};
  if (!readTransaction(nextSlot, old)) return false;
  TransactionRecord r = {nextSequence, assetId, epoch, 1, 0};
  // Never silently overwrite an unacknowledged access transaction.
  // Stop accepting log writes when all 100,000 records remain pending.
  if (validTransaction(old) && old.state == 1) return false;
  if (!writeTransaction(nextSlot, r)) return false;
  ++pendingCount; ++nextSequence; nextSlot = (nextSlot + 1) % MAX_TRANSACTIONS;
  return true;
}
bool Store::oldest(TransactionRecord& result) {
  File f = LittleFS.open(TX_FILE, "r");
  if (!f) return false;
  bool found = false; TransactionRecord r;
  for (uint32_t i = 0; i < MAX_TRANSACTIONS; ++i) {
    if (f.readBytes((char*)&r, sizeof(r)) != sizeof(r)) break;
    if (validTransaction(r) && r.state == 1 && (!found || r.sequence < result.sequence)) {
      result = r; found = true;
    }
  }
  f.close();
  return found;
}
bool Store::acknowledge(uint64_t sequence) {
  File f = LittleFS.open(TX_FILE, "r");
  if (!f) return false;
  TransactionRecord r; int32_t slot = -1;
  for (uint32_t i = 0; i < MAX_TRANSACTIONS; ++i) {
    if (f.readBytes((char*)&r, sizeof(r)) != sizeof(r)) break;
    if (validTransaction(r) && r.sequence == sequence && r.state == 1) { slot = i; break; }
  }
  f.close();
  if (slot < 0) return false;
  r.state = 2;
  if (!writeTransaction(slot, r)) return false;
  if (pendingCount) --pendingCount;
  return true;
}
