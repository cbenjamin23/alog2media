#pragma once

#include "SceneTypes.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "NodeRecord.h"

namespace alog2media {

class NumericSeries {
 public:
  void add(double time, double value);
  void finalize();

  bool empty() const { return samples_.empty(); }
  double minTime() const;
  double maxTime() const;
  double valueAt(double time, bool angular = false) const;
  Bounds pairedBounds(const NumericSeries& other, double start,
                      double end) const;
  std::vector<std::pair<double, double>> pairedSamples(
      const NumericSeries& other, double start, double end,
      std::size_t maximum = 5000) const;

 private:
  std::vector<std::pair<double, double>> samples_;
};

class VehicleTrack {
 public:
  explicit VehicleTrack(std::string name = {});

  const std::string& name() const { return name_; }
  void setName(std::string value) { name_ = std::move(value); }
  void addReport(double time, const NodeRecord& record);
  void crossFillLatLon(double latitude_origin, double longitude_origin);
  void finalize();

  std::optional<NodeRecord> recordAt(double time) const;
  Bounds bounds(double start, double end) const;
  std::vector<std::pair<double, double>> trail(double start, double end) const;

  NumericSeries x;
  NumericSeries y;
  NumericSeries heading;

 private:
  struct TimedRecord {
    double time = 0;
    NodeRecord record;
  };

  std::string name_;
  std::vector<TimedRecord> reports_;
  std::string presentation_type_;
  std::string presentation_color_;
  std::optional<double> presentation_length_;
};

class ALogTimeline {
 public:
  static ALogTimeline load(const std::filesystem::path& path);

  double logStartUtc() const { return log_start_utc_; }
  double minTime() const { return min_time_; }
  double maxTime() const { return max_time_; }
  const std::string& regionInfo() const { return region_info_; }
  const std::string& community() const { return community_; }
  const std::map<std::string, VehicleTrack>& vehicles() const {
    return vehicles_;
  }
  const std::vector<GeometryEvent>& geometryEvents() const {
    return geometry_events_;
  }

  // Supply a mission datum when REGION_INFO did not contain one. Reports
  // which already carry local X/Y are left unchanged.
  void crossFillLatLon(double latitude_origin, double longitude_origin);
  Bounds sceneBounds(double start, double end, bool include_geometry) const;

 private:
  double log_start_utc_ = 0;
  double min_time_ = 0;
  double max_time_ = 0;
  std::string region_info_;
  std::string community_;
  std::map<std::string, VehicleTrack> vehicles_;
  std::vector<GeometryEvent> geometry_events_;
};

}  // namespace alog2media
