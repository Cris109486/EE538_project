#pragma once

#include <cstdint>
#include <optional>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace ee538 {

// Process high-water RSS, not simulated KV-cache size.
inline std::optional<std::uint64_t> peak_process_rss_bytes() {
#if defined(__linux__) || defined(__APPLE__)
  struct rusage usage{};

  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss <= 0) {
    return std::nullopt;
  }

  auto bytes = static_cast<std::uint64_t>(usage.ru_maxrss);

#if defined(__linux__)
  // Linux reports KiB; macOS reports bytes.
  bytes *= 1024;
#endif

  return bytes;
#else
  return std::nullopt;
#endif
}

} // namespace ee538