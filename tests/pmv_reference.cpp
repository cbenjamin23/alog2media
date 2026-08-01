#include "ALogTimeline.hpp"
#include "MissionConfig.hpp"
#include "OffscreenContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "MBUtils.h"
#include "PMV_Viewer.h"

namespace {

std::optional<double> finiteNumber(const std::string& value) {
  const std::string clean = stripBlankEnds(value);
  std::size_t used = 0;
  try {
    const double parsed = std::stod(clean, &used);
    if(used == clean.size() && std::isfinite(parsed))
      return parsed;
  } catch(const std::exception&) {
  }
  return std::nullopt;
}

std::string regionValue(const std::string& region, const std::string& name) {
  return stripBlankEnds(tokStringParse(region, name, ',', '='));
}

void applyMissionSettings(PMV_Viewer& viewer,
                          const alog2media::MissionConfig& mission) {
  for(const alog2media::MissionParam& param : mission.params()) {
    const std::string name = tolower(stripBlankEnds(param.name));
    if(name == "tiff_file" || name == "tiff_file_b")
      continue;
    if(viewer.setParam(name, param.value))
      continue;
    const std::optional<double> numeric = finiteNumber(param.value);
    if(numeric)
      viewer.setParam(name, *numeric);
  }
}

void applyCamera(PMV_Viewer& viewer, const std::string& region) {
  const std::optional<double> zoom = finiteNumber(regionValue(region, "zoom"));
  const std::optional<double> pan_x = finiteNumber(regionValue(region, "pan_x"));
  const std::optional<double> pan_y = finiteNumber(regionValue(region, "pan_y"));
  if(zoom)
    viewer.setParam("set_zoom", *zoom);
  if(pan_x)
    viewer.setParam("set_pan_x", *pan_x);
  if(pan_y)
    viewer.setParam("set_pan_y", *pan_y);
}

void writePpm(const std::filesystem::path& output, int width, int height,
              const std::vector<std::uint8_t>& rgb) {
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  if(!stream)
    throw std::runtime_error("unable to open reference output");
  stream << "P6\n" << width << ' ' << height << "\n255\n";
  stream.write(reinterpret_cast<const char*>(rgb.data()),
               static_cast<std::streamsize>(rgb.size()));
  if(!stream)
    throw std::runtime_error("unable to write reference output");
}

}  // namespace

int main(int argc, char* argv[]) {
  if(argc != 8) {
    std::cerr << "usage: pmv_reference INPUT.alog MAP.tif MISSION.moos "
                 "LOG_TIME WIDTH HEIGHT OUTPUT.ppm\n";
    return 2;
  }

  try {
    const std::filesystem::path input = argv[1];
    const std::filesystem::path map = argv[2];
    const std::filesystem::path mission_path = argv[3];
    const double log_time = std::stod(argv[4]);
    const int width = std::stoi(argv[5]);
    const int height = std::stoi(argv[6]);
    const std::filesystem::path output = argv[7];
    if(width <= 0 || height <= 0 || !std::isfinite(log_time))
      throw std::runtime_error("invalid reference dimensions or time");

    alog2media::ALogTimeline timeline =
        alog2media::ALogTimeline::load(input);
    const alog2media::MissionConfig mission =
        alog2media::MissionConfig::load(mission_path);
    if(mission.latOrigin() && mission.longOrigin())
      timeline.crossFillLatLon(*mission.latOrigin(), *mission.longOrigin());

    int framebuffer_width = width;
    int framebuffer_height = height;
#ifdef __APPLE__
    // Upstream MarineViewer enables high-resolution GL on macOS. Allocate
    // enough backing storage before asking FLTK for the effective pixel size.
    framebuffer_width *= 2;
    framebuffer_height *= 2;
#endif
    std::unique_ptr<alog2media::OffscreenContext> context =
        alog2media::makeOffscreenContext(framebuffer_width,
                                         framebuffer_height);
    context->makeCurrent();

    auto viewer = std::make_unique<PMV_Viewer>(0, 0, width, height);
    std::optional<double> latitude =
        finiteNumber(regionValue(timeline.regionInfo(), "lat_datum"));
    std::optional<double> longitude =
        finiteNumber(regionValue(timeline.regionInfo(), "lon_datum"));
    if((!latitude || !longitude) && mission.latOrigin() && mission.longOrigin()) {
      latitude = mission.latOrigin();
      longitude = mission.longOrigin();
    }
    if(!latitude || !longitude)
      throw std::runtime_error("reference scene has no geodesy datum");
    if(!viewer->initGeodesy(*latitude, *longitude))
      throw std::runtime_error("unable to initialize reference geodesy");
    viewer->updateMOOSGeodesy();
    if(!viewer->setParam("tiff_file", map.string()))
      throw std::runtime_error("unable to configure reference map");

    applyMissionSettings(*viewer, mission);
    applyCamera(*viewer, timeline.regionInfo());
    viewer->setParam("hash_viewable", "false");
    viewer->setParam("trails_viewable", "false");
    viewer->setParam("curr_time", timeline.logStartUtc() + log_time);

    for(const alog2media::GeometryEvent& event : timeline.geometryEvents()) {
      if(event.time > log_time)
        break;
      const std::string community =
          event.community.empty() ? "shoreside" : event.community;
      viewer->addGeoShape(event.variable, event.value, community,
                          timeline.logStartUtc() + event.time);
    }

    for(const auto& [name, track] : timeline.vehicles()) {
      std::optional<NodeRecord> record = track.recordAt(log_time);
      if(!record)
        continue;
      record->setName(name);
      record->setTimeStamp(timeline.logStartUtc() + log_time);
      std::string why_not;
      if(!viewer->handleNodeReport(record->getSpec(), why_not)) {
        throw std::runtime_error("reference node report rejected: " + why_not);
      }
    }
    if(timeline.vehicles().count(timeline.community()) != 0)
      viewer->setParam("active_vehicle_name", timeline.community());
    else if(!timeline.vehicles().empty())
      viewer->setParam("active_vehicle_name", timeline.vehicles().begin()->first);

    viewer->setParam("curr_time", timeline.logStartUtc() + log_time);
    viewer->setConfigComplete();
    viewer->draw();
    glFinish();

    int pixel_width = width;
    int pixel_height = height;
#ifdef __APPLE__
    pixel_width = viewer->pixel_w();
    pixel_height = viewer->pixel_h();
#endif
    if(pixel_width <= 0 || pixel_height <= 0 ||
       pixel_width > framebuffer_width || pixel_height > framebuffer_height) {
      throw std::runtime_error("unexpected pMarineViewer backing scale");
    }

    const std::size_t row_bytes = static_cast<std::size_t>(pixel_width) * 3;
    std::vector<std::uint8_t> bottom_up(
        row_bytes * static_cast<std::size_t>(pixel_height));
    std::vector<std::uint8_t> top_down(bottom_up.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, pixel_width, pixel_height, GL_RGB, GL_UNSIGNED_BYTE,
                 bottom_up.data());
    if(glGetError() != GL_NO_ERROR)
      throw std::runtime_error("pMarineViewer reference readback failed");
    for(int row = 0; row < pixel_height; ++row) {
      const std::size_t source =
          static_cast<std::size_t>(pixel_height - row - 1) * row_bytes;
      const std::size_t destination = static_cast<std::size_t>(row) * row_bytes;
      std::copy_n(bottom_up.data() + source, row_bytes,
                  top_down.data() + destination);
    }
    writePpm(output, pixel_width, pixel_height, top_down);
    viewer.reset();
    return 0;
  } catch(const std::exception& error) {
    std::cerr << "pmv_reference: " << error.what() << '\n';
    return 1;
  }
}
