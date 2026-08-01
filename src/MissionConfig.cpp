#include "MissionConfig.hpp"

#ifndef _WIN32
#include <pthread.h>
#endif

#include <MOOS/libMOOS/Utils/ProcessConfigReader.h>

#include <algorithm>
#include <cctype>
#include <list>
#include <stdexcept>

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

}  // namespace alog2media
