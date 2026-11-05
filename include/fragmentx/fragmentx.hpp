#pragma once

/// @file fragmentx.hpp
/// @brief Master include for the FragmentX library.
///
/// Include this header to access all public FragmentX APIs in a single step.
/// Individual module headers may be included directly for finer-grained control.

#include <fragmentx/config.hpp>
#include <fragmentx/allocator.hpp>
#include <fragmentx/arena.hpp>
#include <fragmentx/policy.hpp>
#include <fragmentx/lifetime.hpp>
#include <fragmentx/statistics.hpp>
#include <fragmentx/capture.hpp>
#include <fragmentx/trace.hpp>
#include <fragmentx/workload.hpp>
#include <fragmentx/fragmentation.hpp>
#include <fragmentx/optimizer.hpp>

/// @namespace fragmentx
/// @brief Root namespace for all FragmentX APIs.
namespace fragmentx {

/// @brief Library version as a human-readable string.
/// @return Null-terminated string of the form "0.1.0".
[[nodiscard]] constexpr const char* version_string() noexcept;

/// @brief Library version encoded as a single integer: (MAJOR*10000 + MINOR*100 + PATCH).
[[nodiscard]] constexpr int version_number() noexcept;

} // namespace fragmentx

// ─── Inline implementations ─────────────────────────────────────────────────
#include <fragmentx/version.hpp>

namespace fragmentx {

constexpr const char* version_string() noexcept {
  return FRAGMENTX_VERSION;
}

constexpr int version_number() noexcept {
  return FRAGMENTX_VERSION_MAJOR * 10000
       + FRAGMENTX_VERSION_MINOR * 100
       + FRAGMENTX_VERSION_PATCH;
}

} // namespace fragmentx

