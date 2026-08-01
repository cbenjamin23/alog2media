#pragma once

#include "SceneTypes.hpp"

#include <cstddef>
#include <vector>

#include "VPlug_GeoShapes.h"

namespace alog2media {

struct GeometryVisibility {
  bool polygons = true;
  bool vessels = true;
  bool seglists = true;
  bool seglrs = true;
  bool wedges = true;
  bool hexagons = true;
  bool grids = true;
  bool vectors = true;
  bool range_pulses = true;
  bool comms_messages = true;
  bool comms_node_reports = true;
  bool points = true;
  bool markers = true;
  bool circles = true;
  bool ovals = true;
  bool arrows = true;
  bool textboxes = true;
};

bool isGeometryVariable(const std::string& variable);
bool applyGeometryEvent(VPlug_GeoShapes& shapes, const GeometryEvent& event,
                        double log_start_utc);
void includeGeometryBounds(Bounds& bounds, const VPlug_GeoShapes& shapes,
                           double utc_time,
                           const GeometryVisibility& visibility = {});
Bounds geometryBounds(const std::vector<GeometryEvent>& events,
                      double log_start_utc, double start, double end,
                      const GeometryVisibility& visibility = {});

class GeometryReplay {
 public:
  GeometryReplay(const std::vector<GeometryEvent>& events,
                 double log_start_utc);

  void advance(double log_time);
  const VPlug_GeoShapes& shapes() const { return shapes_; }

 private:
  void reset();

  const std::vector<GeometryEvent>& events_;
  double log_start_utc_ = 0;
  double current_time_ = -1;
  std::size_t next_event_ = 0;
  VPlug_GeoShapes shapes_;
};

}  // namespace alog2media
