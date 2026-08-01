#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace alog2media {

class FfmpegEncoder {
 public:
  FfmpegEncoder(const std::filesystem::path& output, int width, int height,
                double fps, bool force);
  ~FfmpegEncoder();

  FfmpegEncoder(const FfmpegEncoder&) = delete;
  FfmpegEncoder& operator=(const FfmpegEncoder&) = delete;

  void writeFrame(const std::vector<std::uint8_t>& rgb);
  void finish();

 private:
  int input_fd_ = -1;
  int child_pid_ = -1;
  std::size_t frame_bytes_ = 0;
  bool finished_ = false;
};

bool ffmpegAvailable();

}  // namespace alog2media
