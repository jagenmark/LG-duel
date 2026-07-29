#include "render/OptionMenuLayout.hpp"

#include <iostream>

namespace {

int expect(bool condition, const char *message) {
  if (condition) {
    return 0;
  }
  std::cerr << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;

  const lg::OptionMenuLayout desktop =
      lg::buildOptionMenuLayout(1920, 1080, 32U, 999U);
  failures += expect(
      desktop.visibleRows == 18U && desktop.maxScrollRows == 14U,
      "desktop menu should derive its scroll limit from the rows that fit");
  failures += expect(
      lg::optionMenuRowAt(desktop, desktop.maxScrollRows, 32U,
                          desktop.firstRowY +
                              static_cast<float>(desktop.visibleRows - 1U) *
                                  desktop.rowHeight) == 31,
      "maximum scroll should place the last item in the last visible row");

  const lg::OptionMenuLayout game =
      lg::buildOptionMenuLayout(1280, 720, 32U, 0U);
  failures += expect(game.visibleRows == 11U && game.maxScrollRows == 21U,
                     "720p menu should not use a fixed visible-row count");
  failures +=
      expect(lg::optionMenuPointInScrollbarThumb(
                 game, game.scrollbarThumbX,
                 game.scrollbarThumbY + game.scrollbarThumbHeight * 0.5F),
             "scrollbar thumb should have a mouse hit target");
  failures += expect(
      lg::optionMenuScrollForThumbPointer(
          game,
          game.scrollbarTrackY + game.scrollbarThumbTravel +
              game.scrollbarThumbHeight * 0.5F,
          game.scrollbarThumbHeight * 0.5F) == game.maxScrollRows,
      "dragging the thumb to the bottom should reach the exact last page");
  failures += expect(
      lg::optionMenuScrollForThumbPointer(
          game, game.scrollbarTrackY, game.scrollbarThumbHeight * 0.5F) == 0U,
      "dragging above the track should clamp to the first page");
  failures += expect(lg::optionMenuScrollForWheel(game, 6U, 1.0F) == 3U,
                     "wheel up should move toward the first row");
  failures += expect(lg::optionMenuScrollForWheel(game, 6U, -1.0F) == 9U,
                     "wheel down should move toward the final row");
  failures += expect(
      lg::optionMenuScrollForWheel(game, game.maxScrollRows + 12U, 1.0F) ==
          game.maxScrollRows - 3U,
      "wheel input should clamp a stale offset before moving");
  failures += expect(
      lg::optionMenuScrollForWheel(game, game.maxScrollRows, -1.0F) ==
          game.maxScrollRows,
      "wheel down should stop at the final full page");

  return failures == 0 ? 0 : 1;
}
