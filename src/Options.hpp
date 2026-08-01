#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace alog2media {

enum class ParseAction { run, help, version };
enum class ViewMode { mission, fit };
enum class TrailsMode { window, off, all };

struct Options {
  std::filesystem::path input;
  std::filesystem::path output;
  std::optional<std::filesystem::path> map;
  std::optional<double> start;
  std::optional<double> end;
  std::optional<double> duration;
  double fps = 15.0;
  double warp = 1.0;
  int width = 1280;
  int height = 720;
  ViewMode view = ViewMode::mission;
  TrailsMode trails = TrailsMode::window;
  bool grid = false;
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
std::string helpText();
std::string versionText();

}  // namespace alog2media
