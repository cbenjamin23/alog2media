#include "MediaRenderer.hpp"

#include "ALogTimeline.hpp"
#include "HeadlessSceneViewer.hpp"
#include "MissionConfig.hpp"
#include "OffscreenContext.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "MBUtils.h"

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

std::optional<double> finiteNumber(const std::string& value) {
  std::size_t used = 0;
  try {
    const double result = std::stod(stripBlankEnds(value), &used);
    if(used == stripBlankEnds(value).size() && std::isfinite(result))
      return result;
  } catch(const std::exception&) {
  }
  return std::nullopt;
}

std::string regionValue(const std::string& region, const std::string& name) {
  return stripBlankEnds(tokStringParse(region, name, ',', '='));
}

bool hasWhitespace(const std::filesystem::path& path) {
  const std::string value = path.string();
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char character) {
                       return std::isspace(character) != 0;
                     });
}

class ScopedMapAlias {
 public:
  ~ScopedMapAlias() {
    if(!directory_.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(directory_, ignored);
    }
  }

  std::string normalizeForUpstream(const std::filesystem::path& map_name) {
    const std::string extension = lower(map_name.extension().string());
    const bool canonical_suffix = map_name.extension() == ".tif";
    if(extension != ".tif" && extension != ".tiff")
      return map_name.string();
    if(canonical_suffix && !hasWhitespace(map_name))
      return map_name.string();

    // MarineViewer rejects whitespace in TIFF paths, and older supported
    // revisions derive the INFO path only from a lowercase .tif suffix. A
    // temporary symlink pair isolates those upstream details without touching
    // the mission directory or input log.
    if(!std::filesystem::is_regular_file(map_name))
      return map_name.string();
    std::filesystem::path info = map_name;
    info.replace_extension(".info");
    if(!std::filesystem::is_regular_file(info))
      return map_name.string();

    directory_ = createDirectory();
    const std::filesystem::path map_alias = directory_ / "map.tif";
    const std::filesystem::path info_alias = directory_ / "map.info";
    std::error_code error;
    std::filesystem::create_symlink(std::filesystem::absolute(map_name),
                                    map_alias, error);
    if(error)
      throw std::runtime_error("could not create temporary TIFF alias: " +
                               error.message());
    std::filesystem::create_symlink(std::filesystem::absolute(info),
                                    info_alias, error);
    if(error)
      throw std::runtime_error("could not create temporary INFO alias: " +
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
    throw std::runtime_error(
        "could not allocate a unique temporary map directory");
  }

  std::filesystem::path directory_;
};

std::filesystem::path resolveRelativeMap(
    const std::filesystem::path& requested,
    const std::vector<std::filesystem::path>& base_directories) {
  if(requested.is_absolute())
    return requested;
  for(const std::filesystem::path& base : base_directories) {
    if(base.empty())
      continue;
    const std::filesystem::path candidate = base / requested;
    if(std::filesystem::is_regular_file(candidate))
      return candidate;
  }
  if(std::filesystem::is_regular_file(requested))
    return requested;
  // Preserve a bare unresolved name so MarineViewer can use IVP_IMAGE_DIRS
  // and the normal installed MOOS-IvP data search path.
  return requested;
}

std::optional<std::filesystem::path> matchingMissionMap(
    const std::optional<MissionConfig>& mission,
    const std::filesystem::path& logged_map) {
  if(!mission)
    return std::nullopt;
  for(const MissionParam& param : mission->params()) {
    const std::string name = lower(stripBlankEnds(param.name));
    if((name == "tiff_file" || name == "tiff_file_b") &&
       !param.value.empty()) {
      const std::filesystem::path configured(param.value);
      if(configured.filename() == logged_map.filename())
        return configured;
    }
  }
  return std::nullopt;
}

void appendAncestorMapDirectories(
    const std::filesystem::path& start,
    std::vector<std::filesystem::path>& directories) {
  std::error_code error;
  std::filesystem::path current =
      std::filesystem::absolute(start, error);
  if(error)
    current = start;
  for(int depth = 0; depth < 12 && !current.empty(); ++depth) {
    directories.push_back(current / "data");
    directories.push_back(current / "ivp" / "data");
    const std::filesystem::path parent = current.parent_path();
    if(parent == current)
      break;
    current = parent;
  }
}

void appendEnvironmentMapDirectories(
    std::vector<std::filesystem::path>& directories) {
  const char* configured = std::getenv("IVP_IMAGE_DIRS");
  if(configured == nullptr)
    return;
  std::istringstream values(configured);
  std::string directory;
  while(std::getline(values, directory, ':')) {
    if(!directory.empty())
      directories.emplace_back(directory);
  }
}

bool isMissionMapParameter(const std::string& name) {
  const std::string parameter = lower(stripBlankEnds(name));
  return parameter == "tiff_file" || parameter == "tiff_file_b";
}

}  // namespace

class MediaRenderer::Impl {
 public:
  explicit Impl(const Options& options)
      : options_(options), timeline_(ALogTimeline::load(options.input)) {
    ScopedCoutSilence silence(!options.verbose);

    std::optional<std::filesystem::path> mission_path = options.mission;
    if(!mission_path) {
      mission_path = discoverMissionForLog(options.input);
      metadata_.discovered_mission = mission_path.has_value();
    }
    if(mission_path) {
      if(!std::filesystem::is_regular_file(*mission_path))
        throw std::runtime_error("mission file does not exist or is not a regular file");
      mission_ = MissionConfig::load(*mission_path);
      metadata_.mission = mission_->source().string();
      metadata_.used_mission = true;
      if(metadata_.discovered_mission && options.verbose)
        std::cerr << "discovered pMarineViewer mission: "
                  << mission_->source() << "\n";
      if(mission_->latOrigin() && mission_->longOrigin())
        timeline_.crossFillLatLon(*mission_->latOrigin(),
                                  *mission_->longOrigin());
    }

    metadata_.log_min = timeline_.minTime();
    metadata_.log_max = timeline_.maxTime();
    if(options.output_format == OutputFormat::png) {
      metadata_.start = options.at.value_or(metadata_.log_min);
      metadata_.end = metadata_.start;
    } else {
      metadata_.start = options.start.value_or(metadata_.log_min);
      metadata_.end = options.end.value_or(metadata_.log_max);
      if(options.duration)
        metadata_.end = metadata_.start + *options.duration;
    }

    if(metadata_.start < metadata_.log_min ||
       metadata_.start > metadata_.log_max) {
      throw std::runtime_error("--start is outside the log's available time range");
    }
    if(metadata_.end < metadata_.log_min || metadata_.end > metadata_.log_max) {
      throw std::runtime_error(
          "the requested end time is outside the log's available time range");
    }
    if(options.output_format != OutputFormat::png &&
       metadata_.end <= metadata_.start)
      throw std::runtime_error(
          "the requested end time must be later than the start time");

    metadata_.used_region_info = !timeline_.regionInfo().empty();
    metadata_.vehicle_count = timeline_.vehicles().size();
    metadata_.geometry_event_count = timeline_.geometryEvents().size();

    selectMap();

    context_ = makeOffscreenContext(options.width, options.height);
    context_->makeCurrent();
    metadata_.backend = context_->name();

    viewer_ = std::make_unique<HeadlessSceneViewer>(
        options.width, options.height, timeline_);
    viewer_->setVerbose(options.verbose);
    initializeGeodesy();
    configureMap();

    applyMissionSettings();
    applyCliSceneOverrides();

    if(!viewer_->prepareMap()) {
      if(mapless_)
        throw std::runtime_error("could not initialize the mapless coordinate scene");
      std::ostringstream message;
      message << "unable to load map '" << selected_map_.string()
              << "' and its matching .info metadata; ";
      if(!mission_ && options_.map_mode == MapMode::automatic) {
        message << "no pMarineViewer mission was discovered beside the log "
                   "or in its parent directory; if a .moos file elsewhere "
                   "contains the custom map path, pass --mission FILE.moos; ";
      }
      message << "otherwise pass --map FILE.tif, use --map none, or configure "
                 "IVP_IMAGE_DIRS";
      throw std::runtime_error(message.str());
    }

    if(mapless_) {
      viewer_->setParam("tiff_viewable", "false");
      metadata_.map = "none";
    } else {
      metadata_.map = map_alias_.active() ? selected_map_.string()
                                          : viewer_->resolvedMap();
    }

    configureCamera();
    viewer_->setTime(metadata_.start);

    const GLenum error = glGetError();
    if(error != GL_NO_ERROR) {
      std::ostringstream message;
      message << "OpenGL preparation failed with error 0x" << std::hex
              << error;
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
    while(glGetError() != GL_NO_ERROR) {
    }

    GLint modelview_depth_before = 0;
    GLint projection_depth_before = 0;
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview_depth_before);
    glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection_depth_before);

    viewer_->setTime(log_time);
    viewer_->drawScene();
    glFinish();

    const GLenum draw_error = glGetError();
    GLint modelview_depth_after = 0;
    GLint projection_depth_after = 0;
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &modelview_depth_after);
    glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &projection_depth_after);
    if(draw_error != GL_NO_ERROR ||
       modelview_depth_after != modelview_depth_before ||
       projection_depth_after != projection_depth_before) {
      std::ostringstream message;
      message << "OpenGL scene draw failed at log t=" << log_time
              << " with error 0x" << std::hex << draw_error << std::dec
              << "; stack depths modelview " << modelview_depth_before << "->"
              << modelview_depth_after << ", projection "
              << projection_depth_before << "->" << projection_depth_after;
      throw std::runtime_error(message.str());
    }

    const std::size_t row_bytes =
        static_cast<std::size_t>(options_.width) * 3;
    std::vector<std::uint8_t> bottom_up(
        row_bytes * static_cast<std::size_t>(options_.height));
    std::vector<std::uint8_t> top_down(bottom_up.size());

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, options_.width, options_.height, GL_RGB,
                 GL_UNSIGNED_BYTE, bottom_up.data());
    const GLenum read_error = glGetError();
    if(read_error != GL_NO_ERROR) {
      std::ostringstream message;
      message << "OpenGL frame readback failed with error 0x" << std::hex
              << read_error;
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
  void selectMap() {
    if(options_.map_mode == MapMode::none) {
      mapless_ = true;
      return;
    }

    std::filesystem::path requested;
    enum class Source { cli, region, mission } source = Source::cli;
    if(options_.map_mode == MapMode::file && options_.map) {
      requested = *options_.map;
    } else {
      const std::string region_map =
          regionValue(timeline_.regionInfo(), "img_file");
      if(!region_map.empty()) {
        requested = region_map;
        source = Source::region;
      } else if(mission_) {
        // MarineViewer maps TIFF_FILE_B to TIFF_FILE, queues every entry in
        // source order, and displays the first one initially. Mirror that
        // launch behavior instead of taking the last TIFF_FILE value.
        for(const MissionParam& param : mission_->params()) {
          if(isMissionMapParameter(param.name) && !param.value.empty()) {
            requested = param.value;
            source = Source::mission;
            break;
          }
        }
      }
    }

    if(requested.empty() || lower(requested.string()) == "null.tif") {
      mapless_ = true;
      std::cerr << "warning: no map was logged or configured; rendering a "
                   "mapless local-coordinate scene\n";
      return;
    }

    std::vector<std::filesystem::path> bases;
    if(source == Source::cli)
      bases.push_back(std::filesystem::current_path());
    if(source == Source::region) {
      bases.push_back(options_.input.parent_path());
      // REGION_INFO deliberately logs only the map basename. Recover a custom
      // relative or absolute path from the mission when it names that map.
      const std::optional<std::filesystem::path> configured_map =
          matchingMissionMap(mission_, requested);
      if(mission_ && configured_map) {
        const std::filesystem::path configured = resolveRelativeMap(
            *configured_map, {mission_->source().parent_path()});
        if(std::filesystem::is_regular_file(configured)) {
          selected_map_ = configured;
          return;
        }
      }
    }
    if(source == Source::mission && mission_)
      bases.push_back(mission_->source().parent_path());
    if(source != Source::mission && mission_)
      bases.push_back(mission_->source().parent_path());
    if(source != Source::region)
      bases.push_back(options_.input.parent_path());

    appendAncestorMapDirectories(options_.input.parent_path(), bases);
    if(mission_)
      appendAncestorMapDirectories(mission_->source().parent_path(), bases);
#ifdef ALOG2MEDIA_SOURCE_MAP_DIR
    bases.emplace_back(ALOG2MEDIA_SOURCE_MAP_DIR);
#endif
#ifdef ALOG2MEDIA_INSTALL_MAP_DIR
    bases.emplace_back(ALOG2MEDIA_INSTALL_MAP_DIR);
#endif
    appendEnvironmentMapDirectories(bases);
    selected_map_ = resolveRelativeMap(requested, bases);
  }

  void initializeGeodesy() {
    std::optional<double> latitude =
        finiteNumber(regionValue(timeline_.regionInfo(), "lat_datum"));
    std::optional<double> longitude =
        finiteNumber(regionValue(timeline_.regionInfo(), "lon_datum"));
    if((!latitude || !longitude) && mission_) {
      latitude = mission_->latOrigin();
      longitude = mission_->longOrigin();
    }
    if(!latitude || !longitude) {
      latitude = 0.0;
      longitude = 0.0;
    }
    if(!viewer_->initGeodesy(*latitude, *longitude))
      throw std::runtime_error("could not initialize the scene geodesy datum");
  }

  void configureMap() {
    if(mapless_) {
      if(!viewer_->handleNoTiff())
        throw std::runtime_error("could not create a mapless coordinate plane");
      return;
    }
    const std::string upstream_map =
        map_alias_.normalizeForUpstream(selected_map_);
    if(!viewer_->setParam("tiff_file", upstream_map)) {
      throw std::runtime_error("map path was rejected by MOOS-IvP: '" +
                               selected_map_.string() + "'");
    }
  }

  void applyMissionSettings() {
    if(!mission_)
      return;
    for(const MissionParam& param : mission_->params()) {
      if(isMissionMapParameter(param.name))
        continue;
      if(!viewer_->applyMissionParam(param.name, param.value) &&
         options_.verbose) {
        std::cerr << "note: ignored non-scene pMarineViewer setting '"
                  << param.name << "'\n";
      }
    }
  }

  void applyCliSceneOverrides() {
    if(options_.grid != ToggleMode::automatic)
      viewer_->setGridVisible(options_.grid == ToggleMode::on);
    if(options_.labels != ToggleMode::automatic)
      viewer_->setLabelsVisible(options_.labels == ToggleMode::on);
    if(options_.geometry != ToggleMode::automatic)
      viewer_->setGeometryVisible(options_.geometry == ToggleMode::on);

    switch(options_.trails) {
      case TrailsMode::automatic:
        viewer_->useConfiguredTrails();
        break;
      case TrailsMode::off:
        viewer_->setTrailsVisible(false);
        break;
      case TrailsMode::full:
        viewer_->useFullTrails();
        break;
      case TrailsMode::seconds:
        if(!options_.trails_seconds ||
           !viewer_->useTrailWindow(*options_.trails_seconds)) {
          throw std::runtime_error("invalid trail time window");
        }
        break;
    }
  }

  void configureCamera() {
    bool camera_applied = false;
    if(options_.view == ViewMode::mission) {
      camera_applied =
          viewer_->applyRegionInfoCamera(timeline_.regionInfo());
      // A pMarineViewer mission always has a valid launch camera: unspecified
      // components retain the MarineViewer defaults (zoom 1, pan 0,0).
      // Preserve partially configured or fully default mission cameras rather
      // than treating missing individual keys as an error and switching to fit.
      if(!camera_applied && mission_)
        camera_applied = true;
    }

    metadata_.fit_view = options_.view == ViewMode::fit || !camera_applied;
    if(options_.view == ViewMode::mission && !camera_applied) {
      std::cerr << "warning: no complete REGION_INFO or mission pan/zoom; "
                   "using fit view\n";
    }
    if(metadata_.fit_view &&
       !viewer_->fitToScene(metadata_.start, metadata_.end,
                            options_.geometry != ToggleMode::off)) {
      throw std::runtime_error(
          "cannot calculate a fit view because the requested interval has no "
          "vehicle tracks or visible geometry");
    }
  }

  Options options_;
  ALogTimeline timeline_;
  std::optional<MissionConfig> mission_;
  std::filesystem::path selected_map_;
  bool mapless_ = false;
  ScopedMapAlias map_alias_;
  std::unique_ptr<OffscreenContext> context_;
  std::unique_ptr<HeadlessSceneViewer> viewer_;
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
