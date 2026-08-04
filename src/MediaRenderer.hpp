#pragma once

#include "Options.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace alog2media {

enum class WarpSource { explicit_option, mission, fallback };

struct RenderMetadata {
  double log_min = 0;
  double log_max = 0;
  double start = 0;
  double end = 0;
  double warp = 1;
  WarpSource warp_source = WarpSource::fallback;
  std::string map;
  std::string backend;
  std::optional<std::string> mission;
  bool used_region_info = false;
  bool used_mission = false;
  bool discovered_mission = false;
  bool fit_view = false;
  std::size_t vehicle_count = 0;
  std::size_t geometry_event_count = 0;
};

class MediaRenderer {
 public:
  explicit MediaRenderer(const Options& options);
  ~MediaRenderer();

  MediaRenderer(const MediaRenderer&) = delete;
  MediaRenderer& operator=(const MediaRenderer&) = delete;

  const RenderMetadata& metadata() const;
  std::vector<std::uint8_t> render(double log_time);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace alog2media
