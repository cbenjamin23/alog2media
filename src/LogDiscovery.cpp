#include "LogDiscovery.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace alog2media {
namespace {

constexpr int kMaximumDirectoryDepth = 2;
constexpr auto kRunCompletionWindow = std::chrono::seconds(5);
constexpr std::streamoff kInspectionLimit = 4 * 1024 * 1024;

struct Candidate {
  std::filesystem::path path;
  std::filesystem::file_time_type modified;
  int scene_rank = 0;
};

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

int inspectLog(const std::filesystem::path& path) {
  std::ifstream input(path);
  if(!input)
    return 0;

  bool has_logstart = false;
  bool has_global_report = false;
  bool has_view_event = false;
  bool has_region_info = false;
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
    if(variable == "REGION_INFO")
      has_region_info = true;
    else if(variable.rfind("VIEW_", 0) == 0)
      has_view_event = true;
    else if(variable == "NODE_REPORT" || variable == "NODE_REPORT_UNC")
      has_global_report = true;
  }

  if(!has_logstart)
    return 0;
  if(has_region_info)
    return 4;
  if(has_view_event)
    return 3;
  if(has_global_report)
    return 2;
  return 1;
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
  const int rank = inspectLog(entry.path());
  if(rank > 0)
    candidates.push_back({entry.path(), modified, rank});
}

std::string candidateList(const std::vector<Candidate>& candidates) {
  std::ostringstream message;
  for(const Candidate& candidate : candidates)
    message << "\n  " << candidate.path.string();
  return message.str();
}

}  // namespace

bool hasALogExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".alog";
}

std::filesystem::path discoverLatestLog(const std::filesystem::path& root) {
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

  const int best_rank = std::max_element(
      latest_run.begin(), latest_run.end(),
      [](const Candidate& left, const Candidate& right) {
        return left.scene_rank < right.scene_rank;
      })->scene_rank;
  std::vector<Candidate> best;
  std::copy_if(latest_run.begin(), latest_run.end(), std::back_inserter(best),
               [best_rank](const Candidate& candidate) {
                 return candidate.scene_rank == best_rank;
               });

  if(best.size() != 1) {
    throw std::runtime_error(
        "the latest .alog set has more than one equally plausible scene log; "
        "pass the intended .alog path explicitly:" + candidateList(best));
  }
  return best.front().path;
}

}  // namespace alog2media
