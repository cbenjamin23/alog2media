#include "OffscreenContext.hpp"

#include <stdexcept>

namespace alog2media {

std::unique_ptr<OffscreenContext> makeOffscreenContext(int, int) {
  throw std::runtime_error(
      "this build has no true offscreen renderer; Linux EGL/OSMesa is not implemented yet");
}

}  // namespace alog2media
