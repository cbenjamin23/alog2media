#include "ALogTimeline.hpp"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if(!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

bool closeTo(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main(int argc, char* argv[]) {
  require(argc == 3,
          "usage: timeline_test FIXTURE.alog LATLON_ONLY.alog");
  const std::filesystem::path fixture(argv[1]);
  const alog2media::ALogTimeline timeline =
      alog2media::ALogTimeline::load(fixture);

  require(closeTo(timeline.logStartUtc(), 1000), "LOGSTART is parsed");
  require(closeTo(timeline.minTime(), 0), "minimum time is parsed");
  require(closeTo(timeline.maxTime(), 2), "maximum time is parsed");
  require(timeline.community() == "shoreside", "DB community is inferred");
  require(timeline.vehicles().size() == 2, "two reported vehicles are found");

  const auto alpha = timeline.vehicles().find("alpha");
  require(alpha != timeline.vehicles().end(), "alpha track exists");
  const auto alpha_early = alpha->second.recordAt(0.15);
  require(alpha_early && closeTo(alpha_early->getX(), 1) &&
              closeTo(alpha_early->getY(), 2),
          "vehicle positions use sample-and-hold");
  require(alpha_early && alpha_early->getType().empty() &&
              alpha_early->getColor().empty() && !alpha_early->isSetLength(),
          "later vehicle presentation metadata does not leak backward");
  const auto alpha_same_time = alpha->second.recordAt(0.2);
  require(alpha_same_time && closeTo(alpha_same_time->getX(), 5) &&
              closeTo(alpha_same_time->getY(), 6) &&
              alpha_same_time->getType() == "kayak" &&
              alpha_same_time->getColor() == "yellow" &&
              closeTo(alpha_same_time->getLength(), 4),
          "the last same-time report wins while input order stays stable");

  const auto bravo = timeline.vehicles().find("bravo");
  require(bravo != timeline.vehicles().end(), "bravo track exists");
  const auto bravo_record = bravo->second.recordAt(1.0);
  require(bravo_record && bravo_record->isSetXY(),
          "LAT/LON-only reports are converted from REGION_INFO datum");
  require(std::abs(bravo_record->getX()) > 1 &&
              std::abs(bravo_record->getY()) > 1,
          "converted local-grid coordinates are nontrivial");

  require(timeline.geometryEvents().size() == 3,
          "all supported geometry events remain in the raw timeline");
  const alog2media::Bounds bounds = timeline.sceneBounds(0, 2, true);
  require(bounds.valid() && bounds.max_x >= 120 && bounds.min_x <= -80,
          "fit bounds include geometry, not just tracks");

  alog2media::ALogTimeline mission_datum =
      alog2media::ALogTimeline::load(argv[2]);
  const auto& charlie = mission_datum.vehicles().at("charlie");
  require(!charlie.recordAt(0.5),
          "LAT/LON-only track waits when the log has no datum");
  mission_datum.crossFillLatLon(42.0, -71.0);
  const auto converted = mission_datum.vehicles().at("charlie").recordAt(0.5);
  require(converted && converted->isSetXY(),
          "an optional mission datum completes a LAT/LON-only track");

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path padded_fixture =
      std::filesystem::temp_directory_path() /
      ("alog2media-padded-values-" + std::to_string(nonce) + ".alog");
  {
    std::ofstream output(padded_fixture);
    require(static_cast<bool>(output), "temporary padded fixture opens");
    output << "%% LOGSTART 1000.0\n"
           << "0.00000 DB_TIME MOOSDB_padded 1000.0\n"
           << "0.10000 NAV_X uSimMarine 10.00000   \n"
           << "0.10000 NAV_Y uSimMarine 20.00000   \n"
           << "0.10000 NAV_HEADING uSimMarine 270.00000   \n"
           << "0.20000 APPCAST pLogger end\n";
  }
  const alog2media::ALogTimeline padded =
      alog2media::ALogTimeline::load(padded_fixture);
  std::error_code remove_error;
  std::filesystem::remove(padded_fixture, remove_error);
  require(!remove_error, "temporary padded fixture is removed");
  const auto local = padded.vehicles().find("padded");
  require(local != padded.vehicles().end(), "padded local NAV track exists");
  const auto local_record = local->second.recordAt(0.1);
  require(local_record && closeTo(local_record->getX(), 10) &&
              closeTo(local_record->getY(), 20) &&
              closeTo(local_record->getHeading(), 270),
          "pLogger numeric values with trailing padding are parsed");

  std::cout << "timeline tests passed\n";
  return 0;
}
