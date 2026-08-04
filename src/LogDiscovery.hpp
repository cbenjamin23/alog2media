#pragma once

#include <filesystem>

namespace alog2media {

struct LogDiscoveryResult {
  std::filesystem::path path;
  bool used_mtime_fallback = false;
};

// Search ROOT and at most two nested directory levels for regular .alog files.
// Symlinks are never followed. MISSION_HASH identifies and ranks runs when it
// is available; older logs fall back to modification-time grouping. Log
// contents then select the run's unambiguous scene-bearing member.
LogDiscoveryResult discoverLatestLogWithDetails(
    const std::filesystem::path& root);

std::filesystem::path discoverLatestLog(const std::filesystem::path& root);

bool hasALogExtension(const std::filesystem::path& path);

}  // namespace alog2media
