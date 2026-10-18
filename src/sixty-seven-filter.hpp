#pragma once
#include <obs-module.h>
#include <obs-hotkey.h>
#include <graphics/graphics.h>
#include <cstdint>
#include <mutex>
#include <string>

struct HandPoint {
    float x = 0.0f;
    float y = 0.0f;
    float area = 0.0f;
    bool valid = false;
};

enum class VerticalState { Unknown, FirstAbove, SecondAbove };

struct SixtySevenFilter {
    obs_source_t *context = nullptr;

    uint64_t frame_index = 0;
    int missing_frames = 0;

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
    std::string text_source_name;

    obs_hotkey_id hotkey_reset = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hotkey_plus = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hotkey_minus = OBS_INVALID_HOTKEY_ID;

    HandPoint a, b;

    // Effect-filter sampling path. We render a tiny SDR copy of the target
    // only on sample frames, then use two staging surfaces as a ping-pong
    // GPU->CPU readback so normal rendering stays pass-through.
    gs_texrender_t *sample_render = nullptr;
    gs_stagesurf_t *stage[2] = {nullptr, nullptr};
    bool stage_pending[2] = {false, false};
    int stage_write = 0;
    uint32_t sample_width = 0;
    uint32_t sample_height = 0;

    std::mutex mutex;
};
