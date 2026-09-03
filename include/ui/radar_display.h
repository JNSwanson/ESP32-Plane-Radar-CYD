#pragma once

namespace ui {

enum class RadarTouchAction {
  kNone,
  kRangeChanged,
  kSweepToggled,
};

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Advance the optional rotating sweep when its next frame is due. */
void radarDisplayAnimate();

/** Handle a new touch press on either the radar or zoom controls. */
RadarTouchAction radarDisplayHandleTouch(int x, int y);

}  // namespace ui
