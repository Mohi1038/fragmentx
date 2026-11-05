/// @file metadata_store.cpp
#include "metadata_store.hpp"
#include <cassert>
#include <cstring>

namespace fragmentx {
namespace runtime {

MetadataStore::MetadataStore(std::size_t initial_capacity) {
  // Round up to power of 2.
  std::size_t cap = 1;
  while (cap < initial_capacity) cap <<= 1;
  table_.resize(cap);
}

MetadataStore::~MetadataStore() = default;

std::size_t MetadataStore::find_slot(void* ptr) const noexcept {
  const std::size_t mask = table_.size() - 1;
  // FNV-1a hash of the pointer.
  uint64_t h = 14695981039346656037ULL;
  auto val   = reinterpret_cast<uintptr_t>(ptr);
  for (int i = 0; i < 8; ++i, val >>= 8) {
    h ^= static_cast<uint8_t>(val & 0xFF);
    h *= 1099511628211ULL;
  }
  std::size_t idx = static_cast<std::size_t>(h) & mask;
  for (std::size_t probe = 0; probe < table_.size(); ++probe) {
    const std::size_t slot = (idx + probe) & mask;
    auto state = table_[slot].state.load(std::memory_order_acquire);
    if (state == Slot::State::Empty) return slot; // not found, empty
    if (state == Slot::State::Occupied && table_[slot].key == ptr) return slot;
  }
  return table_.size(); // full (should never happen with load factor control)
}

void MetadataStore::insert(void* ptr, const AllocationRecord& record) {
  std::lock_guard<std::mutex> lk(mutex_);
  maybe_rehash();
  const std::size_t mask = table_.size() - 1;
  uint64_t h = 14695981039346656037ULL;
  auto val   = reinterpret_cast<uintptr_t>(ptr);
  for (int i = 0; i < 8; ++i, val >>= 8) {
    h ^= static_cast<uint8_t>(val & 0xFF);
    h *= 1099511628211ULL;
  }
  std::size_t idx = static_cast<std::size_t>(h) & mask;
  for (std::size_t probe = 0; probe < table_.size(); ++probe) {
    std::size_t slot = (idx + probe) & mask;
    auto state = table_[slot].state.load(std::memory_order_relaxed);
    if (state != Slot::State::Occupied) {
      table_[slot].key   = ptr;
      table_[slot].value = record;
      table_[slot].state.store(Slot::State::Occupied, std::memory_order_release);
      size_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    if (table_[slot].key == ptr) {
      table_[slot].value = record; // update
      return;
    }
  }
  // Table full — this shouldn't happen due to rehashing.
  assert(false && "MetadataStore: table full");
}

const AllocationRecord* MetadataStore::lookup(void* ptr) const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::size_t slot = find_slot(ptr);
  if (slot >= table_.size()) return nullptr;
  if (table_[slot].state.load(std::memory_order_acquire) != Slot::State::Occupied)
    return nullptr;
  if (table_[slot].key != ptr) return nullptr;
  return &table_[slot].value;
}

bool MetadataStore::remove(void* ptr) {
  std::lock_guard<std::mutex> lk(mutex_);
  const std::size_t mask = table_.size() - 1;
  uint64_t h = 14695981039346656037ULL;
  auto val   = reinterpret_cast<uintptr_t>(ptr);
  for (int i = 0; i < 8; ++i, val >>= 8) {
    h ^= static_cast<uint8_t>(val & 0xFF);
    h *= 1099511628211ULL;
  }
  std::size_t idx = static_cast<std::size_t>(h) & mask;
  for (std::size_t probe = 0; probe < table_.size(); ++probe) {
    std::size_t slot = (idx + probe) & mask;
    auto state = table_[slot].state.load(std::memory_order_acquire);
    if (state == Slot::State::Empty) return false;
    if (state == Slot::State::Occupied && table_[slot].key == ptr) {
      table_[slot].state.store(Slot::State::Tombstone, std::memory_order_release);
      size_.fetch_sub(1, std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}

void MetadataStore::maybe_rehash() {
  // Rehash when load factor exceeds 0.6.
  if (size_.load(std::memory_order_relaxed) * 10 < table_.size() * 6) return;
  std::vector<Slot> old = std::move(table_);
  table_.assign(old.size() * 2, Slot{});
  size_.store(0, std::memory_order_relaxed);
  for (auto& slot : old) {
    if (slot.state.load(std::memory_order_relaxed) == Slot::State::Occupied) {
      uint64_t hash = 14695981039346656037ULL;
      auto value = reinterpret_cast<uintptr_t>(slot.key);
      for (std::size_t byte = 0; byte < sizeof(value); ++byte, value >>= 8U) {
        hash ^= static_cast<uint8_t>(value & 0xffU);
        hash *= 1099511628211ULL;
      }
      const std::size_t mask = table_.size() - 1;
      const std::size_t start = static_cast<std::size_t>(hash) & mask;
      for (std::size_t probe = 0; probe < table_.size(); ++probe) {
        Slot& destination = table_[(start + probe) & mask];
        if (destination.state.load(std::memory_order_relaxed) == Slot::State::Empty) {
          destination.key = slot.key;
          destination.value = slot.value;
          destination.state.store(Slot::State::Occupied, std::memory_order_relaxed);
          size_.fetch_add(1, std::memory_order_relaxed);
          break;
        }
      }
    }
  }
}

} // namespace runtime
} // namespace fragmentx

