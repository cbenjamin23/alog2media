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
    } else if(isValueOption(argument, "--map")) {
      value = optionValue(argument, index, argc, argv, "--map");
      std::filesystem::path map(value);
      if(!hasExtension(map, {".tif", ".tiff"}))
        throw UsageError("--map accepts .tif or .tiff files; a matching .info is also required");
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
      if(value == "on")
        result.options.grid = true;
      else if(value == "off")
        result.options.grid = false;
      else
        throw UsageError("--grid must be 'on' or 'off'");
    } else if(isValueOption(argument, "--trails")) {
      value = lower(optionValue(argument, index, argc, argv, "--trails"));
      if(value == "window")
        result.options.trails = TrailsMode::window;
      else if(value == "off")
        result.options.trails = TrailsMode::off;
      else if(value == "all")
        result.options.trails = TrailsMode::all;
      else
        throw UsageError("--trails must be 'window', 'off', or 'all'");
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
  --map FILE.tif|FILE.tiff   Override the map named by REGION_INFO. Both .tif
                             and .tiff are accepted; matching .info metadata is
                             required.
  --view mission|fit         mission uses REGION_INFO pan/zoom (default); fit
                             frames all vehicle tracks.
  --grid on|off              Draw the coordinate grid. Default: off
  --trails window|off|all    Draw the normal recent trail (default), no trail,
                             or the complete track.

General:
  -v, --verbose              Show log caching, map discovery, and render details.
  -h, --help                 Show this complete option reference.
  -V, --version              Show version and renderer-backend information.

Options may use either '--option value' or '--option=value'. The output suffix
selects MP4 or GIF encoding. FFmpeg must be available on PATH.

Examples:
  alog2media mission.alog
  alog2media mission.alog -o clip.gif --start 20 --duration 30 --warp 4
  alog2media mission.alog --map harbor.tiff --view fit --grid off
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
  return std::string("alog2media ") + ALOG2MEDIA_VERSION + "-dev (" + backend + ")";
}

}  // namespace alog2media
