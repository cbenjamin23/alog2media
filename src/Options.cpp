#include "Options.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

#ifndef ALOG2MEDIA_VERSION
#define ALOG2MEDIA_VERSION "development"
#endif

namespace alog2media {
namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool hasExtension(const std::filesystem::path& path,
                  const std::vector<std::string>& extensions) {
  const std::string extension = lower(path.extension().string());
  return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

double parseNumber(const std::string& value, const std::string& option) {
  std::size_t used = 0;
  double result = 0;
  try {
    result = std::stod(value, &used);
  } catch(const std::exception&) {
    throw UsageError(option + " requires a number; received '" + value + "'");
  }
  if(used != value.size() || !std::isfinite(result))
    throw UsageError(option + " requires a finite number; received '" + value + "'");
  return result;
}

std::pair<int, int> parseSize(const std::string& value) {
  const std::size_t separator = value.find_first_of("xX");
  if(separator == std::string::npos || separator == 0 || separator + 1 >= value.size())
    throw UsageError("--size must use WIDTHxHEIGHT, for example 1280x720");

  const double width = parseNumber(value.substr(0, separator), "--size");
  const double height = parseNumber(value.substr(separator + 1), "--size");
  if(std::floor(width) != width || std::floor(height) != height)
    throw UsageError("--size dimensions must be whole numbers");
  if(width < 16 || width > 8192 || height < 16 || height > 8192)
    throw UsageError("--size dimensions must each be between 16 and 8192 pixels");
  if(static_cast<int>(width) % 2 != 0 || static_cast<int>(height) % 2 != 0)
    throw UsageError("--size dimensions must be even for H.264 compatibility");
  return {static_cast<int>(width), static_cast<int>(height)};
}

std::string optionValue(const std::string& argument, int& index, int argc,
                        char* argv[], const std::string& option) {
  const std::string prefix = option + "=";
  if(argument.rfind(prefix, 0) == 0)
    return argument.substr(prefix.size());
  if(argument == option) {
    if(index + 1 >= argc)
      throw UsageError(option + " requires a value");
    ++index;
    return argv[index];
  }
  return {};
}

bool isValueOption(const std::string& argument, const std::string& option) {
  return argument == option || argument.rfind(option + "=", 0) == 0;
}

}  // namespace

ParseResult parseOptions(int argc, char* argv[]) {
  ParseResult result;

  for(int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if(argument == "-h" || argument == "--help") {
      result.action = ParseAction::help;
      return result;
    }
    if(argument == "-V" || argument == "--version") {
      result.action = ParseAction::version;
      return result;
    }
  }

  if(argc < 2)
    throw UsageError("missing INPUT.alog; run 'alog2media --help' for usage");

  const std::string first = argv[1];
  if(first.empty() || first.front() == '-')
    throw UsageError("INPUT.alog must be the first argument");

  result.options.input = first;
  if(!hasExtension(result.options.input, {".alog"}))
    throw UsageError("input must end in .alog");

  for(int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    std::string value;

    if(argument == "-o") {
      if(index + 1 >= argc)
        throw UsageError("-o requires a value");
      result.options.output = argv[++index];
    } else if(isValueOption(argument, "--output")) {
      result.options.output = optionValue(argument, index, argc, argv, "--output");
    } else if(isValueOption(argument, "--mission")) {
      value = optionValue(argument, index, argc, argv, "--mission");
      std::filesystem::path mission(value);
      if(!hasExtension(mission, {".moos"}))
        throw UsageError("--mission accepts a .moos file");
      result.options.mission = std::move(mission);
    } else if(isValueOption(argument, "--map")) {
      value = optionValue(argument, index, argc, argv, "--map");
      if(lower(value) == "none") {
        result.options.map_mode = MapMode::none;
        result.options.map.reset();
        continue;
      }
      std::filesystem::path map(value);
      if(!hasExtension(map, {".tif", ".tiff"}))
        throw UsageError(
            "--map accepts 'none', .tif, or .tiff; TIFF maps also require "
            "matching .info metadata");
      result.options.map_mode = MapMode::file;
      result.options.map = std::move(map);
    } else if(isValueOption(argument, "--view")) {
      value = lower(optionValue(argument, index, argc, argv, "--view"));
      if(value == "mission")
        result.options.view = ViewMode::mission;
      else if(value == "fit")
        result.options.view = ViewMode::fit;
      else
        throw UsageError("--view must be 'mission' or 'fit'");
    } else if(isValueOption(argument, "--start")) {
      result.options.start = parseNumber(
          optionValue(argument, index, argc, argv, "--start"), "--start");
    } else if(isValueOption(argument, "--end")) {
      result.options.end = parseNumber(
          optionValue(argument, index, argc, argv, "--end"), "--end");
    } else if(isValueOption(argument, "--duration")) {
      result.options.duration = parseNumber(
          optionValue(argument, index, argc, argv, "--duration"), "--duration");
    } else if(isValueOption(argument, "--fps")) {
      result.options.fps = parseNumber(
          optionValue(argument, index, argc, argv, "--fps"), "--fps");
    } else if(isValueOption(argument, "--warp")) {
      result.options.warp = parseNumber(
          optionValue(argument, index, argc, argv, "--warp"), "--warp");
    } else if(isValueOption(argument, "--size")) {
      const auto dimensions = parseSize(
          optionValue(argument, index, argc, argv, "--size"));
      result.options.width = dimensions.first;
      result.options.height = dimensions.second;
    } else if(isValueOption(argument, "--grid")) {
      value = lower(optionValue(argument, index, argc, argv, "--grid"));
      if(value == "auto")
        result.options.grid = ToggleMode::automatic;
      else if(value == "on")
        result.options.grid = ToggleMode::on;
      else if(value == "off")
        result.options.grid = ToggleMode::off;
      else
        throw UsageError("--grid must be 'auto', 'on', or 'off'");
    } else if(isValueOption(argument, "--labels")) {
      value = lower(optionValue(argument, index, argc, argv, "--labels"));
      if(value == "auto")
        result.options.labels = ToggleMode::automatic;
      else if(value == "on")
        result.options.labels = ToggleMode::on;
      else if(value == "off")
        result.options.labels = ToggleMode::off;
      else
        throw UsageError("--labels must be 'auto', 'on', or 'off'");
    } else if(isValueOption(argument, "--geometry")) {
      value = lower(optionValue(argument, index, argc, argv, "--geometry"));
      if(value == "auto")
        result.options.geometry = ToggleMode::automatic;
      else if(value == "on")
        result.options.geometry = ToggleMode::on;
      else if(value == "off")
        result.options.geometry = ToggleMode::off;
      else
        throw UsageError("--geometry must be 'auto', 'on', or 'off'");
    } else if(isValueOption(argument, "--trails")) {
      value = lower(optionValue(argument, index, argc, argv, "--trails"));
      result.options.trails_seconds.reset();
      if(value == "auto" || value == "window")
        result.options.trails = TrailsMode::automatic;
      else if(value == "off")
        result.options.trails = TrailsMode::off;
      else if(value == "full" || value == "all")
        result.options.trails = TrailsMode::full;
      else {
        const double seconds = parseNumber(value, "--trails");
        if(seconds <= 0)
          throw UsageError("--trails SECONDS must be greater than zero");
        result.options.trails = TrailsMode::seconds;
        result.options.trails_seconds = seconds;
      }
    } else if(argument == "--force") {
      result.options.force = true;
    } else if(argument == "-v" || argument == "--verbose") {
      result.options.verbose = true;
    } else {
      throw UsageError("unknown option '" + argument + "'; run 'alog2media --help'");
    }
  }

  if(result.options.output.empty()) {
    result.options.output = result.options.input.filename();
    result.options.output.replace_extension(".mp4");
  }
  if(!hasExtension(result.options.output, {".mp4", ".gif"}))
    throw UsageError("--output must end in .mp4 or .gif");

  if(result.options.duration && result.options.end)
    throw UsageError("--duration and --end cannot be used together");
  if(result.options.duration && *result.options.duration <= 0)
    throw UsageError("--duration must be greater than zero");
  if(result.options.fps <= 0 || result.options.fps > 240)
    throw UsageError("--fps must be greater than zero and no more than 240");
  if(result.options.warp <= 0)
    throw UsageError("--warp must be greater than zero");

  return result;
}

std::string helpText() {
  return R"HELP(alog2media - render the pMarineViewer map scene from a MOOS log

Usage:
  alog2media INPUT.alog [OPTIONS]

Arguments:
  INPUT.alog                 MOOS .alog file to render. It must be first.

Output:
  -o, --output FILE          Destination .mp4 or .gif file.
                             Default: ./INPUT_BASENAME.mp4
  --size WIDTHxHEIGHT        Final frame dimensions. Both values must be even.
                             Default: 1280x720
  --fps RATE                 Output frames per second. Default: 15
  --force                    Replace an existing output file.

Time:
  --start SECONDS            First log-relative time. Default: log minimum
  --end SECONDS              End of the log-time interval. Default: log maximum
  --duration SECONDS         Log-time duration after --start; conflicts with --end.
  --warp FACTOR              Log seconds per output second. Default: 1

Scene:
  --mission FILE.moos        Override automatic pMarineViewer mission discovery.
                             Without REGION_INFO, searches beside INPUT.alog
                             and in its parent mission directory.
  --map FILE.tif|FILE.tiff   Override the configured TIFF map; matching .info
                             metadata is required. Use '--map none' for a
                             mapless local-coordinate scene.
  --view mission|fit         mission uses REGION_INFO pan/zoom (default); fit
                             frames visible scene content.
  --grid auto|on|off         Follow mission config, force grid on, or force it
                             off. auto falls back to off.
  --labels auto|on|off       Follow mission config or override all supported
                             scene labels. auto falls back to on.
  --geometry auto|on|off     Follow mission config or override all logged
                             VIEW_* geometry. auto falls back to on.
  --trails auto|off|full|SECONDS
                             Follow mission config, hide trails, draw the full
                             track, or draw the most recent log-time window.
                             auto falls back to the normal recent trail.

General:
  -v, --verbose              Show parser, map discovery, and render details.
  -h, --help                 Show this complete option reference.
  -V, --version              Show version and renderer-backend information.

Options may use either '--option value' or '--option=value'. The output suffix
selects MP4 or GIF encoding. Explicit CLI scene options override mission-file
settings. FFmpeg must be available on PATH.

Examples:
  alog2media mission.alog
  alog2media mission.alog -o clip.gif --start 20 --duration 30 --warp 4
  alog2media mission.alog --mission mission.moos --grid auto --trails 30
  alog2media mission.alog --map none --view fit --labels off
)HELP";
}

std::string versionText() {
#ifdef ALOG2MEDIA_HAVE_CGL
  const char* backend = "cgl-fbo";
#elif defined(ALOG2MEDIA_HAVE_EGL)
  const char* backend = "egl-surfaceless-fbo";
#else
  const char* backend = "window-compatibility";
#endif
  return std::string("alog2media ") + ALOG2MEDIA_VERSION + " (" + backend + ")";
}

}  // namespace alog2media
