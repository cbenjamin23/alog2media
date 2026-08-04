#include "LogDiscovery.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace alog2media {
namespace {

constexpr int kMaximumDirectoryDepth = 2;
constexpr auto kRunCompletionWindow = std::chrono::seconds(5);
constexpr std::streamoff kInspectionLimit = 4 * 1024 * 1024;
constexpr double kUtcTolerance = 0.01;

struct MissionIdentity {
  std::string hash;
  double utc = 0;
};

struct Inspection {
  int scene_rank = 0;
  std::optional<MissionIdentity> mission;
};

struct Candidate {
  std::filesystem::path path;
  std::filesystem::file_time_type modified;
  int scene_rank = 0;
  std::optional<MissionIdentity> mission;
};

struct RunGroup {
  std::string hash;
  double utc = 0;
  std::vector<Candidate> candidates;
};

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string trim(std::string value) {
  const auto first = std::find_if_not(
      value.begin(), value.end(),
      [](unsigned char character) { return std::isspace(character); });
  const auto last = std::find_if_not(
      value.rbegin(), value.rend(),
      [](unsigned char character) { return std::isspace(character); }).base();
  if(first >= last)
    return {};
  return std::string(first, last);
}

std::optional<MissionIdentity> parseMissionIdentity(
    const std::string& payload) {
  std::optional<std::string> hash;
  std::optional<double> utc;
  std::istringstream fields(payload);
  std::string field;
  while(std::getline(fields, field, ',')) {
    const std::size_t equals = field.find('=');
    if(equals == std::string::npos)
      continue;
    const std::string key = lower(trim(field.substr(0, equals)));
    const std::string value = trim(field.substr(equals + 1));
    if(key == "mhash" && !value.empty()) {
      hash = value;
    } else if(key == "utc") {
      try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if(consumed == value.size() && std::isfinite(parsed) && parsed > 0)
          utc = parsed;
      } catch(const std::invalid_argument&) {
        // An incomplete or malformed MISSION_HASH is not usable for grouping.
      } catch(const std::out_of_range&) {
        // An incomplete or malformed MISSION_HASH is not usable for grouping.
      }
    }
  }
  if(!hash || !utc)
    return std::nullopt;
  return MissionIdentity{*hash, *utc};
}

Inspection inspectLog(const std::filesystem::path& path) {
  std::ifstream input(path);
  if(!input)
    return {};

  bool has_logstart = false;
  bool has_global_report = false;
  bool has_view_event = false;
  bool has_region_info = false;
  std::optional<MissionIdentity> mission;
  std::string line;
  while(input.tellg() < kInspectionLimit && std::getline(input, line)) {
    if(line.rfind("%%", 0) == 0) {
      if(line.find("LOGSTART") != std::string::npos)
        has_logstart = true;
      continue;
    }

    std::istringstream fields(line);
    std::string timestamp;
    std::string variable;
    std::string source;
    if(!(fields >> timestamp >> variable >> source))
      continue;
    if(variable == "MISSION_HASH") {
      std::string payload;
      std::getline(fields, payload);
      const auto parsed = parseMissionIdentity(payload);
      if(parsed) {
        if(mission &&
           (mission->hash != parsed->hash ||
            std::abs(mission->utc - parsed->utc) > kUtcTolerance)) {
          throw std::runtime_error(
              "log contains conflicting MISSION_HASH values: " +
              path.string());
        }
        mission = parsed;
      }
    } else if(variable == "REGION_INFO")
      has_region_info = true;
    else if(variable.rfind("VIEW_", 0) == 0)
      has_view_event = true;
    else if(variable == "NODE_REPORT" || variable == "NODE_REPORT_UNC")
      has_global_report = true;
  }

  if(!has_logstart)
    return {};
  if(has_region_info)
    return {4, mission};
  if(has_view_event)
    return {3, mission};
  if(has_global_report)
    return {2, mission};
  return {1, mission};
}

void addCandidate(const std::filesystem::directory_entry& entry,
                  std::vector<Candidate>& candidates) {
  std::error_code error;
  if(entry.is_symlink(error) || error)
    return;
  if(!entry.is_regular_file(error) || error ||
     !hasALogExtension(entry.path())) {
    return;
  }
  const auto modified = entry.last_write_time(error);
  if(error)
    return;
  const Inspection inspection = inspectLog(entry.path());
  if(inspection.scene_rank > 0) {
    candidates.push_back(
        {entry.path(), modified, inspection.scene_rank, inspection.mission});
  }
}

std::string candidateList(const std::vector<Candidate>& candidates) {
  std::ostringstream message;
  for(const Candidate& candidate : candidates)
    message << "\n  " << candidate.path.string();
  return message.str();
}

std::filesystem::path selectScene(const std::vector<Candidate>& candidates,
                                  const std::string& context) {
  const int best_rank = std::max_element(
      candidates.begin(), candidates.end(),
      [](const Candidate& left, const Candidate& right) {
        return left.scene_rank < right.scene_rank;
      })->scene_rank;
  std::vector<Candidate> best;
  std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(best),
               [best_rank](const Candidate& candidate) {
                 return candidate.scene_rank == best_rank;
               });
  if(best.size() != 1) {
    throw std::runtime_error(
        context + " has more than one equally plausible scene log; pass the "
        "intended .alog path explicitly:" + candidateList(best));
  }
  return best.front().path;
}

LogDiscoveryResult selectByMissionHash(
    const std::vector<Candidate>& candidates) {
  std::map<std::string, RunGroup> groups;
  for(const Candidate& candidate : candidates) {
    if(!candidate.mission)
      continue;
    const MissionIdentity& mission = *candidate.mission;
    auto [iterator, inserted] = groups.try_emplace(
        mission.hash, RunGroup{mission.hash, mission.utc, {}});
    if(!inserted &&
       std::abs(iterator->second.utc - mission.utc) > kUtcTolerance) {
      throw std::runtime_error(
          "MISSION_HASH '" + mission.hash +
          "' has inconsistent UTC values across logs; pass the intended "
          ".alog path explicitly");
    }
    iterator->second.candidates.push_back(candidate);
  }

  if(groups.empty())
    return {};

  double newest_utc = groups.begin()->second.utc;
  for(const auto& [hash, group] : groups) {
    (void)hash;
    newest_utc = std::max(newest_utc, group.utc);
  }

  std::vector<const RunGroup*> newest;
  for(const auto& [hash, group] : groups) {
    (void)hash;
    if(std::abs(group.utc - newest_utc) <= kUtcTolerance)
      newest.push_back(&group);
  }
  if(newest.size() != 1) {
    std::vector<Candidate> ambiguous;
    for(const RunGroup* group : newest) {
      ambiguous.insert(ambiguous.end(), group->candidates.begin(),
                       group->candidates.end());
    }
    throw std::runtime_error(
        "more than one MISSION_HASH run has the newest UTC; pass the intended "
        ".alog path explicitly:" + candidateList(ambiguous));
  }

  return {selectScene(newest.front()->candidates,
                      "the latest MISSION_HASH run"),
          false};
}

LogDiscoveryResult selectByModificationTime(
    std::vector<Candidate> candidates) {
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              if(left.modified != right.modified)
                return left.modified > right.modified;
              return left.path.string() < right.path.string();
            });

  const auto newest = candidates.front().modified;
  std::vector<Candidate> latest_run;
  for(const Candidate& candidate : candidates) {
    if(newest - candidate.modified > kRunCompletionWindow)
      break;
    latest_run.push_back(candidate);
  }
  return {selectScene(latest_run, "the latest modification-time .alog set"),
          true};
}

}  // namespace

bool hasALogExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".alog";
}

LogDiscoveryResult discoverLatestLogWithDetails(
    const std::filesystem::path& root) {
  std::error_code error;
  if(!std::filesystem::is_directory(root, error) || error) {
    throw std::runtime_error("cannot search for .alog files because the current "
                             "directory is not readable");
  }

  std::vector<Candidate> candidates;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  while(!error && iterator != end) {
    const std::filesystem::directory_entry entry = *iterator;
    std::error_code entry_error;
    if(entry.is_symlink(entry_error) && entry.is_directory(entry_error))
      iterator.disable_recursion_pending();
    if(iterator.depth() >= kMaximumDirectoryDepth &&
       entry.is_directory(entry_error)) {
      iterator.disable_recursion_pending();
    }
    addCandidate(entry, candidates);
    iterator.increment(error);
  }

  if(candidates.empty()) {
    throw std::runtime_error(
        "no readable .alog file was found in the current directory or within "
        "two nested directory levels");
  }

  const LogDiscoveryResult hashed = selectByMissionHash(candidates);
  if(!hashed.path.empty())
    return hashed;
  return selectByModificationTime(std::move(candidates));
}

std::filesystem::path discoverLatestLog(const std::filesystem::path& root) {
  return discoverLatestLogWithDetails(root).path;
}

}  // namespace alog2media
