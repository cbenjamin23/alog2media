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
           "0.5 MISSION_HASH pMarineViewer "
           "mhash=older-run,utc=1000.00\n"
           "1 REGION_INFO pMarineViewer zoom=1\n",
           baseline + std::chrono::minutes(30));
  writeLog(workspace.path(), "new-run/vehicle.ALOG",
           "0 DB_TIME MOOSDB_alpha 1000\n"
           "0.1 MISSION_HASH pNodeReporter "
           "utc=2000.00,mhash=newest-run\n"
           "0.5 HELM_MAP_CLEAR pMarineViewer false\n"
           "1 NODE_REPORT_LOCAL pNodeReporter NAME=alpha,X=0,Y=0\n",
           baseline + std::chrono::minutes(10));
  const auto scene = writeLog(
      workspace.path(), "new-run/custom-scene.alog",
      "0 DB_TIME MOOSDB_viewer 1000\n"
      "0.1 MISSION_HASH pMarineViewer "
      "mhash=newest-run,utc=2000.00\n"
      "1 REGION_INFO pMarineViewer zoom=1,pan_x=0,pan_y=0\n",
      baseline + std::chrono::minutes(20));

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
  const auto hashed =
      alog2media::discoverLatestLogWithDetails(workspace.path());
  require(hashed.path == scene && !hashed.used_mtime_fallback,
          "MISSION_HASH UTC selects the newest run and its unique REGION_INFO "
          "log despite inverted and widely staggered modification times");

  const auto competing = writeLog(
      workspace.path(), "new-run/another-scene.alog",
      "0 MISSION_HASH pMarineViewer mhash=newest-run,utc=2000.00\n"
      "1 REGION_INFO pMarineViewer zoom=2\n",
      baseline + std::chrono::minutes(10) + std::chrono::seconds(2));
  (void)competing;
  require(discoveryFails(workspace.path(), "more than one equally plausible"),
          "multiple REGION_INFO logs in one hash-identified run are rejected");

  TemporaryDirectory legacy;
  writeLog(legacy.path(), "arbitrary/vehicle.alog",
           "0 DB_TIME MOOSDB_alpha 1000\n"
           "1 NODE_REPORT_LOCAL pNodeReporter NAME=alpha,X=0,Y=0\n",
           baseline);
  const auto legacy_scene = writeLog(
      legacy.path(), "unrelated/scene.alog",
      "0 MISSION_HASH pMarineViewer mhash=incomplete-without-utc\n"
      "1 REGION_INFO pMarineViewer zoom=1\n",
      baseline + std::chrono::seconds(2));
  const auto fallback =
      alog2media::discoverLatestLogWithDetails(legacy.path());
  require(fallback.path == legacy_scene && fallback.used_mtime_fallback,
          "logs without a complete MISSION_HASH retain the explicit "
          "modification-time fallback marker");

  const auto legacy_competing = writeLog(
      legacy.path(), "other/scene.alog",
      "0 REGION_INFO pMarineViewer zoom=2\n",
      baseline + std::chrono::seconds(3));
  (void)legacy_competing;
  require(discoveryFails(legacy.path(), "more than one equally plausible"),
          "the legacy modification-time fallback remains ambiguity-safe");

  TemporaryDirectory conflicting;
  writeLog(conflicting.path(), "conflicting.alog",
           "0 MISSION_HASH pMarineViewer mhash=first,utc=3000\n"
           "1 MISSION_HASH pMarineViewer mhash=second,utc=3001\n"
           "2 REGION_INFO pMarineViewer zoom=1\n",
           baseline);
  require(discoveryFails(conflicting.path(), "conflicting MISSION_HASH"),
          "one log cannot silently change mission identity");

  TemporaryDirectory inconsistent;
  writeLog(inconsistent.path(), "first.alog",
           "0 MISSION_HASH pMarineViewer mhash=shared,utc=3500\n"
           "1 REGION_INFO pMarineViewer zoom=1\n",
           baseline);
  writeLog(inconsistent.path(), "second.alog",
           "0 MISSION_HASH pNodeReporter mhash=shared,utc=3501\n"
           "1 NODE_REPORT_LOCAL pNodeReporter NAME=alpha,X=0,Y=0\n",
           baseline + std::chrono::seconds(1));
  require(discoveryFails(inconsistent.path(), "inconsistent UTC"),
          "one mission hash cannot silently identify different start times");

  TemporaryDirectory tied;
  writeLog(tied.path(), "first.alog",
           "0 MISSION_HASH pMarineViewer mhash=first,utc=4000\n"
           "1 REGION_INFO pMarineViewer zoom=1\n",
           baseline);
  writeLog(tied.path(), "second.alog",
           "0 MISSION_HASH pMarineViewer mhash=second,utc=4000\n"
           "1 REGION_INFO pMarineViewer zoom=2\n",
           baseline + std::chrono::hours(1));
  require(discoveryFails(tied.path(), "more than one MISSION_HASH run"),
          "different mission hashes with the same newest UTC are ambiguous");

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
