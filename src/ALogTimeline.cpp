#include "ALogTimeline.hpp"

#include "GeometryReplay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "NodeRecordUtils.h"
#include "MBUtils.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"

namespace alog2media {
namespace {

std::string trimLeft(std::string value) {
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isspace(ch);
              }));
  return value;
}

std::optional<double> number(const std::string& value) {
  const std::string trimmed = stripBlankEnds(value);
  std::size_t used = 0;
  try {
    const double parsed = std::stod(trimmed, &used);
    if(used == trimmed.size() && std::isfinite(parsed))
      return parsed;
  } catch(const std::exception&) {
  }
  return std::nullopt;
}

std::string communityFromDbSource(const std::string& source) {
  constexpr const char* prefix = "MOOSDB_";
  if(source.rfind(prefix, 0) == 0 && source.size() > 7)
    return source.substr(7);
  return {};
}

bool isLocalNodeReport(const std::string& variable) {
  return variable == "NODE_REPORT_LOCAL" ||
         variable == "NODE_REPORT_LOCAL_FIRST";
}

bool isNodeReport(const std::string& variable) {
  return variable == "NODE_REPORT" || isLocalNodeReport(variable) ||
         variable == "NODE_REPORT_UNC";
}

}  // namespace

void NumericSeries::add(double time, double value) {
  if(std::isfinite(time) && std::isfinite(value))
    samples_.emplace_back(time, value);
}

void NumericSeries::finalize() {
  std::stable_sort(samples_.begin(), samples_.end(),
                   [](const auto& left, const auto& right) {
                     return left.first < right.first;
                   });
  std::vector<std::pair<double, double>> unique;
  for(const auto& sample : samples_) {
    if(!unique.empty() && unique.back().first == sample.first)
      unique.back() = sample;
    else
      unique.push_back(sample);
  }
  samples_ = std::move(unique);
}

double NumericSeries::minTime() const {
  return samples_.empty() ? 0 : samples_.front().first;
}

double NumericSeries::maxTime() const {
  return samples_.empty() ? 0 : samples_.back().first;
}

double NumericSeries::valueAt(double time, bool angular) const {
  (void)angular;
  if(samples_.empty())
    return 0;
  const auto upper = std::upper_bound(
      samples_.begin(), samples_.end(), time,
      [](double value, const auto& sample) { return value < sample.first; });
  if(upper == samples_.begin())
    return upper->second;
  return std::prev(upper)->second;
}

Bounds NumericSeries::pairedBounds(const NumericSeries& other, double start,
                                   double end) const {
  Bounds bounds;
  if(samples_.empty() || other.samples_.empty() || end < start)
    return bounds;

  start = std::max(start, std::max(minTime(), other.minTime()));
  if(end < start)
    return bounds;

  std::vector<double> times = {start, end};
  for(const auto& sample : samples_) {
    if(sample.first >= start && sample.first <= end)
      times.push_back(sample.first);
  }
  for(const auto& sample : other.samples_) {
    if(sample.first >= start && sample.first <= end)
      times.push_back(sample.first);
  }
  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end()), times.end());
  for(double time : times)
    bounds.include(valueAt(time), other.valueAt(time));
  return bounds;
}

std::vector<std::pair<double, double>> NumericSeries::pairedSamples(
    const NumericSeries& other, double start, double end,
    std::size_t maximum) const {
  std::vector<std::pair<double, double>> result;
  if(samples_.empty() || other.samples_.empty() || end < start || maximum == 0)
    return result;

  start = std::max(start, std::max(minTime(), other.minTime()));
  end = std::min(end, std::max(maxTime(), other.maxTime()));
  if(end < start)
    return result;
  std::vector<double> times = {start};
  for(const auto& sample : samples_) {
    if(sample.first > start && sample.first <= end)
      times.push_back(sample.first);
  }
  for(const auto& sample : other.samples_) {
    if(sample.first > start && sample.first <= end)
      times.push_back(sample.first);
  }
  times.push_back(end);
  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end()), times.end());

  const std::size_t stride =
      times.size() <= maximum ? 1 : (times.size() + maximum - 1) / maximum;
  for(std::size_t index = 0; index < times.size(); index += stride)
    result.emplace_back(valueAt(times[index]), other.valueAt(times[index]));
  if(!times.empty() && (times.size() - 1) % stride != 0)
    result.emplace_back(valueAt(times.back()), other.valueAt(times.back()));
  return result;
}

VehicleTrack::VehicleTrack(std::string name) : name_(std::move(name)) {}

void VehicleTrack::addReport(double time, const NodeRecord& record) {
  reports_.push_back({time, record});
  if(record.isSetX())
    x.add(time, record.getX());
  if(record.isSetY())
    y.add(time, record.getY());
  if(record.isSetHeading())
    heading.add(time, record.getHeading());
}

void VehicleTrack::crossFillLatLon(double latitude_origin,
                                   double longitude_origin) {
  CMOOSGeodesy geodesy;
  if(!geodesy.Initialise(latitude_origin, longitude_origin))
    return;

  for(TimedRecord& sample : reports_) {
    NodeRecord& record = sample.record;
    if(record.isSetXY() || !record.isSetLatitude() ||
       !record.isSetLongitude()) {
      continue;
    }
    double northing = 0;
    double easting = 0;
    if(geodesy.LatLong2LocalGrid(record.getLat(), record.getLon(), northing,
                                easting)) {
      record.setX(easting);
      record.setY(northing);
      x.add(sample.time, easting);
      y.add(sample.time, northing);
    }
  }
}

void VehicleTrack::finalize() {
  x.finalize();
  y.finalize();
  heading.finalize();
  std::stable_sort(reports_.begin(), reports_.end(),
                   [](const TimedRecord& left, const TimedRecord& right) {
                     return left.time < right.time;
                   });

  // alogview treats vehicle type, color, and length as log metadata: its
  // splitter discovers the first available NODE_REPORT_LOCAL fields before
  // playback starts. Do the same so NAV_X/Y samples that precede the first
  // node report do not render through MarineViewer's unscaled 100-metre AUV
  // fallback. Runtime fields such as mode and all-stop remain time-local.
  presentation_type_.clear();
  presentation_color_.clear();
  presentation_length_.reset();
  for(const TimedRecord& sample : reports_) {
    if(presentation_type_.empty() && !sample.record.getType().empty())
      presentation_type_ = sample.record.getType();
    if(presentation_color_.empty() && !sample.record.getColor().empty())
      presentation_color_ = sample.record.getColor();
    if(!presentation_length_ && sample.record.isSetLength())
      presentation_length_ = sample.record.getLength();
    if(!presentation_type_.empty() && !presentation_color_.empty() &&
       presentation_length_) {
      break;
    }
  }
}

std::optional<NodeRecord> VehicleTrack::recordAt(double time) const {
  if(x.empty() || y.empty() || time < x.minTime() || time < y.minTime())
    return std::nullopt;

  NodeRecord record(name_, "");
  const auto upper = std::upper_bound(
      reports_.begin(), reports_.end(), time,
      [](double value, const TimedRecord& sample) { return value < sample.time; });
  if(upper != reports_.begin())
    record = std::prev(upper)->record;

  record.setName(name_);
  // Reports may omit unchanged presentation attributes. Backfill only from
  // reports already observed at this time; never leak a later type, color, or
  // length into an earlier frame.
  for(auto sample = upper; sample != reports_.begin() &&
                           (record.getType().empty() ||
                            record.getColor().empty() ||
                            !record.isSetLength());) {
    --sample;
    if(record.getType().empty() && !sample->record.getType().empty())
      record.setType(sample->record.getType());
    if(record.getColor().empty() && !sample->record.getColor().empty())
      record.setColor(sample->record.getColor());
    if(!record.isSetLength() && sample->record.isSetLength())
      record.setLength(sample->record.getLength());
  }
  if(record.getType().empty() && !presentation_type_.empty())
    record.setType(presentation_type_);
  if(record.getColor().empty() && !presentation_color_.empty())
    record.setColor(presentation_color_);
  if(!record.isSetLength() && presentation_length_)
    record.setLength(*presentation_length_);
  record.setX(x.valueAt(time));
  record.setY(y.valueAt(time));
  if(!heading.empty() && time >= heading.minTime())
    record.setHeading(heading.valueAt(time, true));
  else if(!record.isSetHeading())
    record.setHeading(0);
  return record;
}

Bounds VehicleTrack::bounds(double start, double end) const {
  return x.pairedBounds(y, start, end);
}

std::vector<std::pair<double, double>> VehicleTrack::trail(double start,
                                                           double end) const {
  return x.pairedSamples(y, start, end);
}

ALogTimeline ALogTimeline::load(const std::filesystem::path& path) {
  std::ifstream input(path);
  if(!input)
    throw std::runtime_error("unable to open input .alog file for reading");

  ALogTimeline timeline;
  std::vector<std::pair<double, double>> local_x;
  std::vector<std::pair<double, double>> local_y;
  std::vector<std::pair<double, double>> local_heading;
  std::vector<std::pair<double, NodeRecord>> local_reports;
  std::string local_name;
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();

  std::string line;
  while(std::getline(input, line)) {
    if(!line.empty() && line.back() == '\r')
      line.pop_back();
    if(line.empty())
      continue;
    if(line.front() == '%') {
      const std::size_t marker = line.find("LOGSTART");
      if(marker != std::string::npos) {
        std::istringstream value(line.substr(marker + 8));
        double parsed = 0;
        if(value >> parsed)
          timeline.log_start_utc_ = parsed;
      }
      continue;
    }

    std::istringstream fields(line);
    std::string time_text;
    std::string variable;
    std::string source;
    if(!(fields >> time_text >> variable >> source))
      continue;
    const std::optional<double> time = number(time_text);
    if(!time)
      continue;
    // ALOG timestamps are elapsed log time. Some warped launches can emit
    // startup APPCAST_REQ messages whose wall-clock value was incorrectly
    // offset by LOGSTART, producing enormous negative timestamps. They are
    // outside the log timeline and must not define the default media range.
    if(*time < 0)
      continue;
    std::string value;
    std::getline(fields, value);
    value = trimLeft(value);

    minimum = std::min(minimum, *time);
    maximum = std::max(maximum, *time);

    if(variable == "DB_TIME" && timeline.community_.empty())
      timeline.community_ = communityFromDbSource(source);
    if(variable == "REGION_INFO" && timeline.region_info_.empty() &&
       !value.empty()) {
      timeline.region_info_ = value;
    }

    if(variable == "NAV_X") {
      if(const auto parsed = number(value))
        local_x.emplace_back(*time, *parsed);
    } else if(variable == "NAV_Y") {
      if(const auto parsed = number(value))
        local_y.emplace_back(*time, *parsed);
    } else if(variable == "NAV_HEADING") {
      if(const auto parsed = number(value))
        local_heading.emplace_back(*time, *parsed);
    } else if(isNodeReport(variable)) {
      const NodeRecord record = string2NodeRecord(value);
      const std::string name = record.getName();
      if(isLocalNodeReport(variable)) {
        if(!name.empty())
          local_name = name;
        local_reports.emplace_back(*time, record);
      } else if(!name.empty()) {
        auto [iterator, inserted] =
            timeline.vehicles_.try_emplace(name, VehicleTrack(name));
        (void)inserted;
        iterator->second.addReport(*time, record);
      }
    }

    if(isGeometryVariable(variable)) {
      timeline.geometry_events_.push_back(
          {*time, variable, value, timeline.community_});
    }
  }

  if(!std::isfinite(minimum) || !std::isfinite(maximum))
    throw std::runtime_error("input .alog contains no timestamped entries");
  timeline.min_time_ = minimum;
  timeline.max_time_ = maximum;

  if(timeline.community_.empty())
    timeline.community_ = local_name.empty() ? "vehicle" : local_name;
  if(local_name.empty())
    local_name = timeline.community_;
  if(!local_x.empty() || !local_y.empty() || !local_reports.empty()) {
    auto [iterator, inserted] = timeline.vehicles_.try_emplace(
        local_name, VehicleTrack(local_name));
    (void)inserted;
    VehicleTrack& track = iterator->second;
    track.setName(local_name);
    for(const auto& [time, record] : local_reports)
      track.addReport(time, record);
    for(const auto& [time, value] : local_x)
      track.x.add(time, value);
    for(const auto& [time, value] : local_y)
      track.y.add(time, value);
    for(const auto& [time, value] : local_heading)
      track.heading.add(time, value);
  }

  const std::string latitude =
      tokStringParse(timeline.region_info_, "lat_datum", ',', '=');
  const std::string longitude =
      tokStringParse(timeline.region_info_, "lon_datum", ',', '=');
  const std::optional<double> latitude_origin = number(latitude);
  const std::optional<double> longitude_origin = number(longitude);

  for(auto& [name, track] : timeline.vehicles_) {
    (void)name;
    if(latitude_origin && longitude_origin)
      track.crossFillLatLon(*latitude_origin, *longitude_origin);
    track.finalize();
  }
  for(GeometryEvent& event : timeline.geometry_events_) {
    if(event.community.empty())
      event.community = timeline.community_;
  }
  std::stable_sort(timeline.geometry_events_.begin(),
                   timeline.geometry_events_.end(),
                   [](const GeometryEvent& left, const GeometryEvent& right) {
                     return left.time < right.time;
                   });
  return timeline;
}

Bounds ALogTimeline::sceneBounds(double start, double end,
                                 bool include_geometry) const {
  Bounds bounds;
  for(const auto& [name, track] : vehicles_) {
    (void)name;
    bounds.include(track.bounds(start, end));
  }
  if(include_geometry)
    bounds.include(geometryBounds(geometry_events_, log_start_utc_, start, end));
  return bounds;
}

void ALogTimeline::crossFillLatLon(double latitude_origin,
                                   double longitude_origin) {
  for(auto& [name, track] : vehicles_) {
    (void)name;
    track.crossFillLatLon(latitude_origin, longitude_origin);
    track.finalize();
  }
}

}  // namespace alog2media
