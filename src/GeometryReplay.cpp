#include "GeometryReplay.hpp"

#include <algorithm>
#include <cmath>

#include "MBUtils.h"

namespace alog2media {
namespace {

std::string upper(std::string value) {
  return toupper(value);
}

void applyClear(VPlug_GeoShapes& shapes, std::string value) {
  std::string shape;
  std::string stype;
  for(std::string field : parseStringQ(value, ',')) {
    const std::string parameter = tolower(biteStringX(field, '='));
    if(parameter == "shape")
      shape = field;
    else if(parameter == "stype")
      stype = field;
  }
  shapes.clear(shape, stype);
}

}  // namespace

bool isGeometryVariable(const std::string& variable) {
  const std::string name = upper(variable);
  return name == "VIEW_POLYGON" || name == "VIEW_WEDGE" ||
         name == "VIEW_POINT" || name == "VIEW_VECTOR" ||
         name == "VIEW_CIRCLE" || name == "VIEW_OVAL" ||
         name == "VIEW_ARROW" || name == "VIEW_SEGLIST" ||
         name == "VIEW_SEGLR" || name == "VIEW_MARKER" ||
         name == "MARKER" || name == "VIEW_TEXTBOX" ||
         name == "VIEW_RANGE_PULSE" || name == "VIEW_COMMS_PULSE" ||
         name == "VIEW_GRID" || name == "VIEW_GRID_DELTA" ||
         name == "GRID_CONFIG" || name == "GRID_INIT" ||
         name == "GRID_DELTA" || name == "VIEW_VESSEL" ||
         name == "PMV_CLEAR";
}

bool applyGeometryEvent(VPlug_GeoShapes& shapes, const GeometryEvent& event,
                        double log_start_utc) {
  const std::string variable = upper(event.variable);
  const double timestamp = log_start_utc + event.time;

  if(variable == "PMV_CLEAR") {
    applyClear(shapes, event.value);
    return true;
  }
  if(variable == "VIEW_POINT")
    return shapes.addPoint(event.value, timestamp);
  if(variable == "VIEW_POLYGON")
    return shapes.addPolygon(event.value, timestamp);
  if(variable == "VIEW_VESSEL")
    return shapes.addVessel(event.value, timestamp);
  if(variable == "VIEW_SEGLIST")
    return shapes.addSegList(event.value, timestamp);
  if(variable == "VIEW_SEGLR")
    return shapes.addSeglr(event.value);
  if(variable == "VIEW_WEDGE")
    return shapes.addWedge(event.value);
  if(variable == "VIEW_VECTOR")
    return shapes.addVector(event.value);
  if(variable == "VIEW_CIRCLE")
    return shapes.addCircle(event.value, 18, timestamp);
  if(variable == "VIEW_OVAL")
    return shapes.addOval(event.value, 5, timestamp);
  if(variable == "VIEW_ARROW")
    return shapes.addArrow(event.value, timestamp);
  if(variable == "VIEW_RANGE_PULSE")
    return shapes.addRangePulse(event.value, timestamp);
  if(variable == "VIEW_COMMS_PULSE")
    return shapes.addCommsPulse(event.value, timestamp);
  if(variable == "VIEW_MARKER" || variable == "MARKER")
    return shapes.addMarker(event.value, timestamp);
  if(variable == "VIEW_TEXTBOX")
    return shapes.addTextBox(event.value, timestamp);
  if(variable == "GRID_CONFIG" || variable == "GRID_INIT")
    return shapes.addGrid(event.value);
  if(variable == "GRID_DELTA")
    return shapes.updateGrid(event.value);
  if(variable == "VIEW_GRID")
    return shapes.addConvexGrid(event.value);
  if(variable == "VIEW_GRID_DELTA")
    return shapes.updateConvexGrid(event.value);
  return false;
}

void includeGeometryBounds(Bounds& bounds, const VPlug_GeoShapes& shapes,
                           double utc_time,
                           const GeometryVisibility& visibility) {
  if(visibility.polygons) {
    for(const XYPolygon& shape : shapes.getPolygons())
      bounds.include(shape.get_min_x(), shape.get_max_x(), shape.get_min_y(),
                     shape.get_max_y());
  }
  if(visibility.vessels) {
    for(const XYVessel& shape : shapes.getVessels()) {
      const double radius = std::max(1.0, shape.getLen() / 2.0);
      bounds.include(shape.getX() - radius, shape.getX() + radius,
                     shape.getY() - radius, shape.getY() + radius);
    }
  }
  if(visibility.seglists) {
    for(const auto& [label, shape] : shapes.getSegLists()) {
      (void)label;
      bounds.include(shape.get_min_x(), shape.get_max_x(), shape.get_min_y(),
                     shape.get_max_y());
    }
  }
  if(visibility.seglrs) {
    for(const auto& [label, shape] : shapes.getSeglrs()) {
      (void)label;
      bounds.include(shape.getMinX(), shape.getMaxX(), shape.getMinY(),
                     shape.getMaxY());
    }
  }
  if(visibility.wedges) {
    for(const XYWedge& shape : shapes.getWedges())
      bounds.include(shape.getMinX(), shape.getMaxX(), shape.getMinY(),
                     shape.getMaxY());
  }
  if(visibility.hexagons) {
    for(const XYHexagon& shape : shapes.getHexagons())
      bounds.include(shape.get_min_x(), shape.get_max_x(), shape.get_min_y(),
                     shape.get_max_y());
  }
  if(visibility.grids) {
    for(const XYGrid& shape : shapes.getGrids()) {
      const XYSquare square = shape.getSBound();
      bounds.include(square.get_min_x(), square.get_max_x(),
                     square.get_min_y(), square.get_max_y());
    }
    for(const XYConvexGrid& shape : shapes.getConvexGrids()) {
      const XYSquare square = shape.getSBound();
      bounds.include(square.get_min_x(), square.get_max_x(),
                     square.get_min_y(), square.get_max_y());
    }
  }
  if(visibility.vectors) {
    for(const XYVector& shape : shapes.getVectors()) {
      bounds.include(shape.xpos(), shape.ypos());
      bounds.include(shape.xpos() + shape.xdot(),
                     shape.ypos() + shape.ydot());
    }
  }
  if(visibility.range_pulses) {
    for(const XYRangePulse& shape : shapes.getRangePulses()) {
      const double radius = shape.get_radius();
      bounds.include(shape.get_x() - radius, shape.get_x() + radius,
                     shape.get_y() - radius, shape.get_y() + radius);
    }
  }
  for(const XYCommsPulse& shape : shapes.getCommsPulses()) {
    const std::string type = shape.get_pulse_type();
    if((type == "msg" && !visibility.comms_messages) ||
       (type == "nrep" && !visibility.comms_node_reports) ||
       (type != "msg" && type != "nrep" &&
        !visibility.comms_messages && !visibility.comms_node_reports)) {
      continue;
    }
    const std::vector<double> triangle = shape.get_triangle(utc_time);
    for(std::size_t index = 0; index + 1 < triangle.size(); index += 2)
      bounds.include(triangle[index], triangle[index + 1]);
  }
  if(visibility.points) {
    for(const auto& [label, shape] : shapes.getPoints()) {
      (void)label;
      bounds.include(shape.x(), shape.y());
    }
  }
  if(visibility.markers) {
    for(const auto& [label, shape] : shapes.getMarkers()) {
      (void)label;
      const double radius =
          std::max(shape.get_width(), shape.get_range()) / 2.0;
      bounds.include(shape.get_vx() - radius, shape.get_vx() + radius,
                     shape.get_vy() - radius, shape.get_vy() + radius);
    }
  }
  if(visibility.circles) {
    for(const auto& [label, shape] : shapes.getCircles()) {
      (void)label;
      bounds.include(shape.get_min_x(), shape.get_max_x(), shape.get_min_y(),
                     shape.get_max_y());
    }
  }
  if(visibility.ovals) {
    for(const auto& [label, shape] : shapes.getOvals()) {
      (void)label;
      bounds.include(shape.get_min_x(), shape.get_max_x(), shape.get_min_y(),
                     shape.get_max_y());
    }
  }
  if(visibility.arrows) {
    for(const auto& [label, original] : shapes.getArrows()) {
      (void)label;
      XYArrow shape = original;
      bounds.include(shape.getMinX(), shape.getMaxX(), shape.getMinY(),
                     shape.getMaxY());
    }
  }
  if(visibility.textboxes) {
    for(const auto& [label, shape] : shapes.getTextBoxes()) {
      (void)label;
      bounds.include(shape.x(), shape.y());
    }
  }
}

Bounds geometryBounds(const std::vector<GeometryEvent>& events,
                      double log_start_utc, double start, double end,
                      const GeometryVisibility& visibility) {
  VPlug_GeoShapes shapes;
  Bounds bounds;
  std::size_t index = 0;
  while(index < events.size() && events[index].time <= start) {
    applyGeometryEvent(shapes, events[index], log_start_utc);
    ++index;
  }
  shapes.manageMemory(log_start_utc + start);
  includeGeometryBounds(bounds, shapes, log_start_utc + start, visibility);

  while(index < events.size() && events[index].time <= end) {
    applyGeometryEvent(shapes, events[index], log_start_utc);
    shapes.manageMemory(log_start_utc + events[index].time);
    includeGeometryBounds(bounds, shapes,
                          log_start_utc + events[index].time, visibility);
    ++index;
  }
  shapes.manageMemory(log_start_utc + end);
  includeGeometryBounds(bounds, shapes, log_start_utc + end, visibility);
  return bounds;
}

GeometryReplay::GeometryReplay(const std::vector<GeometryEvent>& events,
                               double log_start_utc)
    : events_(events), log_start_utc_(log_start_utc) {}

void GeometryReplay::reset() {
  shapes_ = VPlug_GeoShapes();
  current_time_ = -1;
  next_event_ = 0;
}

void GeometryReplay::advance(double log_time) {
  if(current_time_ >= 0 && log_time < current_time_)
    reset();
  while(next_event_ < events_.size() && events_[next_event_].time <= log_time) {
    applyGeometryEvent(shapes_, events_[next_event_], log_start_utc_);
    ++next_event_;
  }
  shapes_.manageMemory(log_start_utc_ + log_time);
  current_time_ = log_time;
}

}  // namespace alog2media
