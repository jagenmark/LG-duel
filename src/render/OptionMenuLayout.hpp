#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lg {

struct OptionMenuLayout {
  float panelX = 0.0F;
  float panelY = 0.0F;
  float panelWidth = 0.0F;
  float panelHeight = 0.0F;
  float firstRowY = 0.0F;
  float footerY = 0.0F;
  float rowHeight = 38.0F;
  std::size_t visibleRows = 1U;
  std::size_t maxScrollRows = 0U;
  float scrollbarTrackX = 0.0F;
  float scrollbarTrackY = 0.0F;
  float scrollbarTrackWidth = 3.0F;
  float scrollbarTrackHeight = 0.0F;
  float scrollbarThumbX = 0.0F;
  float scrollbarThumbY = 0.0F;
  float scrollbarThumbWidth = 5.0F;
  float scrollbarThumbHeight = 0.0F;
  float scrollbarThumbTravel = 0.0F;
};

[[nodiscard]] inline OptionMenuLayout
buildOptionMenuLayout(int width, int height, std::size_t itemCount,
                      std::size_t scrollRows) {
  OptionMenuLayout layout;
  const float outputWidth = static_cast<float>(std::max(0, width));
  const float outputHeight = static_cast<float>(std::max(0, height));
  const float safeWidth = std::max(320.0F, outputWidth - 48.0F);
  const float safeHeight = std::max(260.0F, outputHeight - 48.0F);
  layout.panelWidth = std::min(safeWidth, outputWidth * 0.75F);
  layout.panelHeight = std::min(safeHeight, outputHeight * 0.75F);
  layout.panelX = (outputWidth - layout.panelWidth) * 0.5F;
  layout.panelY = (outputHeight - layout.panelHeight) * 0.45F;
  layout.firstRowY = layout.panelY + 78.0F;
  layout.footerY = layout.panelY + layout.panelHeight - 30.0F;
  layout.visibleRows = static_cast<std::size_t>(
      std::max(1.0F, std::floor((layout.footerY - layout.firstRowY - 8.0F) /
                                layout.rowHeight)));
  layout.maxScrollRows =
      itemCount > layout.visibleRows ? itemCount - layout.visibleRows : 0U;

  layout.scrollbarTrackX = layout.panelX + layout.panelWidth - 11.0F;
  layout.scrollbarTrackY = layout.panelY + 64.0F;
  layout.scrollbarTrackHeight =
      std::max(0.0F, layout.footerY - layout.scrollbarTrackY - 8.0F);
  layout.scrollbarThumbX = layout.panelX + layout.panelWidth - 12.0F;
  if (itemCount > layout.visibleRows && layout.scrollbarTrackHeight > 0.0F) {
    layout.scrollbarThumbHeight =
        std::min(layout.scrollbarTrackHeight,
                 std::max(18.0F, layout.scrollbarTrackHeight *
                                     static_cast<float>(layout.visibleRows) /
                                     static_cast<float>(itemCount)));
    layout.scrollbarThumbTravel = std::max(
        0.0F, layout.scrollbarTrackHeight - layout.scrollbarThumbHeight);
    const float progress =
        layout.maxScrollRows == 0U
            ? 0.0F
            : static_cast<float>(std::min(scrollRows, layout.maxScrollRows)) /
                  static_cast<float>(layout.maxScrollRows);
    layout.scrollbarThumbY =
        layout.scrollbarTrackY + layout.scrollbarThumbTravel * progress;
  } else {
    layout.scrollbarThumbY = layout.scrollbarTrackY;
  }
  return layout;
}

[[nodiscard]] inline bool
optionMenuPointInScrollbarTrack(const OptionMenuLayout &layout, float x,
                                float y) {
  constexpr float hitPadding = 8.0F;
  return layout.maxScrollRows > 0U &&
         x >= layout.scrollbarTrackX - hitPadding &&
         x <=
             layout.scrollbarTrackX + layout.scrollbarTrackWidth + hitPadding &&
         y >= layout.scrollbarTrackY &&
         y <= layout.scrollbarTrackY + layout.scrollbarTrackHeight;
}

[[nodiscard]] inline bool
optionMenuPointInScrollbarThumb(const OptionMenuLayout &layout, float x,
                                float y) {
  constexpr float hitPadding = 8.0F;
  return layout.maxScrollRows > 0U &&
         x >= layout.scrollbarThumbX - hitPadding &&
         x <=
             layout.scrollbarThumbX + layout.scrollbarThumbWidth + hitPadding &&
         y >= layout.scrollbarThumbY &&
         y <= layout.scrollbarThumbY + layout.scrollbarThumbHeight;
}

[[nodiscard]] inline std::size_t
optionMenuScrollForThumbPointer(const OptionMenuLayout &layout, float pointerY,
                                float grabOffsetY) {
  if (layout.maxScrollRows == 0U || layout.scrollbarThumbTravel <= 0.0F) {
    return 0U;
  }
  const float thumbY =
      std::clamp(pointerY - grabOffsetY, layout.scrollbarTrackY,
                 layout.scrollbarTrackY + layout.scrollbarThumbTravel);
  const float progress =
      (thumbY - layout.scrollbarTrackY) / layout.scrollbarThumbTravel;
  return static_cast<std::size_t>(
      std::lround(progress * static_cast<float>(layout.maxScrollRows)));
}

[[nodiscard]] inline std::size_t
optionMenuScrollForWheel(const OptionMenuLayout &layout,
                         std::size_t scrollRows, float wheelY,
                         std::size_t stepRows = 3U) {
  scrollRows = std::min(scrollRows, layout.maxScrollRows);
  if (wheelY > 0.0F) {
    return scrollRows > stepRows ? scrollRows - stepRows : 0U;
  }
  if (wheelY < 0.0F) {
    return std::min(scrollRows + stepRows, layout.maxScrollRows);
  }
  return scrollRows;
}

[[nodiscard]] inline int optionMenuRowAt(const OptionMenuLayout &layout,
                                         std::size_t scrollRows,
                                         std::size_t itemCount, float y) {
  const float rowsBottom =
      layout.firstRowY +
      static_cast<float>(std::min(layout.visibleRows, itemCount)) *
          layout.rowHeight;
  if (y < layout.firstRowY || y >= rowsBottom) {
    return -1;
  }
  const std::size_t row =
      std::min(scrollRows, layout.maxScrollRows) +
      static_cast<std::size_t>((y - layout.firstRowY) / layout.rowHeight);
  return row < itemCount ? static_cast<int>(row) : -1;
}

} // namespace lg
