/// @file site_registry.cpp
#include <fragmentx/capture.hpp>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <string>

namespace fragmentx {
namespace capture {

struct SiteInfo { std::string file; std::string function; int line; };
static std::unordered_map<uint64_t, SiteInfo> g_site_registry;
static std::shared_mutex g_registry_mutex;

void register_site(uint64_t hash, const char* file, const char* fn, int line) {
  std::unique_lock lk(g_registry_mutex);
  g_site_registry[hash] = SiteInfo{file ? file : "", fn ? fn : "", line};
}

bool lookup_site(uint64_t hash, std::string& file, std::string& function, int& line) {
  std::shared_lock lk(g_registry_mutex);
  auto it = g_site_registry.find(hash);
  if (it == g_site_registry.end()) return false;
  file = it->second.file; function = it->second.function; line = it->second.line;
  return true;
}

} // namespace capture
} // namespace fragmentx

