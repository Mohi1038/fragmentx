/// @file policy_serializer.cpp
/// @brief Hand-crafted, zero-dependency JSON serializer and parser for AllocationPolicy.

#include <fragmentx/policy.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace fragmentx {

// ─── AllocationPolicy helpers ────────────────────────────────────────────────

std::optional<uint32_t> AllocationPolicy::arena_for_site(uint64_t site_hash) const noexcept {
  for (const auto& a : arena_assignments) {
    if (a.site_hash == site_hash) {
      return a.arena_id;
    }
  }
  return std::nullopt;
}

// ─── PolicySerializer ────────────────────────────────────────────────────────

static std::string arena_type_to_string(ArenaType type) {
  switch (type) {
    case ArenaType::FixedPool:       return "FixedPool";
    case ArenaType::Region:          return "Region";
    case ArenaType::GeneralPurpose:  return "GeneralPurpose";
    case ArenaType::LongLived:       return "LongLived";
    case ArenaType::Fallback:        return "Fallback";
  }
  return "GeneralPurpose";
}

static ArenaType string_to_arena_type(const std::string& s) {
  if (s == "FixedPool")       return ArenaType::FixedPool;
  if (s == "Region")          return ArenaType::Region;
  if (s == "GeneralPurpose")  return ArenaType::GeneralPurpose;
  if (s == "LongLived")       return ArenaType::LongLived;
  if (s == "Fallback")        return ArenaType::Fallback;
  return ArenaType::GeneralPurpose;
}

static std::string escape_json_str(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (static_cast<unsigned char>(c)) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U) {
          constexpr char hex[] = "0123456789abcdef";
          const auto byte = static_cast<unsigned char>(c);
          out += "\\u00";
          out += hex[(byte >> 4U) & 0x0fU];
          out += hex[byte & 0x0fU];
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string PolicySerializer::to_json(const AllocationPolicy& policy, bool pretty) {
  if (!std::isfinite(policy.cost_value) ||
      !std::isfinite(policy.estimated_improvement) ||
      !std::isfinite(policy.confidence)) {
    return {};
  }
  for (const auto& assignment : policy.arena_assignments) {
    if (!std::isfinite(assignment.confidence)) return {};
  }

  std::ostringstream ss;
  const std::string ind = pretty ? "  " : "";
  const std::string nl  = pretty ? "\n" : "";

  ss << "{" << nl;
  ss << ind << "\"version\": \"" << escape_json_str(policy.version) << "\"," << nl;
  ss << ind << "\"name\": \"" << escape_json_str(policy.name) << "\"," << nl;
  ss << ind << "\"description\": \"" << escape_json_str(policy.description) << "\"," << nl;
  ss << ind << "\"strategy\": \"" << escape_json_str(policy.strategy) << "\"," << nl;
  ss << ind << "\"cost_value\": " << policy.cost_value << "," << nl;
  ss << ind << "\"estimated_improvement\": " << policy.estimated_improvement << "," << nl;
  ss << ind << "\"confidence\": " << policy.confidence << "," << nl;

  // Arenas
  ss << ind << "\"arenas\": [" << nl;
  for (size_t i = 0; i < policy.arenas.size(); ++i) {
    const auto& a = policy.arenas[i];
    ss << ind << ind << "{" << nl;
    ss << ind << ind << ind << "\"id\": " << a.id << "," << nl;
    ss << ind << ind << ind << "\"type\": \"" << arena_type_to_string(a.config.type) << "\"," << nl;
    ss << ind << ind << ind << "\"label\": \"" << escape_json_str(std::string(a.config.label)) << "\"," << nl;
    ss << ind << ind << ind << "\"initial_size\": " << a.config.initial_size << "," << nl;
    ss << ind << ind << ind << "\"object_size\": " << a.config.object_size << "," << nl;
    ss << ind << ind << ind << "\"rationale\": \"" << escape_json_str(std::string(a.rationale)) << "\"" << nl;
    ss << ind << ind << "}" << (i + 1 < policy.arenas.size() ? "," : "") << nl;
  }
  ss << ind << "]," << nl;

  // Assignments
  ss << ind << "\"assignments\": [" << nl;
  for (size_t i = 0; i < policy.arena_assignments.size(); ++i) {
    const auto& asgn = policy.arena_assignments[i];
    ss << ind << ind << "{"
       << "\"site_hash\": " << asgn.site_hash << ", "
       << "\"arena_id\": " << asgn.arena_id << ", "
       << "\"confidence\": " << asgn.confidence << "}"
       << (i + 1 < policy.arena_assignments.size() ? "," : "") << nl;
  }
  ss << ind << "]" << nl;

  ss.put('}');
  ss << nl;
  return ss.str();
}

bool PolicySerializer::save(const AllocationPolicy& policy, const std::string& path) {
  std::string json = to_json(policy, true);
  if (json.empty()) return false;
  std::ofstream f(path);
  if (!f.is_open()) return false;
  f << json;
  return f.good();
}

// ─── PolicyLoader ────────────────────────────────────────────────────────────

static thread_local std::string g_last_policy_error;

const std::string& PolicyLoader::last_error() noexcept {
  return g_last_policy_error;
}

static std::optional<std::string> decode_json_string(
    std::string_view input, size_t& pos) {
  if (pos >= input.size() || input[pos] != '"') return std::nullopt;
  ++pos;
  std::string value;
  while (pos < input.size()) {
    const unsigned char ch = static_cast<unsigned char>(input[pos++]);
    if (ch == '"') return value;
    if (ch != '\\') {
      if (ch < 0x20U) return std::nullopt;
      value += static_cast<char>(ch);
      continue;
    }
    if (pos == input.size()) return std::nullopt;
    switch (input[pos++]) {
      case '"': value += '"'; break;
      case '\\': value += '\\'; break;
      case '/': value += '/'; break;
      case 'b': value += '\b'; break;
      case 'f': value += '\f'; break;
      case 'n': value += '\n'; break;
      case 'r': value += '\r'; break;
      case 't': value += '\t'; break;
      case 'u': {
        if (pos + 4 > input.size()) return std::nullopt;
        unsigned int cp = 0;
        for (unsigned int i = 0; i < 4; ++i) {
          const char digit = input[pos++];
          cp <<= 4U;
          if (digit >= '0' && digit <= '9') cp |= static_cast<unsigned int>(digit - '0');
          else if (digit >= 'a' && digit <= 'f') cp |= static_cast<unsigned int>(digit - 'a' + 10);
          else if (digit >= 'A' && digit <= 'F') cp |= static_cast<unsigned int>(digit - 'A' + 10);
          else return std::nullopt;
        }
        if (cp >= 0xd800U && cp <= 0xdfffU) return std::nullopt;
        if (cp <= 0x7fU) value += static_cast<char>(cp);
        else if (cp <= 0x7ffU) {
          value += static_cast<char>(0xc0U | (cp >> 6U));
          value += static_cast<char>(0x80U | (cp & 0x3fU));
        } else {
          value += static_cast<char>(0xe0U | (cp >> 12U));
          value += static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU));
          value += static_cast<char>(0x80U | (cp & 0x3fU));
        }
        break;
      }
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

// Simple token-based JSON scanner for AllocationPolicy
std::optional<AllocationPolicy> PolicyLoader::parse(std::string_view json_text) {
  g_last_policy_error.clear();
  const size_t first = json_text.find_first_not_of(" \t\r\n");
  const size_t last = json_text.find_last_not_of(" \t\r\n");
  if (first == std::string_view::npos || json_text[first] != '{' ||
      json_text[last] != '}') {
    g_last_policy_error = "Policy JSON must be a non-empty object";
    return std::nullopt;
  }

  try {
  AllocationPolicy pol;

  auto find_string_prop = [&](const std::string& key) -> std::string {
    std::string needle = "\"" + key + "\":";
    size_t pos = json_text.find(needle);
    if (pos == std::string_view::npos) {
      needle = "\"" + key + "\" :";
      pos = json_text.find(needle);
    }
    if (pos == std::string_view::npos) return "";
    pos = json_text.find(':', pos + needle.size() - 1);
    if (pos == std::string_view::npos) return "";
    ++pos;
    while (pos < json_text.size() && std::isspace(
        static_cast<unsigned char>(json_text[pos]))) ++pos;
    auto decoded = decode_json_string(json_text, pos);
    return decoded.value_or("");
  };

  auto find_double_prop = [&](const std::string& key, double def = 0.0) -> double {
    std::string needle = "\"" + key + "\":";
    size_t pos = json_text.find(needle);
    if (pos == std::string_view::npos) {
      needle = "\"" + key + "\" :";
      pos = json_text.find(needle);
    }
    if (pos == std::string_view::npos) return def;
    pos += needle.size();
    while (pos < json_text.size() && std::isspace(
        static_cast<unsigned char>(json_text[pos]))) ++pos;
    size_t end = pos;
    while (end < json_text.size() && (std::isdigit(json_text[end]) || json_text[end] == '.' || json_text[end] == '-' || json_text[end] == 'e' || json_text[end] == 'E' || json_text[end] == '+')) end++;
    if (end > pos) {
      try {
        return std::stod(std::string(json_text.substr(pos, end - pos)));
      } catch (...) {}
    }
    return def;
  };

  pol.version = find_string_prop("version");
  if (pol.version.empty() || json_text.find("\"arenas\"") == std::string_view::npos ||
      json_text.find("\"assignments\"") == std::string_view::npos) {
    g_last_policy_error = "Policy JSON is missing required version, arenas, or assignments fields";
    return std::nullopt;
  }
  pol.name = find_string_prop("name");
  pol.description = find_string_prop("description");
  pol.strategy = find_string_prop("strategy");
  pol.cost_value = find_double_prop("cost_value", 0.0);
  pol.estimated_improvement = find_double_prop("estimated_improvement", 0.0);
  pol.confidence = find_double_prop("confidence", 0.0);

  // Parse arenas block
  size_t arenas_pos = json_text.find("\"arenas\"");
  if (arenas_pos != std::string_view::npos) {
    size_t start_arr = json_text.find('[', arenas_pos);
    size_t end_arr = (start_arr != std::string_view::npos) ? json_text.find(']', start_arr) : std::string_view::npos;
    if (start_arr != std::string_view::npos && end_arr != std::string_view::npos) {
      std::string_view arr_str = json_text.substr(start_arr, end_arr - start_arr + 1);
      size_t cur = 0;
      while ((cur = arr_str.find('{', cur)) != std::string_view::npos) {
        size_t obj_end = arr_str.find('}', cur);
        if (obj_end == std::string_view::npos) break;
        std::string_view obj = arr_str.substr(cur, obj_end - cur + 1);

        PolicyArena ar;
        // id
        size_t id_pos = obj.find("\"id\"");
        if (id_pos != std::string_view::npos) {
          size_t c_pos = obj.find(':', id_pos);
          if (c_pos != std::string_view::npos) {
            ar.id = static_cast<uint32_t>(
              std::stoul(std::string(obj.substr(c_pos + 1))));
          }
        }
        // type
        size_t type_pos = obj.find("\"type\"");
        if (type_pos != std::string_view::npos) {
          size_t q1 = obj.find('"', obj.find(':', type_pos));
          size_t q2 = (q1 != std::string_view::npos) ? obj.find('"', q1 + 1) : std::string_view::npos;
          if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
            ar.config.type = string_to_arena_type(std::string(obj.substr(q1 + 1, q2 - q1 - 1)));
          }
        }
        auto find_arena_number = [&](const std::string& key, double fallback) {
          const auto key_pos = obj.find("\"" + key + "\"");
          if (key_pos == std::string_view::npos) return fallback;
          const auto colon = obj.find(':', key_pos);
          if (colon == std::string_view::npos) return fallback;
          size_t number_start = colon + 1;
          while (number_start < obj.size() && std::isspace(
              static_cast<unsigned char>(obj[number_start]))) ++number_start;
          size_t number_end = number_start;
          while (number_end < obj.size() &&
              (std::isdigit(static_cast<unsigned char>(obj[number_end])) ||
               obj[number_end] == '+' || obj[number_end] == '-')) ++number_end;
          if (number_end == number_start) return fallback;
          return std::stod(std::string(obj.substr(number_start, number_end - number_start)));
        };
        ar.config.initial_size = static_cast<std::size_t>(find_arena_number("initial_size", 0));
        ar.config.object_size = static_cast<std::size_t>(find_arena_number("object_size", 0));
        const auto label_pos = obj.find("\"label\"");
        if (label_pos != std::string_view::npos) {
          const auto colon = obj.find(':', label_pos);
          if (colon != std::string_view::npos) {
            const auto quote = obj.find('"', colon + 1);
            if (quote != std::string_view::npos) {
              size_t string_pos = quote;
              if (auto decoded = decode_json_string(obj, string_pos))
                ar.config.label = std::move(*decoded);
            }
          }
        }
        const auto rationale_pos = obj.find("\"rationale\"");
        if (rationale_pos != std::string_view::npos) {
          const auto colon = obj.find(':', rationale_pos);
          if (colon != std::string_view::npos) {
            const auto quote = obj.find('"', colon + 1);
            if (quote != std::string_view::npos) {
              size_t string_pos = quote;
              if (auto decoded = decode_json_string(obj, string_pos))
                ar.rationale = std::move(*decoded);
            }
          }
        }
        pol.arenas.push_back(ar);
        cur = obj_end + 1;
      }
    }
  }

  // Parse assignments block
  size_t asgn_pos = json_text.find("\"assignments\"");
  if (asgn_pos != std::string_view::npos) {
    size_t start_arr = json_text.find('[', asgn_pos);
    size_t end_arr = (start_arr != std::string_view::npos) ? json_text.find(']', start_arr) : std::string_view::npos;
    if (start_arr != std::string_view::npos && end_arr != std::string_view::npos) {
      std::string_view arr_str = json_text.substr(start_arr, end_arr - start_arr + 1);
      size_t cur = 0;
      while ((cur = arr_str.find('{', cur)) != std::string_view::npos) {
        size_t obj_end = arr_str.find('}', cur);
        if (obj_end == std::string_view::npos) break;
        std::string_view obj = arr_str.substr(cur, obj_end - cur + 1);

        ArenaAssignment asgn{};
        size_t sh_pos = obj.find("\"site_hash\"");
        if (sh_pos != std::string_view::npos) {
          size_t c = obj.find(':', sh_pos);
          if (c != std::string_view::npos) {
            asgn.site_hash = std::stoull(std::string(obj.substr(c + 1)));
          }
        }
        size_t aid_pos = obj.find("\"arena_id\"");
        if (aid_pos != std::string_view::npos) {
          size_t c = obj.find(':', aid_pos);
          if (c != std::string_view::npos) {
            asgn.arena_id = static_cast<uint32_t>(
              std::stoul(std::string(obj.substr(c + 1))));
          }
        }
        size_t conf_pos = obj.find("\"confidence\"");
        if (conf_pos != std::string_view::npos) {
          size_t c = obj.find(':', conf_pos);
          if (c != std::string_view::npos) {
            asgn.confidence = std::stod(std::string(obj.substr(c + 1)));
          }
        }
        pol.arena_assignments.push_back(asgn);
        cur = obj_end + 1;
      }
    }
  }

  if (pol.arenas.empty()) {
    // If no arenas were explicitly defined, create a default general-purpose arena
    PolicyArena def_ar;
    def_ar.id = 1;
    def_ar.config.type = ArenaType::GeneralPurpose;
    def_ar.config.label = "default";
    pol.arenas.push_back(def_ar);
  }

  return pol;
  } catch (const std::exception& error) {
    g_last_policy_error = std::string("Invalid policy JSON: ") + error.what();
    return std::nullopt;
  } catch (...) {
    g_last_policy_error = "Invalid policy JSON";
    return std::nullopt;
  }
}

std::optional<AllocationPolicy> PolicyLoader::load(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    g_last_policy_error = "Could not open file: " + path;
    return std::nullopt;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return parse(ss.str());
}

} // namespace fragmentx

