#include "Options.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

alog2media::ParseResult parse(std::vector<std::string> values) {
  std::vector<char*> arguments;
  for(std::string& value : values)
    arguments.push_back(value.data());
  return alog2media::parseOptions(static_cast<int>(arguments.size()),
                                  arguments.data());
}

void require(bool condition, const std::string& message) {
  if(!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

bool rejects(const std::vector<std::string>& input) {
  try {
    parse(input);
    return false;
  } catch(const alog2media::UsageError&) {
    return true;
  }
}

}  // namespace

int main() {
  const auto tif = parse({"alog2media", "mission.alog", "--map", "harbor.tif"});
  require(tif.options.map && tif.options.map->extension() == ".tif",
          "--map accepts .tif");
  require(tif.options.map_mode == alog2media::MapMode::file,
          "a TIFF selects file map mode");

  const auto tiff = parse({"alog2media", "mission.alog", "--map=harbor.tiff"});
  require(tiff.options.map && tiff.options.map->extension() == ".tiff",
          "--map accepts .tiff");

  const auto mapless = parse({"alog2media", "mission.alog", "--map", "none"});
  require(mapless.options.map_mode == alog2media::MapMode::none &&
              !mapless.options.map,
          "--map none selects a mapless scene");

  const auto mission = parse(
      {"alog2media", "mission.alog", "--mission=viewer.moos"});
  require(mission.options.mission &&
              mission.options.mission->filename() == "viewer.moos",
          "--mission accepts .moos files");

  require(rejects({"alog2media", "mission.alog", "--map", "harbor.png"}),
          "--map rejects non-TIFF extensions");
  require(rejects({"alog2media", "mission.alog", "--mission", "viewer.txt"}),
          "--mission rejects non-.moos extensions");
  const auto unordered =
      parse({"alog2media", "--grid", "off", "mission.alog"});
  require(unordered.options.input == "mission.alog",
          "the input may follow options");
  const auto interleaved = parse(
      {"alog2media", "--grid", "off", "mission.alog", "--labels", "on"});
  require(interleaved.options.input == "mission.alog" &&
              interleaved.options.labels == alog2media::ToggleMode::on,
          "options may appear on both sides of the input");
  require(rejects({"alog2media", "first.alog", "second.alog"}),
          "multiple explicit inputs are rejected");
  require(rejects({"alog2media", "mission.txt"}),
          "unknown positional arguments are rejected");
  require(rejects({"alog2media", "mission.alog", "--duration", "2", "--end", "3"}),
          "--duration conflicts with --end");
  require(rejects({"alog2media", "mission.alog", "--size", "1279x720"}),
          "odd H.264 dimensions are rejected");
  const auto explicit_warp =
      parse({"alog2media", "mission.alog", "--warp", "2.5"});
  require(explicit_warp.options.warp_explicit &&
              explicit_warp.options.warp == 2.5,
          "--warp is retained as an explicit override");

  const auto png = parse({"alog2media", "mission.alog", "--output",
                          "snapshot.PNG", "--at", "12.5", "--size",
                          "1279x719"});
  require(png.options.output_format == alog2media::OutputFormat::png,
          ".png selects snapshot output");
  require(png.options.at && *png.options.at == 12.5,
          "--at selects the snapshot log time");
  require(png.options.width == 1279 && png.options.height == 719,
          "PNG snapshots accept odd dimensions");
  require(rejects({"alog2media", "mission.alog", "--at", "1"}),
          "--at requires PNG output");
  require(rejects({"alog2media", "mission.alog", "-o", "scene.png",
                   "--start", "1"}),
          "PNG snapshots reject video interval options");
  require(rejects({"alog2media", "mission.alog", "-o", "scene.png",
                   "--fps", "4"}),
          "PNG snapshots reject video rate options");

  const auto help = parse({"alog2media", "--help"});
  require(help.action == alog2media::ParseAction::help,
          "--help does not require an input file");
  require(alog2media::helpText().find("FILE.tif|FILE.tiff") != std::string::npos,
          "help documents both TIFF extensions");
  require(alog2media::helpText().find("--view mission|fit") != std::string::npos,
          "help documents view modes");
  require(alog2media::helpText().find("--trails auto|off|full|SECONDS") !=
              std::string::npos,
          "help documents every trail mode");
  require(alog2media::helpText().find(
              "Override automatic pMarineViewer mission discovery") !=
              std::string::npos,
          "help explains that --mission overrides discovery");
  require(alog2media::helpText().find(
              "MOOSTimeWarp; otherwise 1 with a warning") !=
              std::string::npos,
          "help explains automatic playback warp and its fallback");
  require(alog2media::helpText().find(".mp4, .gif, or .png") !=
              std::string::npos,
          "help documents every output suffix");
  const std::vector<std::string> documented_options = {
    "--output", "--size", "--fps", "--force", "--at", "--start", "--end",
    "--duration", "--warp", "--mission", "--map", "--view", "--grid",
    "--labels", "--geometry", "--trails", "--verbose", "--help", "--version"
  };
  for(const std::string& option : documented_options) {
    require(alog2media::helpText().find(option) != std::string::npos,
            "help documents " + option);
  }

  const auto defaults = parse({"alog2media", "mission.alog"});
  require(!defaults.options.input_discovered,
          "an explicit input is never marked as discovered");
  require(defaults.options.view == alog2media::ViewMode::mission,
          "mission viewport is the default");
  require(defaults.options.map_mode == alog2media::MapMode::automatic,
          "map defaults to automatic discovery");
  require(defaults.options.grid == alog2media::ToggleMode::off,
          "grid defaults to off");
  require(defaults.options.labels == alog2media::ToggleMode::automatic,
          "labels default to auto");
  require(defaults.options.geometry == alog2media::ToggleMode::automatic,
          "geometry defaults to auto");
  require(defaults.options.trails == alog2media::TrailsMode::automatic,
          "trails default to auto");
  require(!defaults.options.warp_explicit,
          "an omitted --warp remains eligible for mission recovery");

  const auto toggles = parse({"alog2media", "mission.alog", "--grid", "on",
                              "--labels=off", "--geometry", "auto"});
  require(toggles.options.grid == alog2media::ToggleMode::on,
          "--grid on is explicit");
  require(toggles.options.labels == alog2media::ToggleMode::off,
          "--labels off is explicit");
  require(toggles.options.geometry == alog2media::ToggleMode::automatic,
          "--geometry auto follows configuration");
  require(rejects({"alog2media", "mission.alog", "--geometry", "sometimes"}),
          "tri-state options reject unknown values");

  const auto trail_window =
      parse({"alog2media", "mission.alog", "--trails", "12.5"});
  require(trail_window.options.trails == alog2media::TrailsMode::seconds &&
              trail_window.options.trails_seconds &&
              *trail_window.options.trails_seconds == 12.5,
          "--trails accepts a positive seconds window");
  const auto full = parse({"alog2media", "mission.alog", "--trails=full"});
  require(full.options.trails == alog2media::TrailsMode::full &&
              !full.options.trails_seconds,
          "--trails full selects the complete track");
  const auto compatible =
      parse({"alog2media", "mission.alog", "--trails", "all"});
  require(compatible.options.trails == alog2media::TrailsMode::full,
          "legacy --trails all remains an alias for full");
  require(rejects({"alog2media", "mission.alog", "--trails", "0"}),
          "--trails rejects a zero-length window");

  std::cout << "options tests passed\n";
  return 0;
}
