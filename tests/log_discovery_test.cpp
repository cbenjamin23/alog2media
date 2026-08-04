#include "LogDiscovery.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("alog2media-discovery-" + std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::filesystem::path writeLog(
    const std::filesystem::path& root, const std::filesystem::path& relative,
    const std::string& records,
    std::filesystem::file_time_type modified) {
  const std::filesystem::path path = root / relative;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  require(static_cast<bool>(output), "test log opens for writing");
  output << "%% LOGSTART 1000.0\n" << records;
  output.close();
  std::filesystem::last_write_time(path, modified);
  return path;
}

bool discoveryFails(const std::filesystem::path& root,
                    const std::string& expected) {
  try {
    (void)alog2media::discoverLatestLog(root);
    return false;
  } catch(const std::runtime_error& error) {
    return std::string(error.what()).find(expected) != std::string::npos;
  }
}

}  // namespace

int main() {
  TemporaryDirectory workspace;
  const auto baseline = std::filesystem::file_time_type::clock::now() -
                        std::chrono::hours(1);

  writeLog(workspace.path(), "older/arbitrary-name.alog",
           "0 DB_TIME MOOSDB_old 1000\n"
           "1 REGION_INFO pMarineViewer zoom=1\n",
           baseline);
  writeLog(workspace.path(), "new-run/vehicle.ALOG",
           "0 DB_TIME MOOSDB_alpha 1000\n"
           "1 NODE_REPORT_LOCAL pNodeReporter NAME=alpha,X=0,Y=0\n",
           baseline + std::chrono::minutes(10));
  const auto scene = writeLog(
      workspace.path(), "new-run/custom-scene.alog",
      "0 DB_TIME MOOSDB_viewer 1000\n"
      "1 REGION_INFO pMarineViewer zoom=1,pan_x=0,pan_y=0\n",
      baseline + std::chrono::minutes(10) + std::chrono::seconds(1));

  const auto malformed = workspace.path() / "new-run/not-really.alog";
  {
    std::ofstream output(malformed);
    output << "not a MOOS log\n";
  }
  std::filesystem::last_write_time(
      malformed, baseline + std::chrono::minutes(20));

  const auto too_deep = writeLog(
      workspace.path(), "one/two/three/ignored.alog",
      "0 REGION_INFO pMarineViewer zoom=1\n",
      baseline + std::chrono::minutes(30));
  (void)too_deep;

  require(alog2media::hasALogExtension("mission.ALOG"),
          "the .alog extension is case-insensitive");
  require(alog2media::discoverLatestLog(workspace.path()) == scene,
          "the latest run's unique scene-bearing log is selected without "
          "filename-prefix assumptions");

  const auto competing = writeLog(
      workspace.path(), "new-run/another-scene.alog",
      "0 REGION_INFO pMarineViewer zoom=2\n",
      baseline + std::chrono::minutes(10) + std::chrono::seconds(2));
  (void)competing;
  require(discoveryFails(workspace.path(), "more than one equally plausible"),
          "equally plausible logs in the latest run are rejected as ambiguous");

  TemporaryDirectory empty;
  require(discoveryFails(empty.path(), "no readable .alog"),
          "a directory without valid logs has a useful error");

  TemporaryDirectory symlinks;
  const auto external = writeLog(
      workspace.path(), "external.alog",
      "0 REGION_INFO pMarineViewer zoom=1\n",
      baseline + std::chrono::minutes(40));
  std::error_code link_error;
  std::filesystem::create_symlink(external, symlinks.path() / "linked.alog",
                                  link_error);
  if(!link_error) {
    require(discoveryFails(symlinks.path(), "no readable .alog"),
            "discovery does not follow .alog symlinks");
  }

  std::cout << "log discovery tests passed\n";
  return 0;
}
