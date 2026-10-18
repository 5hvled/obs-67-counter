#pragma once
#include "sixty-seven-filter.hpp"

// Raw asynchronous OBS frame detector.
bool detect_hand_pair_frame(const obs_source_frame *frame, float min_blob_fraction,
                            HandPoint &a, HandPoint &b);

// BGRA detector used by the standard Effect Filter GPU readback path.
bool detect_hand_pair_bgra(const uint8_t *data, uint32_t width, uint32_t height,
                           uint32_t stride, float min_blob_fraction,
                           HandPoint &a, HandPoint &b);
