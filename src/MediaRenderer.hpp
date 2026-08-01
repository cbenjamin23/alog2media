#pragma once

#include "Options.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace alog2media {

struct RenderMetadata {
  double log_min = 0;
  double log_max = 0;
  double start = 0;
  double end = 0;
  std::string map;
  std::string backend;
  bool used_region_info = false;
  bool used_mission = false;
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
