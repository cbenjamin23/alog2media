#include "MissionConfig.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

  std::cout << "mission config tests passed\n";
  return 0;
}
