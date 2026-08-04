#include "VehicleLabel.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include "NodeRecord.h"

namespace {

void requireEqual(const std::string& actual, const std::string& expected,
                  const std::string& message) {
  if(actual != expected) {
    std::cerr << "FAIL: " << message << "\n"
              << "  expected: " << expected << "\n"
              << "  actual:   " << actual << "\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  NodeRecord startup("alpha", "kayak");
  requireEqual(
      alog2media::formatVehicleLabel("alpha", "names+mode", startup),
      "alpha", "missing startup mode fields do not produce empty suffixes");
  requireEqual(
      alog2media::formatVehicleLabel("alpha", "names+shortmode", startup),
      "alpha", "short mode also suppresses missing startup fields");

  NodeRecord underway("alpha", "kayak");
  underway.setMode("MODE@ACTIVE:TRANSIT");
  underway.setAllStop("ManualOverride");
  requireEqual(
      alog2media::formatVehicleLabel("alpha", "names+mode", underway),
      "alpha (MODE@ACTIVE:TRANSIT) (ManualOverride)",
      "full mode preserves reported values");
  requireEqual(
      alog2media::formatVehicleLabel("alpha", "names+shortmode", underway),
      "alpha (TRANSIT) (ManualOverride)",
      "short mode abbreviates a reported helm mode");

  NodeRecord clear("alpha", "kayak");
  clear.setMode("none");
  clear.setAllStop("clear");
  requireEqual(
      alog2media::formatVehicleLabel("alpha", "names+mode", clear),
      "alpha", "sentinel mode values remain hidden");

  std::cout << "vehicle label tests passed\n";
  return 0;
}
