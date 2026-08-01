#include "MissionConfig.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if(!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

bool loadThrows(const std::filesystem::path& path) {
  try {
    alog2media::MissionConfig::load(path);
    return false;
  } catch(const std::runtime_error&) {
    return true;
  }
}

class TemporaryTree {
 public:
  TemporaryTree() {
    std::random_device random;
    for(int attempt = 0; attempt < 64; ++attempt) {
      path_ = std::filesystem::temp_directory_path() /
              ("alog2media-mission-test-" + std::to_string(random()));
      std::error_code error;
      if(std::filesystem::create_directory(path_, error))
        return;
    }
    throw std::runtime_error("could not create mission discovery test directory");
  }

  ~TemporaryTree() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

bool discoveryThrows(const std::filesystem::path& log) {
  try {
    (void)alog2media::discoverMissionForLog(log);
    return false;
  } catch(const std::runtime_error&) {
    return true;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  require(argc == 2, "mission_config_test requires its fixture path");
  const std::filesystem::path fixture = argv[1];
  const auto config = alog2media::MissionConfig::load(fixture);

  require(config.source() == fixture, "source path is retained");
  require(config.latOrigin() &&
              std::abs(*config.latOrigin() - 42.358436) < 1e-9,
          "LatOrigin is exposed");
  require(config.longOrigin() &&
              std::abs(*config.longOrigin() - -71.087448) < 1e-9,
          "LongOrigin is exposed");

  const auto& params = config.params();
  require(params.size() == 5, "all pMarineViewer parameters are loaded");
  require(params[0].name == "TIFF_FILE" && params[0].value == "first.tif",
          "parameter spelling and first-file position are retained");
  require(params[1].name == "hash_viewable" && params[1].value == "false",
          "the first duplicate retains its position");
  require(params[2].name == "Vehicles_Name_Mode" &&
              params[2].value == "names+depth",
          "mission whitespace is normalized the same way as pMarineViewer");
  require(params[3].name == "hash_viewable" && params[3].value == "true",
          "the later duplicate retains its position");
  require(params[4].name == "point_viewable_all" &&
              params[4].value == "false",
          "inline mission comments are excluded");

  require(config.last("HASH_VIEWABLE") &&
              *config.last("HASH_VIEWABLE") == "true",
          "last() is case-insensitive and honors file order");
  require(!config.last("not_present"), "last() reports missing parameters");
  require(loadThrows(fixture.parent_path() / "does-not-exist.moos"),
          "an unreadable mission file is rejected");

  TemporaryTree tree;
  const std::filesystem::path xlog = tree.path() / "XLOG_SHORESIDE_01";
  std::filesystem::create_directory(xlog);
  const std::filesystem::path log = xlog / "LOG_SHORESIDE_01.alog";
  const std::filesystem::path conventional =
      tree.path() / "targ_shoreside.moos";
  std::filesystem::copy_file(fixture, conventional);

  const auto discovered = alog2media::discoverMissionForLog(log);
  require(discovered && std::filesystem::equivalent(*discovered, conventional),
          "a parent targ_shoreside.moos is discovered from an XLOG path");
  const auto relative_discovered = alog2media::discoverMissionForLog(
      std::filesystem::relative(log, std::filesystem::current_path()));
  require(relative_discovered &&
              std::filesystem::equivalent(*relative_discovered, conventional),
          "relative XLOG input still searches its absolute parent mission");

  std::filesystem::remove(conventional);
  const std::filesystem::path generic = tree.path() / "viewer.moos";
  std::filesystem::copy_file(fixture, generic);
  require(!alog2media::discoverMissionForLog(log, false),
          "generic missions are ignored when logged context is complete");
  const auto sole_mission = alog2media::discoverMissionForLog(log);
  require(sole_mission && std::filesystem::equivalent(*sole_mission, generic),
          "one generic pMarineViewer mission is an unambiguous fallback");

  const std::filesystem::path competing = xlog / "other.moos";
  std::filesystem::copy_file(fixture, competing);
  require(discoveryThrows(log),
          "multiple generic pMarineViewer missions require --mission");

  std::filesystem::copy_file(fixture, conventional);
  const auto preferred = alog2media::discoverMissionForLog(log, false);
  require(preferred && std::filesystem::equivalent(*preferred, conventional),
          "targ_shoreside.moos is imported even without generic fallback");

  std::cout << "mission config tests passed\n";
  return 0;
}
