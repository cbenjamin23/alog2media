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

namespace {

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

  alog2media::MediaRenderer renderer(options);
  const alog2media::RenderMetadata& metadata = renderer.metadata();

  const bool snapshot = options.output_format == alog2media::OutputFormat::png;
  const double media_duration = snapshot ? 0.0 :
      (metadata.end - metadata.start) / options.warp;
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
              << " (" << options.warp << "x warp)\n";
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
        metadata.start + (static_cast<double>(frame) * options.warp / options.fps));
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
