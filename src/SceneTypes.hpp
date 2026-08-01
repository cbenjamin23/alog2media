#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace alog2media {

struct Bounds {
  double min_x = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();

  bool valid() const {
    return std::isfinite(min_x) && std::isfinite(max_x) &&
           std::isfinite(min_y) && std::isfinite(max_y);
  }

  void include(double x, double y) {
    if(!std::isfinite(x) || !std::isfinite(y))
      return;
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  }

  void include(double low_x, double high_x, double low_y, double high_y) {
    include(low_x, low_y);
    include(high_x, high_y);
  }

  void include(const Bounds& other) {
    if(other.valid())
      include(other.min_x, other.max_x, other.min_y, other.max_y);
  }
};

struct GeometryEvent {
  double time = 0;
  std::string variable;
  std::string value;
  std::string community;
};

}  // namespace alog2media
