#pragma once

#include <memory>
#include <string>

namespace alog2media {

class OffscreenContext {
 public:
  virtual ~OffscreenContext() = default;
  virtual void makeCurrent() = 0;
  virtual std::string name() const = 0;
};

std::unique_ptr<OffscreenContext> makeOffscreenContext(int width, int height);

}  // namespace alog2media
