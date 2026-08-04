#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace alog2media {

enum class ParseAction { run, help, version };
enum class OutputFormat { mp4, gif, png };
enum class ViewMode { mission, fit };
enum class MapMode { automatic, file, none };
enum class ToggleMode { automatic, on, off };
enum class TrailsMode {
  automatic,
  off,
  full,
  seconds,

  // Source compatibility for the original CLI spellings. New code should use
  // automatic and full.
  window = automatic,
  all = full,
};

struct Options {
  std::filesystem::path input;
  bool input_discovered = false;
  std::filesystem::path output;
  OutputFormat output_format = OutputFormat::mp4;
  std::optional<std::filesystem::path> mission;
  MapMode map_mode = MapMode::automatic;
  std::optional<std::filesystem::path> map;
  std::optional<double> start;
  std::optional<double> end;
  std::optional<double> duration;
  std::optional<double> at;
  double fps = 15.0;
  double warp = 1.0;
  bool warp_explicit = false;
  int width = 1280;
  int height = 720;
  ViewMode view = ViewMode::mission;
  ToggleMode grid = ToggleMode::off;
  ToggleMode labels = ToggleMode::automatic;
  ToggleMode geometry = ToggleMode::automatic;
  TrailsMode trails = TrailsMode::automatic;
  std::optional<double> trails_seconds;
  bool force = false;
  bool verbose = false;
};

struct ParseResult {
  ParseAction action = ParseAction::run;
  Options options;
};

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

ParseResult parseOptions(int argc, char* argv[]);
ParseResult parseOptions(int argc, char* argv[],
                         const std::filesystem::path& discovery_root);
std::string helpText();
std::string versionText();

}  // namespace alog2media
