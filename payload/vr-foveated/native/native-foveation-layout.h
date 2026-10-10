#pragma once
#include <algorithm>
#include <cstdint>
namespace native_foveation {
struct Rect {
  unsigned x = 0, y = 0, w = 0, h = 0;
};
inline bool valid(Rect r, unsigned w, unsigned h) {
  return r.w && r.h && r.x <= w && r.y <= h && r.w <= w - r.x && r.h <= h - r.y;
}
inline Rect crop(Rect eye, unsigned width, unsigned height) {
  // Align crop dimensions to eight pixels. Never expand
  // beyond the original eye viewport, including odd-sized engine buffers.
  unsigned w = (eye.w * std::clamp(width, 35u, 90u) / 100u) & ~7u;
  unsigned h = (eye.h * std::clamp(height, 35u, 90u) / 100u) & ~7u;
  if (w < 64 || h < 64)
    return {};
  return {eye.x + (eye.w - w) / 2, eye.y + (eye.h - h) / 2, w, h};
}
inline Rect scale(Rect crop, Rect color, Rect guide) {
  // Coordinates are relative to the engine's valid subrectangle, rather
  // than the allocation. Cover exactly the same region in every guide.
  auto floor = [](uint64_t x, unsigned n, unsigned d) {
    return unsigned(x * n / d);
  };
  auto ceil = [](uint64_t x, unsigned n, unsigned d) {
    return unsigned((x * n + d - 1) / d);
  };
  unsigned x = floor(crop.x - color.x, guide.w, color.w),
           y = floor(crop.y - color.y, guide.h, color.h);
  unsigned right = ceil(uint64_t(crop.x - color.x) + crop.w, guide.w, color.w);
  unsigned bottom = ceil(uint64_t(crop.y - color.y) + crop.h, guide.h, color.h);
  return {guide.x + x, guide.y + y, right - x, bottom - y};
}
} // namespace native_foveation
