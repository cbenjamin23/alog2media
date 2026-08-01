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

  const auto tiff = parse({"alog2media", "mission.alog", "--map=harbor.tiff"});
  require(tiff.options.map && tiff.options.map->extension() == ".tiff",
          "--map accepts .tiff");

  require(rejects({"alog2media", "mission.alog", "--map", "harbor.png"}),
          "--map rejects non-TIFF extensions");
  require(rejects({"alog2media", "--grid", "off", "mission.alog"}),
          "input must be first");
  require(rejects({"alog2media", "mission.alog", "--duration", "2", "--end", "3"}),
          "--duration conflicts with --end");
  require(rejects({"alog2media", "mission.alog", "--size", "1279x720"}),
          "odd H.264 dimensions are rejected");

  const auto help = parse({"alog2media", "--help"});
  require(help.action == alog2media::ParseAction::help,
          "--help does not require an input file");
  require(alog2media::helpText().find("FILE.tif|FILE.tiff") != std::string::npos,
          "help documents both TIFF extensions");
  require(alog2media::helpText().find("--view mission|fit") != std::string::npos,
          "help documents view modes");
  const std::vector<std::string> documented_options = {
    "--output", "--size", "--fps", "--force", "--start", "--end",
    "--duration", "--warp", "--map", "--view", "--grid", "--trails",
    "--verbose", "--help", "--version"
  };
  for(const std::string& option : documented_options) {
    require(alog2media::helpText().find(option) != std::string::npos,
            "help documents " + option);
  }

  const auto defaults = parse({"alog2media", "mission.alog"});
  require(defaults.options.view == alog2media::ViewMode::mission,
          "mission viewport is the default");
  require(!defaults.options.grid, "grid defaults off");

  std::cout << "options tests passed\n";
  return 0;
}
