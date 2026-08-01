#include "FfmpegEncoder.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace alog2media {
namespace {

std::string number(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6) << value;
  return stream.str();
}

std::vector<std::string> ffmpegArguments(const std::filesystem::path& output,
                                         int width, int height, double fps,
                                         bool force) {
  std::vector<std::string> arguments = {
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
    force ? "-y" : "-n",
    "-f", "rawvideo",
    "-pixel_format", "rgb24",
    "-video_size", std::to_string(width) + "x" + std::to_string(height),
    "-framerate", number(fps),
    "-i", "pipe:0",
    "-an"
  };

  std::string extension = output.extension().string();
  for(char& character : extension)
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

  if(extension == ".mp4") {
    arguments.insert(arguments.end(), {
      "-c:v", "libx264", "-preset", "medium", "-crf", "20",
      "-pix_fmt", "yuv420p", "-movflags", "+faststart"
    });
  } else {
    arguments.insert(arguments.end(), {
      "-filter_complex",
      "[0:v]split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse",
      "-loop", "0"
    });
  }

  arguments.push_back(output.string());
  return arguments;
}

std::vector<char*> argumentPointers(std::vector<std::string>& arguments) {
  std::vector<char*> pointers;
  pointers.reserve(arguments.size() + 1);
  for(std::string& argument : arguments)
    pointers.push_back(argument.data());
  pointers.push_back(nullptr);
  return pointers;
}

}  // namespace

bool ffmpegAvailable() {
  const char* path_value = std::getenv("PATH");
  if(!path_value)
    return false;

  std::stringstream paths(path_value);
  std::string directory;
  while(std::getline(paths, directory, ':')) {
    if(directory.empty())
      directory = ".";
    const std::filesystem::path candidate =
        std::filesystem::path(directory) / "ffmpeg";
    if(::access(candidate.c_str(), X_OK) == 0)
      return true;
  }
  return false;
}

FfmpegEncoder::FfmpegEncoder(const std::filesystem::path& output,
                             int width, int height, double fps, bool force)
    : frame_bytes_(static_cast<std::size_t>(width) *
                   static_cast<std::size_t>(height) * 3) {
  int pipe_fds[2];
  if(::pipe(pipe_fds) != 0)
    throw std::system_error(errno, std::generic_category(), "creating FFmpeg pipe");

  std::vector<std::string> arguments =
      ffmpegArguments(output, width, height, fps, force);
  std::vector<char*> pointers = argumentPointers(arguments);

  const pid_t pid = ::fork();
  if(pid < 0) {
    const int saved_errno = errno;
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    throw std::system_error(saved_errno, std::generic_category(), "starting FFmpeg");
  }

  if(pid == 0) {
    ::close(pipe_fds[1]);
    if(::dup2(pipe_fds[0], STDIN_FILENO) < 0)
      _exit(126);
    ::close(pipe_fds[0]);
    ::execvp(pointers[0], pointers.data());
    _exit(127);
  }

  ::close(pipe_fds[0]);
  input_fd_ = pipe_fds[1];
  child_pid_ = static_cast<int>(pid);
  std::signal(SIGPIPE, SIG_IGN);
}

FfmpegEncoder::~FfmpegEncoder() {
  if(!finished_) {
    if(input_fd_ >= 0)
      ::close(input_fd_);
    if(child_pid_ > 0) {
      int status = 0;
      while(::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
    }
  }
}

void FfmpegEncoder::writeFrame(const std::vector<std::uint8_t>& rgb) {
  if(finished_ || input_fd_ < 0)
    throw std::runtime_error("cannot write to a finished FFmpeg encoder");
  if(rgb.size() != frame_bytes_)
    throw std::runtime_error("rendered frame size does not match the encoder input");

  std::size_t written = 0;
  while(written < rgb.size()) {
    const ssize_t count =
        ::write(input_fd_, rgb.data() + written, rgb.size() - written);
    if(count < 0) {
      if(errno == EINTR)
        continue;
      throw std::system_error(errno, std::generic_category(), "writing frame to FFmpeg");
    }
    written += static_cast<std::size_t>(count);
  }
}

void FfmpegEncoder::finish() {
  if(finished_)
    return;
  finished_ = true;

  if(input_fd_ >= 0) {
    ::close(input_fd_);
    input_fd_ = -1;
  }

  int status = 0;
  while(::waitpid(child_pid_, &status, 0) < 0) {
    if(errno == EINTR)
      continue;
    throw std::system_error(errno, std::generic_category(), "waiting for FFmpeg");
  }
  child_pid_ = -1;

  if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::ostringstream message;
    message << "FFmpeg failed";
    if(WIFEXITED(status))
      message << " with exit code " << WEXITSTATUS(status);
    else if(WIFSIGNALED(status))
      message << " after signal " << WTERMSIG(status);
    throw std::runtime_error(message.str());
  }
}

}  // namespace alog2media
