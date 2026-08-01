#include "GeometryReplay.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if(!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

}  // namespace

int main() {
  const std::vector<alog2media::GeometryEvent> events = {
      {0.0, "VIEW_CIRCLE",
       "x=0,y=0,radius=5,label=target,duration=10", "shore"},
      {1.0, "VIEW_CIRCLE",
       "x=100,y=0,radius=7,label=target,duration=10", "shore"},
      {2.0, "VIEW_CIRCLE", "x=100,y=0,radius=7,label=target,active=false",
       "shore"},
      {3.0, "VIEW_POINT", "x=-50,y=10,label=brief,duration=1", "shore"},
  };

  alog2media::GeometryReplay replay(events, 1000.0);
  replay.advance(0.5);
  require(replay.shapes().getCircles().size() == 1,
          "a circle is active after its event");
  require(replay.shapes().getCircles().at("target").getX() == 0,
          "the first labeled shape is retained");

  replay.advance(1.5);
  require(replay.shapes().getCircles().size() == 1,
          "same-label replacement does not duplicate a shape");
  require(replay.shapes().getCircles().at("target").getX() == 100,
          "same-label replacement takes effect at its timestamp");

  replay.advance(2.5);
  require(replay.shapes().getCircles().empty(),
          "active=false removes the labeled shape");
  replay.advance(3.5);
  require(replay.shapes().getPoints().size() == 1,
          "duration geometry remains active inside its lifetime");
  replay.advance(4.1);
  require(replay.shapes().getPoints().empty(),
          "duration geometry expires against LOGSTART plus log time");

  replay.advance(1.5);
  require(replay.shapes().getCircles().size() == 1,
          "seeking backward resets and deterministically replays state");

  const alog2media::Bounds bounds =
      alog2media::geometryBounds(events, 1000.0, 0.0, 4.5);
  require(bounds.valid() && bounds.min_x <= -50 && bounds.max_x >= 107,
          "geometry fit spans replacement and duration states");

  alog2media::GeometryVisibility circles_only;
  circles_only.points = false;
  const alog2media::Bounds visible_bounds =
      alog2media::geometryBounds(events, 1000.0, 0.0, 4.5, circles_only);
  require(visible_bounds.valid() && visible_bounds.min_x > -50 &&
              visible_bounds.max_x >= 107,
          "geometry fit excludes a mission-hidden shape family");

  std::cout << "geometry replay tests passed\n";
  return 0;
}
