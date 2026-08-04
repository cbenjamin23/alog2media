#include "VehicleLabel.hpp"

#include "MBUtils.h"
#include "NodeRecord.h"

namespace alog2media {

std::string formatVehicleLabel(const std::string& name,
                               const std::string& names_mode,
                               const NodeRecord& record) {
  std::string label = name;

  if(names_mode == "names+mode") {
    const std::string mode = record.getMode();
    const std::string allstop = record.getAllStop();
    if(!mode.empty() && mode != "none" && mode != "unknown-mode")
      label += " (" + mode + ")";
    if(!allstop.empty() && allstop != "clear")
      label += " (" + allstop + ")";
  } else if(names_mode == "names+shortmode") {
    const std::string mode = record.getMode();
    const std::string allstop = record.getAllStop();
    if(!mode.empty() && mode != "none" && mode != "unknown-mode")
      label += " (" + modeShorten(mode) + ")";
    if(!allstop.empty() && allstop != "clear" && allstop != "n/a")
      label += " (" + allstop + ")";
  } else if(names_mode == "names+auxmode") {
    const std::string mode = record.getModeAux();
    label += mode.empty() ? " (no auxmode info)" : " (" + mode + ")";
  } else if(names_mode == "names+depth") {
    label += " (depth=" + doubleToStringX(record.getDepth(), 1) + ")";
  }

  return label;
}

}  // namespace alog2media
