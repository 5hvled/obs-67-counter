#pragma once
#include "sixty-seven-filter.hpp"

// Lightweight V1 detector. It samples the raw asynchronous OBS camera frame
// directly (RGB or common 8-bit YUV formats), builds a low-resolution skin
// mask, and returns two separated hand-region candidates.
bool detect_hand_pair_frame(const obs_source_frame *frame, float min_blob_fraction,
                            HandPoint &a, HandPoint &b);
