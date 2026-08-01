#pragma once

#include "ALogTimeline.hpp"
#include "GeometryReplay.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "MarineViewer.h"

namespace alog2media {

// A pMarineViewer-compatible scene compositor which uses Fl_Gl_Window only as
// the upstream MarineViewer state holder. The caller owns the OpenGL context,
// framebuffer, timeline, and frame readback.
class HeadlessSceneViewer final : public MarineViewer {
 public:
  enum class TrailPolicy { configured, full, window };

  HeadlessSceneViewer(int width, int height, const ALogTimeline& timeline);

  void draw() override;
  int handle(int event) override;
  bool setParam(std::string name, std::string value = "") override;
  bool setParam(std::string name, double value) override;

  // The time supplied here is the relative timestamp used in the .alog.
  void setTime(double log_time);
  double time() const { return log_time_; }
  double utcTime() const { return timeline_.logStartUtc() + log_time_; }

  // Draws to the framebuffer which is current on the calling thread.
  void drawScene();

  // Map texture creation also requires a current OpenGL context.
  bool prepareMap();
  bool mapReady() const;
  std::string resolvedMap() const;

  bool fitToBounds(const Bounds& bounds, double padding = 0.05);
  bool fitToScene(double start, double end, bool include_geometry = true,
                  double padding = 0.05);

  // Apply one or more entries from ProcessConfig = pMarineViewer. The bulk
  // form returns the names which this viewport does not understand (GUI-only
  // pMarineViewer options are intentionally outside this class).
  bool applyMissionParam(const std::string& name, const std::string& value);
  std::vector<std::string> applyMissionParams(
      const std::vector<std::pair<std::string, std::string>>& params);

  // REGION_INFO reports absolute camera state, unlike the multiplicative
  // pMarineViewer "zoom" mission parameter.
  bool applyRegionInfoCamera(const std::string& region_info);

  void setActiveVehicle(std::string name);
  const std::string& activeVehicle() const { return active_vehicle_; }

  void setGridVisible(bool visible);
  void setGeometryVisible(bool visible);
  void setLabelsVisible(bool visible);
  void setTrailsVisible(bool visible);

  // Configured mode preserves the mission's trails_viewable setting and uses
  // its trails_length. Full/window are explicit overrides and enable trails.
  void useConfiguredTrails();
  void useFullTrails();
  bool useTrailWindow(double seconds);
  TrailPolicy trailPolicy() const { return trail_policy_; }
  std::optional<double> trailWindow() const;

 private:
  void setupViewport();
  void drawGeometry();
  void drawTrails();
  void drawVehicles();
  void drawTrail(const VehicleTrack& track);
  void drawVehicle(const VehicleTrack& track);

  const ALogTimeline& timeline_;
  GeometryReplay geometry_;
  int target_width_;
  int target_height_;
  double log_time_;
  bool geometry_visible_;
  bool labels_visible_;
  TrailPolicy trail_policy_;
  double trail_window_seconds_;
  std::string active_vehicle_;
  std::string vehicle_name_mode_;
};

}  // namespace alog2media
