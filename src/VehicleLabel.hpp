#pragma once

#include <string>

class NodeRecord;

namespace alog2media {

std::string formatVehicleLabel(const std::string& name,
                               const std::string& names_mode,
                               const NodeRecord& record);

}  // namespace alog2media
