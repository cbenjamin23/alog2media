#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alog2media {

struct MissionParam {
  std::string name;
  std::string value;
};

class MissionConfig {
 public:
  static MissionConfig load(const std::filesystem::path& source);

  const std::filesystem::path& source() const { return source_; }
  const std::vector<MissionParam>& params() const { return params_; }
  std::optional<double> latOrigin() const { return lat_origin_; }
  std::optional<double> longOrigin() const { return long_origin_; }

  // Return the last value for a parameter, matching its name without regard
  // to ASCII case. Repeated parameters retain their original order in params().
  std::optional<std::string> last(std::string_view name) const;

 private:
  std::filesystem::path source_;
  std::vector<MissionParam> params_;
  std::optional<double> lat_origin_;
  std::optional<double> long_origin_;
};

}  // namespace alog2media
