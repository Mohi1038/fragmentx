#pragma once

/// @file policy.hpp
/// @brief Allocation policy representation, loading, and serialization.
///
/// An AllocationPolicy is a JSON-serializable document that describes how
/// allocation sites should be routed to arenas. The FragmentX optimizer
/// generates policies; the runtime loader applies them.

#include <fragmentx/arena.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string_view>

namespace fragmentx {

// ─── SizeClassOverride ──────────────────────────────────────────────────────

/// @brief Overrides the default size class for a specific allocation size range.
struct SizeClassOverride {
  std::size_t min_size;        ///< Inclusive lower bound of size range.
  std::size_t max_size;        ///< Inclusive upper bound of size range.
  std::size_t target_class;    ///< Size to round up to (must be power of 2).
  uint32_t    target_arena_id; ///< Arena to route this size class to.
};

// ─── ArenaAssignment ────────────────────────────────────────────────────────

/// @brief Maps a single allocation site to a specific arena.
struct ArenaAssignment {
  uint64_t site_hash;   ///< FNV-1a hash of the allocation call site.
  uint32_t arena_id;    ///< Target arena ID.
  double   confidence;  ///< Optimizer's confidence in this assignment [0, 1].
};

// ─── PolicyArena ────────────────────────────────────────────────────────────

/// @brief Arena specification embedded in a policy file.
struct PolicyArena {
  uint32_t       id;      ///< Arena identifier (referenced by ArenaAssignment).
  ArenaConfig    config;  ///< Arena construction parameters.
  std::string    rationale; ///< Human-readable explanation from the optimizer.
};

// ─── AllocationPolicy ───────────────────────────────────────────────────────

/// @brief A complete allocation policy produced by the FragmentX optimizer.
///
/// Policies are stored as JSON files and loaded at runtime. The allocator's
/// routing layer uses the arena_assignments map to dispatch allocation
/// requests to the appropriate arena based on call-site hash.
struct AllocationPolicy {
  /// Policy schema version. Must match FRAGMENTX_POLICY_VERSION for loading.
  std::string version = "1.0";

  /// Human-readable name for this policy.
  std::string name;

  /// Brief description of the optimization strategy.
  std::string description;

  /// Strategy that generated this policy (e.g. "CompatibilityGrouped").
  std::string strategy;

  /// Optimizer cost function value J(P) for this policy. Lower is better.
  double cost_value = 0.0;

  /// Estimated improvement over the baseline (fraction, may be negative).
  double estimated_improvement = 0.0;

  /// Optimizer's confidence in the improvement estimate.
  double confidence = 0.0;

  /// Arena definitions.
  std::vector<PolicyArena> arenas;

  /// Allocation site → arena routing table.
  std::vector<ArenaAssignment> arena_assignments;

  /// Size class overrides (optional refinements).
  std::vector<SizeClassOverride> size_class_overrides;

  /// ISO 8601 timestamp when this policy was generated.
  std::string generated_at;

  /// FragmentX version that generated this policy.
  std::string generator_version;

  /// Arbitrary metadata key-value pairs (platform, workload description, etc.).
  std::unordered_map<std::string, std::string> metadata;

  // ── Helpers ───────────────────────────────────────────────────────────

  /// @return Arena ID for the given site hash, or nullopt if unassigned.
  [[nodiscard]] std::optional<uint32_t>
  arena_for_site(uint64_t site_hash) const noexcept;

  /// @return true if this policy contains at least one arena and one assignment.
  [[nodiscard]] bool is_valid() const noexcept {
    return !arenas.empty() && !arena_assignments.empty();
  }
};

// ─── PolicyLoader ───────────────────────────────────────────────────────────

/// @brief Loads an AllocationPolicy from a JSON file.
class PolicyLoader {
public:
  /// @brief Load a policy from the given file path.
  /// @param path  Absolute or relative path to a .json policy file.
  /// @return      The loaded policy, or an empty optional on failure.
  [[nodiscard]] static std::optional<AllocationPolicy>
  load(const std::string& path);

  /// @brief Parse a policy from a JSON string in memory.
  /// @param json_text  Null-terminated JSON text.
  /// @return           The parsed policy, or empty on parse/validation failure.
  [[nodiscard]] static std::optional<AllocationPolicy>
  parse(std::string_view json_text);

  /// @return The last error message, or empty if the last operation succeeded.
  [[nodiscard]] static const std::string& last_error() noexcept;
};

// ─── PolicySerializer ───────────────────────────────────────────────────────

/// @brief Serializes an AllocationPolicy to JSON.
class PolicySerializer {
public:
  /// @brief Write a policy to a JSON file.
  /// @param policy  The policy to serialize.
  /// @param path    Output file path. File is created or overwritten.
  /// @return        true on success.
  [[nodiscard]] static bool
  save(const AllocationPolicy& policy, const std::string& path);

  /// @brief Serialize a policy to a JSON string.
  /// @param policy   The policy to serialize.
  /// @param pretty   When true, produce indented human-readable JSON.
  /// @return         JSON string, or empty on error.
  [[nodiscard]] static std::string
  to_json(const AllocationPolicy& policy, bool pretty = true);
};

} // namespace fragmentx

