#pragma once
#include <obs-module.h>
#include <cstdint>
#include <vector>
#include <mutex>

struct HandPoint {
    float x = 0.0f;
    float y = 0.0f;
    float area = 0.0f;
    bool valid = false;
};

enum class VerticalState { Unknown, FirstAbove, SecondAbove };

struct SixtySevenFilter {
    obs_source_t *context = nullptr;
    gs_texrender_t *render = nullptr;
    gs_stagesurf_t *stage = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frame_index = 0;

    bool enabled = true;
    bool debug = true;
    float dead_zone = 0.08f;
    float min_blob_fraction = 0.0035f;
    int stable_frames_required = 4;
    uint64_t cooldown_ns = 250000000ULL;

    VerticalState candidate = VerticalState::Unknown;
    VerticalState committed = VerticalState::Unknown;
    int candidate_frames = 0;
    uint64_t last_count_ns = 0;
    uint64_t count = 0;

    HandPoint a, b;
    std::mutex mutex;
};
