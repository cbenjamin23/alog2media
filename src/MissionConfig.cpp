#include "MissionConfig.hpp"

#ifndef _WIN32
#include <pthread.h>
#endif

#include <MOOS/libMOOS/Utils/ProcessConfigReader.h>

#include <algorithm>
#include <cctype>
#include <list>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace alog2media {
namespace {

void trim(std::string& value) {
  const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(),
              std::find_if_not(value.begin(), value.end(), whitespace));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(),
              value.end());
}

bool equalIgnoringCase(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char a, unsigned char b) {
                      return std::tolower(a) == std::tolower(b);
                    });
}

std::filesystem::path normalized(const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path, error);
  return error ? std::filesystem::absolute(path) : canonical;
}

bool isMoosFile(const std::filesystem::directory_entry& entry) {
  std::error_code error;
  if(!entry.is_regular_file(error) || error)
    return false;
  std::string extension = entry.path().extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return extension == ".moos";
}

bool hasMarineViewerConfig(const std::filesystem::path& path) {
  try {
    (void)MissionConfig::load(path);
    return true;
  } catch(const std::runtime_error&) {
    return false;
  }
}

}  // namespace

MissionConfig MissionConfig::load(const std::filesystem::path& source) {
  CProcessConfigReader reader;
  reader.EnableVerbatimQuoting(false);
  if(!reader.SetFile(source.string()))
    throw std::runtime_error("unable to read mission file '" + source.string() + "'");

  STRING_LIST lines;
  if(!reader.GetConfiguration("pMarineViewer", lines)) {
    throw std::runtime_error("mission file '" + source.string() +
                             "' has no complete ProcessConfig = pMarineViewer block");
  }

  MissionConfig config;
  config.source_ = source;
  config.params_.reserve(lines.size());
  // GetConfiguration intentionally returns newest-first. pMarineViewer walks
  // this list in reverse, restoring source-file order before applying every
  // setting. Mirror that behavior so repeated and relative settings (zoom,
  // pan, "bigger", and so on) resolve exactly as they do at launch.
  for(auto iterator = lines.rbegin(); iterator != lines.rend(); ++iterator) {
    const std::string& line = *iterator;
    const std::size_t separator = line.find('=');
    std::string name = line.substr(0, separator);
    std::string value = separator == std::string::npos
                            ? std::string()
                            : line.substr(separator + 1);
    trim(name);
    trim(value);
    if(!name.empty())
      config.params_.push_back({std::move(name), std::move(value)});
  }

  double origin = 0;
  if(reader.GetValue("LatOrigin", origin))
    config.lat_origin_ = origin;
  if(reader.GetValue("LongOrigin", origin))
    config.long_origin_ = origin;

  return config;
}

std::optional<std::string> MissionConfig::last(std::string_view name) const {
  for(auto param = params_.rbegin(); param != params_.rend(); ++param) {
    if(equalIgnoringCase(param->name, name))
      return param->value;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> discoverMissionForLog(
    const std::filesystem::path& log, bool allow_generic_fallback) {
  std::vector<std::filesystem::path> directories;
  const std::filesystem::path adjacent = normalized(log).parent_path();
  directories.push_back(adjacent);
  const std::filesystem::path parent = adjacent.parent_path();
  if(!parent.empty() && normalized(parent) != normalized(adjacent))
    directories.push_back(parent);

  // A generated shoreside target has an unambiguous role, so prefer the
  // closest conventional filename before considering generic .moos files.
  for(const std::filesystem::path& directory : directories) {
    const std::filesystem::path candidate =
        directory / "targ_shoreside.moos";
    std::error_code error;
    if(std::filesystem::is_regular_file(candidate, error) && !error) {
      if(!hasMarineViewerConfig(candidate)) {
        throw std::runtime_error(
            "automatic mission candidate '" + candidate.string() +
            "' has no usable ProcessConfig = pMarineViewer block; pass "
            "--mission with the intended file");
      }
      return normalized(candidate);
    }
  }

  if(!allow_generic_fallback)
    return std::nullopt;

  std::vector<std::filesystem::path> candidates;
  for(const std::filesystem::path& directory : directories) {
    std::error_code error;
    std::filesystem::directory_iterator entries(directory, error);
    if(error)
      continue;
    for(const std::filesystem::directory_entry& entry : entries) {
      if(!isMoosFile(entry) || !hasMarineViewerConfig(entry.path()))
        continue;
      const std::filesystem::path candidate = normalized(entry.path());
      if(std::find(candidates.begin(), candidates.end(), candidate) ==
         candidates.end()) {
        candidates.push_back(candidate);
      }
    }
  }

  if(candidates.empty())
    return std::nullopt;
  if(candidates.size() == 1)
    return candidates.front();

  std::sort(candidates.begin(), candidates.end());
  std::ostringstream message;
  message << "multiple pMarineViewer missions were found near '"
          << log.string() << "':";
  for(const std::filesystem::path& candidate : candidates)
    message << "\n  " << candidate.string();
  message << "\npass --mission with the intended file";
  throw std::runtime_error(message.str());
}

}  // namespace alog2media
