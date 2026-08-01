#include "MediaRenderer.hpp"

#include "OffscreenContext.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <streambuf>

#include "ALogDataBroker.h"
#include "LogPlot.h"
#include "MBUtils.h"
#include "NavPlotViewer.h"

namespace alog2media {
namespace {

class NullBuffer final : public std::streambuf {
 protected:
  int overflow(int character) override { return character; }
};

class ScopedCoutSilence {
 public:
  explicit ScopedCoutSilence(bool silence) : silence_(silence) {
    if(silence_)
      previous_ = std::cout.rdbuf(&null_);
  }
  ~ScopedCoutSilence() {
    if(silence_)
      std::cout.rdbuf(previous_);
  }

 private:
  bool silence_ = false;
  NullBuffer null_;
  std::streambuf* previous_ = nullptr;
};

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

class ScopedMapAlias {
 public:
  ~ScopedMapAlias() {
    if(!directory_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(directory_, ignored);
    }
  }

  std::string normalizeForUpstream(const std::string& map_name) {
    const std::filesystem::path source(map_name);
    const std::string extension = lower(source.extension().string());
    if(extension == ".tif" && source.extension() == ".tif")
      return map_name;
    if(extension != ".tif" && extension != ".tiff")
      return map_name;

    // Current MOOS-IvP accepts both suffixes, but older supported builds only
    // derive the .info sidecar from a lowercase .tif name. Present an exact,
    // temporary alias so alog2media's public input contract is independent of
    // the MOOS-IvP revision it is linked against.
    if(!std::filesystem::is_regular_file(source))
      return map_name;

    std::filesystem::path info = source;
    info.replace_extension(".info");
    if(!std::filesystem::is_regular_file(info))
      return map_name;

    directory_ = createDirectory();
    const std::filesystem::path map_alias = directory_ / "map.tif";
    const std::filesystem::path info_alias = directory_ / "map.info";
    std::error_code error;
    std::filesystem::create_symlink(std::filesystem::absolute(source),
                                    map_alias, error);
    if(error)
      throw std::runtime_error("could not create temporary .tif map alias: " +
                               error.message());
    std::filesystem::create_symlink(std::filesystem::absolute(info),
                                    info_alias, error);
    if(error)
      throw std::runtime_error("could not create temporary .info map alias: " +
                               error.message());
    return map_alias.string();
  }

  bool active() const { return !directory_.empty(); }

 private:
  static std::filesystem::path createDirectory() {
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    std::random_device random;
    for(int attempt = 0; attempt < 64; ++attempt) {
      const std::filesystem::path candidate =
          root / ("alog2media-map-" + std::to_string(random()));
      std::error_code error;
      if(std::filesystem::create_directory(candidate, error))
        return candidate;
      if(error && error != std::errc::file_exists)
        throw std::runtime_error("could not create temporary map directory: " +
                                 error.message());
    }
    throw std::runtime_error("could not allocate a unique temporary map directory");
  }

  std::filesystem::path directory_;
};

struct Bounds {
  double min_x = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();

  bool valid() const {
    return std::isfinite(min_x) && std::isfinite(max_x) &&
           std::isfinite(min_y) && std::isfinite(max_y);
  }
};

class HeadlessNavPlotViewer final : public NavPlotViewer {
 public:
  HeadlessNavPlotViewer(int width, int height)
      : NavPlotViewer(0, 0, width, height), target_width_(width),
        target_height_(height) {}

  void draw() override {
    drawScene();
  }

  void drawScene() {
    if(!m_textures_init)
      applyTiffFiles();

    autoZoom();
    clearBackground();
    glViewport(0, 0, target_width_, target_height_);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w(), 0, h(), -1, 1);

    const double image_width = m_back_img.get_img_pix_width();
    const double image_height = m_back_img.get_img_pix_height();
    const double shape_width = image_width * m_zoom;
    const double shape_height = image_height * m_zoom;
    const double shift_x = m_vshift_x * m_zoom;
    const double shift_y = m_vshift_y * m_zoom;

    m_x_origin = -shape_width / 2 + shift_x;
    m_y_origin = -shape_height / 2 + shift_y;

    if(m_geo_settings.viewable("tiff_viewable"))
      drawTiff();
    m_main_window = Fl_Window::current();
    drawTrails();
    drawNavPlots();
    drawVPlugPlots();
    if(m_geo_settings.viewable("hash_viewable"))
      drawFastHash();
  }

  bool mapReady() const {
    return m_back_img.get_img_pix_width() > 0 &&
           m_back_img.get_img_pix_height() > 0 &&
           m_back_img.get_img_mtr_width() > 0 &&
           m_back_img.get_img_mtr_height() > 0;
  }

  std::string resolvedMap() const { return getTiffFileCurrent(); }

  void fitToBounds(const Bounds& bounds) {
    if(!bounds.valid() || !mapReady())
      throw std::runtime_error("cannot calculate a fit view without tracks and map metadata");

    const double center_x = (bounds.min_x + bounds.max_x) / 2;
    const double center_y = (bounds.min_y + bounds.max_y) / 2;
    const double ppm_x = m_back_img.get_pix_per_mtr_x();
    const double ppm_y = m_back_img.get_pix_per_mtr_y();

    m_vshift_x = -ppm_x * (center_x - m_back_img.get_x_at_img_ctr());
    m_vshift_y = -ppm_y * (center_y - m_back_img.get_y_at_img_ctr());

    const double range_x = std::max(bounds.max_x - bounds.min_x, 20.0);
    const double range_y = std::max(bounds.max_y - bounds.min_y, 20.0);
    const double fit_x = (static_cast<double>(w()) * 0.9) / (range_x * ppm_x);
    const double fit_y = (static_cast<double>(h()) * 0.9) / (range_y * ppm_y);
    m_zoom = std::max(0.00001, std::min(fit_x, fit_y));
  }

 private:
  int target_width_;
  int target_height_;
};

double regionNumber(const std::string& region, const std::string& key,
                    double fallback, bool& complete) {
  const std::string value = tokStringParse(region, key, ',', '=');
  if(!isNumber(value)) {
    complete = false;
    return fallback;
  }
  return std::stod(value);
}

Bounds trackBounds(ALogDataBroker& broker, double start, double end) {
  Bounds bounds;
  for(unsigned int index = 0; index < broker.sizeALogs(); ++index) {
    const std::string vehicle = broker.getVNameFromAix(index);
    const unsigned int x_index = broker.getMixFromVNameVarName(vehicle, "NAV_X");
    const unsigned int y_index = broker.getMixFromVNameVarName(vehicle, "NAV_Y");
    if(x_index >= broker.sizeMix() || y_index >= broker.sizeMix())
      continue;

    const LogPlot x_plot = broker.getLogPlot(x_index);
    const LogPlot y_plot = broker.getLogPlot(y_index);
    if(x_plot.empty() || y_plot.empty())
      continue;

    bounds.min_x = std::min(bounds.min_x, x_plot.getMinVal(start, end));
    bounds.max_x = std::max(bounds.max_x, x_plot.getMaxVal(start, end));
    bounds.min_y = std::min(bounds.min_y, y_plot.getMinVal(start, end));
    bounds.max_y = std::max(bounds.max_y, y_plot.getMaxVal(start, end));
  }
  return bounds;
}

void restoreMatrixStack(GLenum mode, GLenum depth_query, GLint target_depth) {
  glMatrixMode(mode);
  GLint depth = 0;
  glGetIntegerv(depth_query, &depth);
  while(depth > target_depth) {
    glPopMatrix();
    --depth;
  }
}

}  // namespace

class MediaRenderer::Impl {
 public:
  explicit Impl(const Options& options) : options_(options) {
    ScopedCoutSilence silence(!options.verbose);

    broker_.setProgress(options.verbose);
    broker_.setVerbose(options.verbose);
    broker_.addALogFile(options.input.string());
    if(!broker_.checkALogFiles() || !broker_.splitALogFiles() ||
       !broker_.setTimingInfo()) {
      throw std::runtime_error("MOOS-IvP could not load or cache the input .alog file");
    }
    broker_.cacheMasterIndices();
    broker_.cacheBehaviorIndices();
    broker_.cacheAppLogIndices();

    metadata_.log_min = broker_.getPrunedMinTime();
    metadata_.log_max = broker_.getPrunedMaxTime();
    metadata_.start = options.start.value_or(metadata_.log_min);
    metadata_.end = options.end.value_or(metadata_.log_max);
    if(options.duration)
      metadata_.end = metadata_.start + *options.duration;

    if(metadata_.start < metadata_.log_min || metadata_.start > metadata_.log_max)
      throw std::runtime_error("--start is outside the log's available time range");
    if(metadata_.end < metadata_.log_min || metadata_.end > metadata_.log_max)
      throw std::runtime_error("the requested end time is outside the log's available time range");
    if(metadata_.end <= metadata_.start)
      throw std::runtime_error("the requested end time must be later than the start time");

    const std::string region = broker_.getRegionInfo();
    metadata_.used_region_info = !region.empty();

    std::string map_name;
    if(options.map)
      map_name = options.map->string();
    else
      map_name = tokStringParse(region, "img_file", ',', '=');

    bool mission_view_complete = !region.empty();
    const double zoom = regionNumber(region, "zoom", 1.0, mission_view_complete);
    const double pan_x = regionNumber(region, "pan_x", 0.0, mission_view_complete);
    const double pan_y = regionNumber(region, "pan_y", 0.0, mission_view_complete);

    metadata_.fit_view = options.view == ViewMode::fit || !mission_view_complete;
    if(options.view == ViewMode::mission && !mission_view_complete)
      std::cerr << "warning: REGION_INFO has no complete pan/zoom; using fit view\n";
    if(map_name.empty()) {
      map_name = "forrest19.tif";
      metadata_.fit_view = true;
      std::cerr << "warning: REGION_INFO has no map; using forrest19.tif\n";
    }

    context_ = makeOffscreenContext(options.width, options.height);
    context_->makeCurrent();
    metadata_.backend = context_->name();

    viewer_ = std::make_unique<HeadlessNavPlotViewer>(options.width, options.height);
    const std::string upstream_map = map_alias_.normalizeForUpstream(map_name);
    viewer_->setParam("tiff_file", upstream_map);
    viewer_->setParam("hash_viewable", options.grid ? "true" : "false");
    if(options.trails == TrailsMode::off)
      viewer_->setParam("trails_viewable", "false");
    else if(options.trails == TrailsMode::all)
      viewer_->setParam("trails_viewable", "true");

    viewer_->setDataBroker(broker_);
    viewer_->initPlots();
    viewer_->setCurrTime(metadata_.start);

    viewer_->applyTiffFiles();
    if(!viewer_->mapReady()) {
      throw std::runtime_error(
          "unable to load map '" + map_name +
          "' and its matching .info metadata; use --map with an exact path or set IVP_IMAGE_DIRS");
    }
    metadata_.map = map_alias_.active() ? map_name : viewer_->resolvedMap();

    if(metadata_.fit_view) {
      viewer_->fitToBounds(trackBounds(broker_, metadata_.start, metadata_.end));
    } else {
      viewer_->setParam("set_pan_x", pan_x);
      viewer_->setParam("set_pan_y", pan_y);
      viewer_->setParam("set_zoom", zoom);
    }

    const GLenum error = glGetError();
    if(error != GL_NO_ERROR) {
      std::ostringstream message;
      message << "OpenGL preparation failed with error 0x" << std::hex << error;
      throw std::runtime_error(message.str());
    }
  }

  ~Impl() {
    if(context_)
      context_->makeCurrent();
    viewer_.reset();
  }

  std::vector<std::uint8_t> render(double log_time) {
    context_->makeCurrent();
    while(glGetError() != GL_NO_ERROR) {}

    GLint modelview_depth_before = 0;
    GLint projection_depth_before = 0;
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview_depth_before);
    glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection_depth_before);

    viewer_->setCurrTime(log_time);
    viewer_->drawScene();
    glFinish();

    const GLenum draw_error = glGetError();
    GLint modelview_depth_after = 0;
    GLint projection_depth_after = 0;
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview_depth_after);
    glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection_depth_after);

    // MarineViewer::drawCommonVehicle() has an upstream early return when a
    // vehicle label is outside the viewport, before its modelview pop. FLTK
    // windows tolerate this only until the finite legacy stack fills. Restore
    // the caller-owned stack boundary after every offscreen frame.
    restoreMatrixStack(GL_MODELVIEW, GL_MODELVIEW_STACK_DEPTH,
                       modelview_depth_before);
    restoreMatrixStack(GL_PROJECTION, GL_PROJECTION_STACK_DEPTH,
                       projection_depth_before);

    if(draw_error != GL_NO_ERROR ||
       modelview_depth_after < modelview_depth_before ||
       projection_depth_after < projection_depth_before) {
      std::ostringstream message;
      message << "OpenGL scene draw failed at log t=" << log_time
              << " with error 0x" << std::hex << draw_error << std::dec
              << "; stack depths modelview " << modelview_depth_before << "->"
              << modelview_depth_after << ", projection "
              << projection_depth_before << "->" << projection_depth_after;
      throw std::runtime_error(message.str());
    }

    const std::size_t row_bytes = static_cast<std::size_t>(options_.width) * 3;
    std::vector<std::uint8_t> bottom_up(
        row_bytes * static_cast<std::size_t>(options_.height));
    std::vector<std::uint8_t> top_down(bottom_up.size());

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, options_.width, options_.height, GL_RGB,
                 GL_UNSIGNED_BYTE, bottom_up.data());

    const GLenum error = glGetError();
    if(error != GL_NO_ERROR) {
      std::ostringstream message;
      message << "OpenGL frame readback failed with error 0x" << std::hex << error;
      throw std::runtime_error(message.str());
    }

    for(int row = 0; row < options_.height; ++row) {
      const std::size_t source =
          static_cast<std::size_t>(options_.height - row - 1) * row_bytes;
      const std::size_t destination = static_cast<std::size_t>(row) * row_bytes;
      std::copy_n(bottom_up.data() + source, row_bytes,
                  top_down.data() + destination);
    }
    return top_down;
  }

  const RenderMetadata& metadata() const { return metadata_; }

 private:
  Options options_;
  ALogDataBroker broker_;
  ScopedMapAlias map_alias_;
  std::unique_ptr<OffscreenContext> context_;
  std::unique_ptr<HeadlessNavPlotViewer> viewer_;
  RenderMetadata metadata_;
};

MediaRenderer::MediaRenderer(const Options& options)
    : impl_(std::make_unique<Impl>(options)) {}

MediaRenderer::~MediaRenderer() = default;

const RenderMetadata& MediaRenderer::metadata() const {
  return impl_->metadata();
}

std::vector<std::uint8_t> MediaRenderer::render(double log_time) {
  return impl_->render(log_time);
}

}  // namespace alog2media
