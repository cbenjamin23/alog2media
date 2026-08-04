#pragma once

#include <filesystem>

namespace alog2media {

// Search ROOT and at most two nested directory levels for regular .alog files.
// Symlinks are never followed. The newest completed run is identified by file
// modification time, then log contents select its unambiguous scene-bearing
// member.
std::filesystem::path discoverLatestLog(const std::filesystem::path& root);

bool hasALogExtension(const std::filesystem::path& path);

}  // namespace alog2media
