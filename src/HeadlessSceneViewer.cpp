#include "HeadlessSceneViewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>

#include "ColorPack.h"
#include "MBUtils.h"
#include "MacroUtils.h"
#include "XYSegList.h"

namespace alog2media {
namespace {

std::string normalized(std::string value) {
  return tolower(stripBlankEnds(value));
}

std::optional<double> parseNumber(const std::string& value) {
  std::size_t used = 0;
  try {
    const double parsed = std::stod(stripBlankEnds(value), &used);
    if(used == stripBlankEnds(value).size() && std::isfinite(parsed))
      return parsed;
  } catch(const std::exception&) {
  }
  return std::nullopt;
}

std::optional<double> regionNumber(const std::string& region,
                                   const std::string& wanted) {
  for(std::string field : parseStringQ(region, ',')) {
    const std::string name = normalized(biteStringX(field, '='));
    if(name == wanted)
      return parseNumber(field);
  }
  return std::nullopt;
}

template <typename Container>
Container withoutLabels(Container shapes) {
  for(auto& shape : shapes) {
    shape.set_label("");
    shape.set_msg("");
  }
  return shapes;
}

template <typename Shape>
std::map<std::string, Shape> withoutMapLabels(
    const std::map<std::string, Shape>& source) {
  std::map<std::string, Shape> shapes = source;
  for(auto& [key, shape] : shapes) {
    (void)key;
    shape.set_label("");
    shape.set_msg("");
  }
  return shapes;
}

void restoreMatrixStack(GLenum mode, GLenum depth_query, GLint target_depth) {
  glMatrixMode(mode);
  GLint depth = 0;
  glGetIntegerv(depth_query, &depth);
  while(depth > target_depth) {
    glPopMatrix();
    --depth;
  }
}

}  // namespace

HeadlessSceneViewer::HeadlessSceneViewer(int width, int height,
                                         const ALogTimeline& timeline)
    : MarineViewer(0, 0, width, height),
      timeline_(timeline),
      geometry_(timeline.geometryEvents(), timeline.logStartUtc()),
      target_width_(width),
      target_height_(height),
      log_time_(timeline.minTime()),
      geometry_visible_(true),
      labels_visible_(true),
      trail_policy_(TrailPolicy::configured),
      trail_window_seconds_(0),
      vehicle_name_mode_(m_vehi_settings.getVehiclesNameMode()) {
  if(width <= 0 || height <= 0)
    throw std::invalid_argument("headless scene dimensions must be positive");

  if(timeline_.vehicles().count(timeline_.community()) != 0)
    active_vehicle_ = timeline_.community();
  else if(!timeline_.vehicles().empty())
    active_vehicle_ = timeline_.vehicles().begin()->first;

  geometry_.advance(log_time_);
}

void HeadlessSceneViewer::draw() {
  drawScene();
}

int HeadlessSceneViewer::handle(int) {
  return 0;
}

bool HeadlessSceneViewer::setParam(std::string name, std::string value) {
  const std::string parameter = normalized(name);
  if(MarineViewer::setParam(name, value)) {
    if(parameter == "vehicle_name_viewable" ||
       parameter == "vehicles_name_viewable" ||
       parameter == "vehicles_name_mode") {
      const std::string mode = m_vehi_settings.getVehiclesNameMode();
      if(mode != "off")
        vehicle_name_mode_ = mode;
    }
    return true;
  }

  if(parameter == "op_vertex")
    return m_op_area.addVertex(value, m_geodesy);
  if(parameter == "active_vehicle_name") {
    setActiveVehicle(value);
    return true;
  }
  if(parameter == "geometry_viewable") {
    const std::string setting = normalized(value);
    if(setting == "true" || setting == "on") {
      setGeometryVisible(true);
      return true;
    }
    if(setting == "false" || setting == "off") {
      setGeometryVisible(false);
      return true;
    }
    return false;
  }
  if(parameter == "labels_viewable") {
    const std::string setting = normalized(value);
    if(setting == "true" || setting == "on") {
      setLabelsVisible(true);
      return true;
    }
    if(setting == "false" || setting == "off") {
      setLabelsVisible(false);
      return true;
    }
    return false;
  }

  return m_op_area.setParam(parameter, value);
}

bool HeadlessSceneViewer::setParam(std::string name, double value) {
  const std::string parameter = normalized(name);
  if(parameter == "curr_time") {
    setTime(value);
    return true;
  }
  if(parameter == "trail_window")
    return useTrailWindow(value);

  bool handled = MarineViewer::setParam(parameter, value);
  handled = m_vehi_settings.setParam(parameter, value) || handled;
  return handled;
}

void HeadlessSceneViewer::setTime(double log_time) {
  if(!std::isfinite(log_time))
    throw std::invalid_argument("scene time must be finite");
  log_time_ = log_time;
  geometry_.advance(log_time_);
}

bool HeadlessSceneViewer::prepareMap() {
  if(!m_textures_init)
    applyTiffFiles();
  return mapReady();
}

bool HeadlessSceneViewer::mapReady() const {
  return m_back_img.get_img_pix_width() > 0 &&
         m_back_img.get_img_pix_height() > 0 &&
         m_back_img.get_img_mtr_width() > 0 &&
         m_back_img.get_img_mtr_height() > 0;
}

std::string HeadlessSceneViewer::resolvedMap() const {
  return getTiffFileCurrent();
}

void HeadlessSceneViewer::setupViewport() {
  if(!m_textures_init)
    applyTiffFiles();

  autoZoom();
  clearBackground();
  glViewport(0, 0, target_width_, target_height_);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, w(), 0, h(), -1, 1);

  const double image_width = m_back_img.get_img_pix_width();
  const double image_height = m_back_img.get_img_pix_height();
  const double shape_width = image_width * m_zoom;
  const double shape_height = image_height * m_zoom;
  const double shift_x = m_vshift_x * m_zoom;
  const double shift_y = m_vshift_y * m_zoom;

  m_x_origin = -shape_width / 2 + shift_x;
  m_y_origin = -shape_height / 2 + shift_y;

  if(m_geo_settings.viewable("tiff_viewable"))
    drawTiff();
  if(m_main_window == nullptr)
    m_main_window = Fl_Window::current();
}

void HeadlessSceneViewer::drawScene() {
  GLint matrix_mode = GL_MODELVIEW;
  GLint modelview_depth = 0;
  GLint projection_depth = 0;
  glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
  glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview_depth);
  glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection_depth);

  geometry_.advance(log_time_);
  setupViewport();

  if(m_geo_settings.viewable("hash_viewable"))
    drawFastHash();

  if(geometry_visible_) {
    drawGeometry();
    drawOpArea(m_op_area);
    drawDatum(m_op_area);
    drawDropPoints();
  }
  drawTrails();
  drawVehicles();

  glFlush();

  // MarineViewer::drawCommonVehicle() may return while a modelview matrix is
  // still pushed when an offscreen vehicle label is culled. Keep every frame
  // at the matrix boundary owned by the caller.
  restoreMatrixStack(GL_MODELVIEW, GL_MODELVIEW_STACK_DEPTH, modelview_depth);
  restoreMatrixStack(GL_PROJECTION, GL_PROJECTION_STACK_DEPTH,
                     projection_depth);
  glMatrixMode(static_cast<GLenum>(matrix_mode));
}

void HeadlessSceneViewer::drawGeometry() {
  const VPlug_GeoShapes& shapes = geometry_.shapes();
  const double timestamp = utcTime();

  if(labels_visible_) {
    drawPolygons(shapes.getPolygons(), timestamp);
    drawVessels(shapes.getVessels(), timestamp);
    for(const XYHexagon& hexagon : shapes.getHexagons())
      drawPolygon(hexagon);
    drawGrids(shapes.getGrids());
    drawConvexGrids(shapes.getConvexGrids());
    drawSegLists(shapes.getSegLists(), timestamp);
    drawSeglrs(shapes.getSeglrs(), timestamp);
    drawCircles(shapes.getCircles(), timestamp);
    drawOvals(shapes.getOvals(), timestamp);
    drawArrows(shapes.getArrows(), timestamp);
    drawPoints(shapes.getPoints(), timestamp);
    drawVectors(shapes.getVectors());
    drawWedges(shapes.getWedges());
    drawRangePulses(shapes.getRangePulses(), timestamp);
    drawCommsPulses(shapes.getCommsPulses(), timestamp);
    drawMarkers(shapes.getMarkers(), timestamp);
    drawTextBoxes(shapes.getTextBoxes(), timestamp);
    return;
  }

  drawPolygons(withoutLabels(shapes.getPolygons()), timestamp);
  drawVessels(withoutLabels(shapes.getVessels()), timestamp);
  for(XYHexagon hexagon : withoutLabels(shapes.getHexagons()))
    drawPolygon(hexagon);
  drawGrids(shapes.getGrids());
  drawConvexGrids(shapes.getConvexGrids());
  drawSegLists(withoutMapLabels(shapes.getSegLists()), timestamp);
  drawSeglrs(withoutMapLabels(shapes.getSeglrs()), timestamp);
  drawCircles(withoutMapLabels(shapes.getCircles()), timestamp);
  drawOvals(withoutMapLabels(shapes.getOvals()), timestamp);
  drawArrows(withoutMapLabels(shapes.getArrows()), timestamp);
  drawPoints(withoutMapLabels(shapes.getPoints()), timestamp);
  drawVectors(withoutLabels(shapes.getVectors()));
  drawWedges(withoutLabels(shapes.getWedges()));
  drawRangePulses(withoutLabels(shapes.getRangePulses()), timestamp);
  drawCommsPulses(withoutLabels(shapes.getCommsPulses()), timestamp);
  drawMarkers(withoutMapLabels(shapes.getMarkers()), timestamp);

  // A VIEW_TEXTBOX's text is its payload, not an incidental label.
  drawTextBoxes(shapes.getTextBoxes(), timestamp);
}

void HeadlessSceneViewer::drawTrails() {
  if(!m_vehi_settings.isViewableVehicles() ||
     !m_vehi_settings.isViewableTrails()) {
    return;
  }
  for(const auto& [name, track] : timeline_.vehicles()) {
    (void)name;
    drawTrail(track);
  }
}

void HeadlessSceneViewer::drawTrail(const VehicleTrack& track) {
  double start = timeline_.minTime();
  if(trail_policy_ == TrailPolicy::window)
    start = std::max(start, log_time_ - trail_window_seconds_);

  std::vector<std::pair<double, double>> points =
      track.trail(start, log_time_);
  if(points.empty())
    return;

  if(trail_policy_ == TrailPolicy::configured) {
    const std::size_t length = m_vehi_settings.getTrailsLength();
    if(length == 0)
      return;
    if(points.size() > length)
      points.erase(points.begin(), points.end() - length);
  }

  XYSegList trail;
  for(const auto& [x, y] : points)
    trail.add_vertex(x, y);

  const ColorPack color = m_vehi_settings.getColorTrails();
  trail.set_label("trails");
  trail.set_color("vertex", color.str());
  trail.set_color("label", "invisible");
  trail.set_vertex_size(m_vehi_settings.getTrailsPointSize());
  trail.set_color("edge", m_vehi_settings.isViewableTrailsConnect()
                              ? "white"
                              : "invisible");
  drawSegList(trail);
}

void HeadlessSceneViewer::drawVehicles() {
  if(!m_vehi_settings.isViewableVehicles())
    return;
  for(const auto& [name, track] : timeline_.vehicles()) {
    (void)name;
    drawVehicle(track);
  }
}

void HeadlessSceneViewer::drawVehicle(const VehicleTrack& track) {
  const double first_position = std::max(track.x.minTime(), track.y.minTime());
  if(track.x.empty() || track.y.empty() || log_time_ < first_position)
    return;

  const double last_position = std::max(track.x.maxTime(), track.y.maxTime());
  const double age = std::max(0.0, log_time_ - last_position);
  if(age > m_vehi_settings.getStaleRemoveThresh())
    return;

  std::optional<NodeRecord> sample = track.recordAt(log_time_);
  if(!sample)
    return;
  NodeRecord record = *sample;

  const bool active = track.name() == active_vehicle_;
  ColorPack body_color = active
                             ? m_vehi_settings.getColorActiveVehicle()
                             : m_vehi_settings.getColorInactiveVehicle();
  if(!record.getColor().empty())
    body_color.setColor(record.getColor());

  const ColorPack name_color = m_vehi_settings.getColorVehicleName();
  const std::string names_mode = m_vehi_settings.getVehiclesNameMode();
  bool draw_name = labels_visible_ && names_mode != "off";
  std::string display_name = track.name();

  if(names_mode == "names+mode") {
    const std::string mode = record.getMode();
    const std::string allstop = record.getAllStop();
    if(mode != "none" && mode != "unknown-mode")
      display_name += " (" + mode + ")";
    if(allstop != "clear")
      display_name += " (" + allstop + ")";
  } else if(names_mode == "names+shortmode") {
    std::string mode = record.getMode();
    const std::string allstop = record.getAllStop();
    if(mode != "none" && mode != "unknown-mode")
      display_name += " (" + modeShorten(mode) + ")";
    if(allstop != "clear" && allstop != "n/a")
      display_name += " (" + allstop + ")";
  } else if(names_mode == "names+auxmode") {
    const std::string mode = record.getModeAux();
    display_name += mode.empty() ? " (no auxmode info)" : " (" + mode + ")";
  } else if(names_mode == "names+depth") {
    display_name += " (depth=" + doubleToStringX(record.getDepth(), 1) + ")";
  }

  if(age > m_vehi_settings.getStaleReportThresh()) {
    display_name = track.name() + "(Stale Report: " +
                   doubleToString(age, 0) + ")";
  }

  record.setName(display_name);
  record.setLength(record.getLength() *
                   m_vehi_settings.getVehiclesShapeScale());
  drawCommonVehicle(record, body_color, name_color, draw_name, 1,
                    record.getTransparency());
}

bool HeadlessSceneViewer::fitToBounds(const Bounds& bounds, double padding) {
  if(!bounds.valid() || !mapReady() || !std::isfinite(padding))
    return false;

  padding = std::clamp(padding, 0.0, 0.45);
  const double center_x = (bounds.min_x + bounds.max_x) / 2;
  const double center_y = (bounds.min_y + bounds.max_y) / 2;
  const double pixels_per_meter_x = m_back_img.get_pix_per_mtr_x();
  const double pixels_per_meter_y = m_back_img.get_pix_per_mtr_y();
  if(pixels_per_meter_x <= 0 || pixels_per_meter_y <= 0)
    return false;

  m_vshift_x = -pixels_per_meter_x *
               (center_x - m_back_img.get_x_at_img_ctr());
  m_vshift_y = -pixels_per_meter_y *
               (center_y - m_back_img.get_y_at_img_ctr());

  const double range_x = std::max(bounds.max_x - bounds.min_x, 20.0);
  const double range_y = std::max(bounds.max_y - bounds.min_y, 20.0);
  const double usable = 1.0 - 2.0 * padding;
  const double fit_x = (static_cast<double>(target_width_) * usable) /
                       (range_x * pixels_per_meter_x);
  const double fit_y = (static_cast<double>(target_height_) * usable) /
                       (range_y * pixels_per_meter_y);
  m_zoom = std::max(0.00001, std::min(fit_x, fit_y));
  return true;
}

bool HeadlessSceneViewer::fitToScene(double start, double end,
                                     bool include_geometry, double padding) {
  if(!std::isfinite(start) || !std::isfinite(end) || end < start)
    return false;

  Bounds bounds = timeline_.sceneBounds(start, end, false);
  if(include_geometry && geometry_visible_) {
    GeometryVisibility visibility;
    visibility.polygons =
        m_geo_settings.viewable("polygon_viewable_all", true);
    visibility.seglists =
        m_geo_settings.viewable("seglist_viewable_all", true);
    visibility.seglrs = m_geo_settings.viewable("seglr_viewable_all", true);
    visibility.wedges =
        m_geo_settings.viewable("wedges_viewable_all", true);
    visibility.grids = m_geo_settings.viewable("grid_viewable_all", true);
    visibility.vectors =
        m_geo_settings.viewable("vector_viewable_all", true);
    visibility.range_pulses =
        m_geo_settings.viewable("pulses_viewable_all", true);
    visibility.comms_messages =
        m_geo_settings.viewable("comms_pulse_viewable_all", true);
    visibility.comms_node_reports =
        m_geo_settings.viewable("node_pulse_viewable_all", true);
    visibility.points = m_geo_settings.viewable("point_viewable_all", true);
    visibility.markers =
        m_geo_settings.viewable("marker_viewable_all", true);
    visibility.circles =
        m_geo_settings.viewable("circle_viewable_all", true);
    visibility.ovals = m_geo_settings.viewable("oval_viewable_all", true);
    visibility.arrows = m_geo_settings.viewable("arrow_viewable_all", true);
    visibility.textboxes =
        m_geo_settings.viewable("tbox_viewable_all", true);
    bounds.include(geometryBounds(timeline_.geometryEvents(),
                                  timeline_.logStartUtc(), start, end,
                                  visibility));

    if(m_geo_settings.viewable("oparea_viewable_all", true)) {
      for(unsigned int index = 0; index < m_op_area.size(); ++index)
        bounds.include(m_op_area.getXPos(index), m_op_area.getYPos(index));
    }
    if(m_geo_settings.viewable("datum_viewable", false))
      bounds.include(0, 0);
    if(m_geo_settings.viewable("drop_point_viewable_all", true)) {
      for(unsigned int index = 0; index < m_drop_points.size(); ++index) {
        const XYPoint point = m_drop_points.getPoint(index);
        bounds.include(point.x(), point.y());
      }
    }
  }
  return fitToBounds(bounds, padding);
}

bool HeadlessSceneViewer::applyMissionParam(const std::string& name,
                                            const std::string& value) {
  if(setParam(name, value))
    return true;
  const std::optional<double> numeric = parseNumber(value);
  return numeric && setParam(name, *numeric);
}

std::vector<std::string> HeadlessSceneViewer::applyMissionParams(
    const std::vector<std::pair<std::string, std::string>>& params) {
  std::vector<std::string> unhandled;
  for(const auto& [name, value] : params) {
    if(!applyMissionParam(name, value))
      unhandled.push_back(name);
  }
  return unhandled;
}

bool HeadlessSceneViewer::applyRegionInfoCamera(
    const std::string& region_info) {
  const std::optional<double> zoom = regionNumber(region_info, "zoom");
  const std::optional<double> pan_x = regionNumber(region_info, "pan_x");
  const std::optional<double> pan_y = regionNumber(region_info, "pan_y");
  if(!zoom || !pan_x || !pan_y || *zoom <= 0)
    return false;
  setParam("set_zoom", *zoom);
  setParam("set_pan_x", *pan_x);
  setParam("set_pan_y", *pan_y);
  return true;
}

void HeadlessSceneViewer::setActiveVehicle(std::string name) {
  active_vehicle_ = stripBlankEnds(name);
}

void HeadlessSceneViewer::setGridVisible(bool visible) {
  MarineViewer::setParam("hash_viewable", visible ? "true" : "false");
}

void HeadlessSceneViewer::setGeometryVisible(bool visible) {
  geometry_visible_ = visible;
  if(!visible)
    return;

  // An explicit CLI "on" has higher precedence than mission family toggles.
  const char* families[] = {
      "polygon_viewable_all", "seglist_viewable_all",
      "seglr_viewable_all",   "marker_viewable_all",
      "point_viewable_all",   "vector_viewable_all",
      "grid_viewable_all",    "circle_viewable_all",
      "wedges_viewable_all",  "oval_viewable_all",
      "arrow_viewable_all",   "tbox_viewable_all",
      "oparea_viewable_all",  "drop_point_viewable_all",
      "pulses_viewable_all", "range_pulse_viewable_all",
      "comms_pulse_viewable_all",
      "node_pulse_viewable_all"};
  for(const char* family : families)
    MarineViewer::setParam(family, "true");
  m_op_area.setParam("op_area_viewable_all", "true");
}

void HeadlessSceneViewer::setLabelsVisible(bool visible) {
  labels_visible_ = visible;
  if(!visible) {
    const std::string current = m_vehi_settings.getVehiclesNameMode();
    if(current != "off")
      vehicle_name_mode_ = current;
    m_vehi_settings.setParam("vehicles_name_mode", "off");
  } else {
    const std::string restored = vehicle_name_mode_.empty()
                                     ? "names+shortmode"
                                     : vehicle_name_mode_;
    m_vehi_settings.setParam("vehicles_name_mode", restored);
  }

  const char* families[] = {
      "polygon_viewable_labels", "seglist_viewable_labels",
      "seglr_viewable_labels",   "marker_viewable_labels",
      "point_viewable_labels",   "vector_viewable_labels",
      "grid_viewable_labels",    "circle_viewable_labels",
      "wedge_viewable_labels",   "arrow_viewable_labels",
      "oparea_viewable_labels"};
  for(const char* family : families)
    MarineViewer::setParam(family, visible ? "true" : "false");
  m_op_area.setParam("op_area_viewable_labels", visible ? "true" : "false");
}

void HeadlessSceneViewer::setTrailsVisible(bool visible) {
  m_vehi_settings.setParam("trails_viewable", visible ? "true" : "false");
}

void HeadlessSceneViewer::useConfiguredTrails() {
  trail_policy_ = TrailPolicy::configured;
}

void HeadlessSceneViewer::useFullTrails() {
  trail_policy_ = TrailPolicy::full;
  setTrailsVisible(true);
}

bool HeadlessSceneViewer::useTrailWindow(double seconds) {
  if(!std::isfinite(seconds) || seconds < 0)
    return false;
  trail_policy_ = TrailPolicy::window;
  trail_window_seconds_ = seconds;
  setTrailsVisible(true);
  return true;
}

std::optional<double> HeadlessSceneViewer::trailWindow() const {
  if(trail_policy_ != TrailPolicy::window)
    return std::nullopt;
  return trail_window_seconds_;
}

}  // namespace alog2media
