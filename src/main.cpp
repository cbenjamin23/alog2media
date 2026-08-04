#include "FfmpegEncoder.hpp"
#include "MediaRenderer.hpp"
#include "Options.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

struct FileState {
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modified;
};

FileState fileState(const std::filesystem::path& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if(error)
    throw std::runtime_error("could not inspect automatically selected .alog: " +
                             error.message());
  const auto modified = std::filesystem::last_write_time(path, error);
  if(error)
    throw std::runtime_error("could not inspect automatically selected .alog: " +
                             error.message());
  return {size, modified};
}

bool operator==(const FileState& left, const FileState& right) {
  return left.size == right.size && left.modified == right.modified;
}

const char* warpSourceName(alog2media::WarpSource source) {
  if(source == alog2media::WarpSource::mission)
    return "mission";
  if(source == alog2media::WarpSource::explicit_option)
    return "explicit";
  return "fallback";
}

int run(const alog2media::Options& options) {
  if(!std::filesystem::is_regular_file(options.input))
    throw std::runtime_error("input .alog file does not exist or is not a regular file");
  if(std::filesystem::exists(options.output) && !options.force)
    throw std::runtime_error(
        "output already exists; choose another path or pass --force");
  if(!options.output.parent_path().empty() &&
     !std::filesystem::is_directory(options.output.parent_path()))
    throw std::runtime_error("output parent directory does not exist");
  if(!alog2media::ffmpegAvailable())
    throw std::runtime_error("FFmpeg was not found on PATH");

  FileState discovered_state;
  if(options.input_discovered) {
    if(options.input_discovery_used_mtime_fallback) {
      std::cerr << "alog2media: warning: discovered logs contain no usable "
                   "MISSION_HASH; using modification times to identify the "
                   "latest run. Pass an .alog path explicitly if that choice "
                   "is not intended.\n";
    }
    std::cout << "Using latest log: " << options.input << "\n";
    discovered_state = fileState(options.input);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if(!(discovered_state == fileState(options.input))) {
      throw std::runtime_error(
          "automatically selected .alog is still changing; wait for logging "
          "to finish or pass an .alog path explicitly");
    }
  }

  alog2media::MediaRenderer renderer(options);
  if(options.input_discovered &&
     !(discovered_state == fileState(options.input))) {
    throw std::runtime_error(
        "automatically selected .alog changed while it was being loaded; "
        "wait for logging to finish or pass an .alog path explicitly");
  }
  const alog2media::RenderMetadata& metadata = renderer.metadata();

  const bool snapshot = options.output_format == alog2media::OutputFormat::png;
  const double warp = metadata.warp;
  if(!snapshot && metadata.warp_source == alog2media::WarpSource::fallback) {
    std::cerr << "alog2media: warning: no valid mission MOOSTimeWarp was "
                 "available; using 1 log second per output second. Pass "
                 "--warp FACTOR to override.\n";
  }
  const double media_duration = snapshot ? 0.0 :
      (metadata.end - metadata.start) / warp;
  const std::uint64_t frame_count = snapshot ? 1 : std::max<std::uint64_t>(
      1, static_cast<std::uint64_t>(std::ceil(media_duration * options.fps)));
  if(frame_count > 100000000)
    throw std::runtime_error("requested render exceeds 100,000,000 frames");

  std::cout << "Rendering " << options.input << "\n"
            << "  log time: " << metadata.start;
  if(!snapshot)
    std::cout << " to " << metadata.end;
  std::cout << "\n";
  if(snapshot)
    std::cout << "  frames:   1 snapshot\n";
  else
    std::cout << "  frames:   " << frame_count << " at " << options.fps << " fps"
              << " (" << warp << "x warp, "
              << warpSourceName(metadata.warp_source) << ")\n";
  std::cout
            << "  scene:    " << metadata.map << " via " << metadata.backend << "\n"
            << "  output:   " << options.output << "\n";
  if(metadata.mission) {
    std::cout << "  mission:  " << *metadata.mission;
    if(metadata.discovered_mission)
      std::cout << " (discovered)";
    std::cout << "\n";
  }

  alog2media::FfmpegEncoder encoder(
      options.output, options.width, options.height,
      snapshot ? 1.0 : options.fps, options.force);

  for(std::uint64_t frame = 0; frame < frame_count; ++frame) {
    const double log_time = std::min(
        metadata.end,
        metadata.start + (static_cast<double>(frame) * warp / options.fps));
    encoder.writeFrame(renderer.render(log_time));

    if(!snapshot &&
       (options.verbose || frame + 1 == frame_count || frame % 10 == 0)) {
      std::cout << "\r  rendered " << (frame + 1) << "/" << frame_count
                << " at log t=" << std::fixed << std::setprecision(2) << log_time
                << std::flush;
    }
  }
  if(!snapshot)
    std::cout << "\n";

  encoder.finish();
  std::cout << "Created " << options.output << "\n";
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const alog2media::ParseResult parsed = alog2media::parseOptions(argc, argv);
    if(parsed.action == alog2media::ParseAction::help) {
      std::cout << alog2media::helpText();
      return 0;
    }
    if(parsed.action == alog2media::ParseAction::version) {
      std::cout << alog2media::versionText() << "\n";
      return 0;
    }
    return run(parsed.options);
  } catch(const alog2media::UsageError& error) {
    std::cerr << "alog2media: " << error.what() << "\n";
    return 2;
  } catch(const std::exception& error) {
    std::cerr << "alog2media: " << error.what() << "\n";
    return 1;
  }
}
