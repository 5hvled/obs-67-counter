#pragma once
#include "sixty-seven-filter.hpp"
#include <cstdint>

// Lightweight V1 detector. It finds skin-like connected regions in a downscaled
// BGRA frame and returns the two best hand candidates. This is intentionally
// dependency-free so we can get a real OBS build/test loop before swapping in
// a landmark model.
bool detect_hand_pair_bgra(const uint8_t *data, uint32_t width, uint32_t height,
                           uint32_t stride, float min_blob_fraction,
                           HandPoint &a, HandPoint &b);
