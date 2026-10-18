#include "sixty-seven-filter.hpp"
#include "hand-detector.hpp"
#include <util/platform.h>
#include <algorithm>
#include <cstdio>

static const char *filter_name(void *) { return "67 Counter"; }
static const char *raw_filter_name(void *) { return "67 Counter (Raw Camera)"; }

static void defaults(obs_data_t *s)
{
    obs_data_set_default_bool(s, "enabled", true);
    obs_data_set_default_bool(s, "debug", true);
    obs_data_set_default_double(s, "dead_zone", 0.08);
    obs_data_set_default_double(s, "min_blob", 0.0035);
    obs_data_set_default_int(s, "stable_frames", 4);
    obs_data_set_default_int(s, "cooldown_ms", 250);
    obs_data_set_default_int(s, "count", 0);
}

static void update(void *data, obs_data_t *s)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    std::lock_guard<std::mutex> lock(f->mutex);
    f->enabled = obs_data_get_bool(s, "enabled");
    f->debug = obs_data_get_bool(s, "debug");
    f->dead_zone = (float)obs_data_get_double(s, "dead_zone");
    f->min_blob_fraction = (float)obs_data_get_double(s, "min_blob");
    f->stable_frames_required = (int)obs_data_get_int(s, "stable_frames");
    f->cooldown_ns = (uint64_t)obs_data_get_int(s, "cooldown_ms") * 1000000ULL;
}

static void adjust_count(SixtySevenFilter *f, int delta, bool reset)
{
    std::lock_guard<std::mutex> lock(f->mutex);
    if (reset) {
        f->count = 0;
    } else if (delta > 0) {
        f->count += (uint64_t)delta;
    } else if (delta < 0) {
        const uint64_t amount = (uint64_t)(-delta);
        f->count = (f->count > amount) ? (f->count - amount) : 0;
    }

    blog(LOG_INFO, "[obs-67-counter] manual count=%llu",
         (unsigned long long)f->count);
}

static void hotkey_reset(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (pressed)
        adjust_count(static_cast<SixtySevenFilter *>(data), 0, true);
}

static void hotkey_plus(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (pressed)
        adjust_count(static_cast<SixtySevenFilter *>(data), 1, false);
}

static void hotkey_minus(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (pressed)
        adjust_count(static_cast<SixtySevenFilter *>(data), -1, false);
}

static void *create(obs_data_t *s, obs_source_t *source)
{
    auto *f = new SixtySevenFilter;
    f->context = source;
    update(f, s);

    {
        std::lock_guard<std::mutex> lock(f->mutex);
        const int64_t saved_count = obs_data_get_int(s, "count");
        f->count = saved_count > 0 ? (uint64_t)saved_count : 0;
    }

    f->hotkey_reset = obs_hotkey_register_source(
        source, "67Counter.Reset", "67 Counter: Reset", hotkey_reset, f);
    f->hotkey_plus = obs_hotkey_register_source(
        source, "67Counter.PlusOne", "67 Counter: +1", hotkey_plus, f);
    f->hotkey_minus = obs_hotkey_register_source(
        source, "67Counter.MinusOne", "67 Counter: -1", hotkey_minus, f);

    return f;
}

static void destroy_graphics_resources(SixtySevenFilter *f)
{
    if (f->stage[0]) {
        gs_stagesurface_destroy(f->stage[0]);
        f->stage[0] = nullptr;
    }
    if (f->stage[1]) {
        gs_stagesurface_destroy(f->stage[1]);
        f->stage[1] = nullptr;
    }
    if (f->sample_render) {
        gs_texrender_destroy(f->sample_render);
        f->sample_render = nullptr;
    }
    f->stage_pending[0] = false;
    f->stage_pending[1] = false;
    f->sample_width = 0;
    f->sample_height = 0;
}

static void destroy(void *data)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    if (!f)
        return;

    if (f->sample_render || f->stage[0] || f->stage[1]) {
        obs_enter_graphics();
        destroy_graphics_resources(f);
        obs_leave_graphics();
    }

    delete f;
}

static bool button_reset(obs_properties_t *, obs_property_t *, void *data)
{
    adjust_count(static_cast<SixtySevenFilter *>(data), 0, true);
    return true;
}

static bool button_plus(obs_properties_t *, obs_property_t *, void *data)
{
    adjust_count(static_cast<SixtySevenFilter *>(data), 1, false);
    return true;
}

static bool button_minus(obs_properties_t *, obs_property_t *, void *data)
{
    adjust_count(static_cast<SixtySevenFilter *>(data), -1, false);
    return true;
}

static obs_properties_t *properties(void *data)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    obs_properties_t *p = obs_properties_create();

    if (f) {
        uint64_t count;
        {
            std::lock_guard<std::mutex> lock(f->mutex);
            count = f->count;
        }
        char status[96];
        snprintf(status, sizeof(status), "Current count: %llu",
                 (unsigned long long)count);
        obs_properties_add_text(p, "count_status", status, OBS_TEXT_INFO);
        obs_properties_add_button2(p, "count_plus", "+1", button_plus, f);
        obs_properties_add_button2(p, "count_minus", "-1", button_minus, f);
        obs_properties_add_button2(p, "count_reset", "Reset counter", button_reset, f);
    }
    obs_properties_add_bool(p, "enabled", "Enable detection");
    obs_properties_add_bool(p, "debug", "Debug logging");
    obs_properties_add_float_slider(p, "dead_zone", "Vertical dead zone", 0.02, 0.30, 0.01);
    obs_properties_add_float_slider(p, "min_blob", "Minimum hand region", 0.001, 0.03, 0.0005);
    obs_properties_add_int_slider(p, "stable_frames", "Stable samples", 1, 15, 1);
    obs_properties_add_int_slider(p, "cooldown_ms", "Cooldown (ms)", 0, 1500, 25);
    return p;
}

static void save(void *data, obs_data_t *settings)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    std::lock_guard<std::mutex> lock(f->mutex);
    obs_data_set_int(settings, "count", (long long)f->count);
}

static void reset_detection_after_loss(SixtySevenFilter *f)
{
    ++f->missing_frames;
    f->candidate = VerticalState::Unknown;
    f->candidate_frames = 0;
    if (f->missing_frames >= 10)
        f->committed = VerticalState::Unknown;
}

static void consume_pair(SixtySevenFilter *f, const HandPoint &a, const HandPoint &b)
{
    const float dy = a.y - b.y;
    VerticalState observed = VerticalState::Unknown;
    if (dy < -f->dead_zone)
        observed = VerticalState::FirstAbove;
    else if (dy > f->dead_zone)
        observed = VerticalState::SecondAbove;

    if (observed == VerticalState::Unknown) {
        f->candidate = VerticalState::Unknown;
        f->candidate_frames = 0;
        return;
    }

    if (observed != f->candidate) {
        f->candidate = observed;
        f->candidate_frames = 1;
        return;
    }

    ++f->candidate_frames;
    if (f->candidate_frames < f->stable_frames_required)
        return;
    if (observed == f->committed)
        return;

    const uint64_t now = os_gettime_ns();
    if (f->committed != VerticalState::Unknown &&
        now - f->last_count_ns >= f->cooldown_ns) {
        ++f->count;
        f->last_count_ns = now;
        blog(LOG_INFO, "[obs-67-counter] 67! count=%llu",
             (unsigned long long)f->count);
    }

    f->committed = observed;
}

static void accept_detection(SixtySevenFilter *f, bool found,
                             const HandPoint &a, const HandPoint &b,
                             bool debug, uint64_t sample_index,
                             const char *path_name)
{
    std::lock_guard<std::mutex> lock(f->mutex);

    if (!found) {
        reset_detection_after_loss(f);
        return;
    }

    f->missing_frames = 0;
    f->a = a;
    f->b = b;
    consume_pair(f, a, b);

    if (debug && (sample_index % 90) == 0) {
        blog(LOG_DEBUG,
             "[obs-67-counter] %s A=(%.3f,%.3f) B=(%.3f,%.3f) count=%llu",
             path_name, a.x, a.y, b.x, b.y,
             (unsigned long long)f->count);
    }
}

/* ------------------------------------------------------------------------- */
/* Raw asynchronous camera path (Audio/Video Filters)                        */

static obs_source_frame *filter_video(void *data, obs_source_frame *frame)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    if (!frame)
        return frame;

    bool enabled;
    bool debug;
    float min_blob;
    uint64_t sample_index;

    {
        std::lock_guard<std::mutex> lock(f->mutex);
        enabled = f->enabled;
        debug = f->debug;
        min_blob = f->min_blob_fraction;
        sample_index = ++f->frame_index;
    }

    if (!enabled || (sample_index % 3) != 0)
        return frame;

    HandPoint a, b;
    const bool found = detect_hand_pair_frame(frame, min_blob, a, b);
    accept_detection(f, found, a, b, debug, sample_index, "raw");
    return frame;
}

/* ------------------------------------------------------------------------- */
/* Standard Effect Filter path                                               */

static void choose_sample_size(uint32_t width, uint32_t height,
                               uint32_t &sample_w, uint32_t &sample_h)
{
    const uint32_t max_dim = 320;
    if (width >= height) {
        sample_w = max_dim;
        sample_h = std::max<uint32_t>(64, (uint32_t)((uint64_t)height * max_dim / width));
    } else {
        sample_h = max_dim;
        sample_w = std::max<uint32_t>(64, (uint32_t)((uint64_t)width * max_dim / height));
    }
}

static bool ensure_effect_resources(SixtySevenFilter *f,
                                    uint32_t sample_w, uint32_t sample_h)
{
    if (!f->sample_render)
        f->sample_render = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

    if (!f->sample_render)
        return false;

    if (f->sample_width != sample_w || f->sample_height != sample_h ||
        !f->stage[0] || !f->stage[1]) {
        if (f->stage[0])
            gs_stagesurface_destroy(f->stage[0]);
        if (f->stage[1])
            gs_stagesurface_destroy(f->stage[1]);

        f->stage[0] = gs_stagesurface_create(sample_w, sample_h, GS_BGRA);
        f->stage[1] = gs_stagesurface_create(sample_w, sample_h, GS_BGRA);
        f->stage_pending[0] = false;
        f->stage_pending[1] = false;
        f->stage_write = 0;
        f->sample_width = sample_w;
        f->sample_height = sample_h;
    }

    return f->stage[0] && f->stage[1];
}

static bool render_target_to_sample(SixtySevenFilter *f,
                                    obs_source_t *target,
                                    obs_source_t *parent,
                                    uint32_t width, uint32_t height,
                                    uint32_t sample_w, uint32_t sample_h)
{
    gs_texrender_reset(f->sample_render);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

    bool rendered = false;
    if (gs_texrender_begin_with_color_space(f->sample_render,
                                            sample_w, sample_h,
                                            GS_CS_SRGB)) {
        vec4 clear_color;
        vec4_zero(&clear_color);
        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

        const uint32_t flags = obs_source_get_output_flags(target);
        const bool custom_draw = (flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
        const bool async = (flags & OBS_SOURCE_ASYNC) != 0;

        if (target == parent && !custom_draw && !async)
            obs_source_default_render(target);
        else
            obs_source_video_render(target);

        gs_texrender_end(f->sample_render);
        rendered = true;
    }

    gs_blend_state_pop();
    return rendered;
}

static void process_previous_stage(SixtySevenFilter *f, int index,
                                   float min_blob, bool debug,
                                   uint64_t sample_index)
{
    if (!f->stage_pending[index] || !f->stage[index])
        return;

    uint8_t *data = nullptr;
    uint32_t linesize = 0;

    if (gs_stagesurface_map(f->stage[index], &data, &linesize)) {
        HandPoint a, b;
        const bool found = detect_hand_pair_bgra(
            data, f->sample_width, f->sample_height, linesize,
            min_blob, a, b);
        gs_stagesurface_unmap(f->stage[index]);

        accept_detection(f, found, a, b, debug, sample_index, "effect");
    }

    f->stage_pending[index] = false;
}

static void effect_render(void *data, gs_effect_t *)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    obs_source_t *target = obs_filter_get_target(f->context);
    obs_source_t *parent = obs_filter_get_parent(f->context);

    if (!target || !parent) {
        obs_source_skip_video_filter(f->context);
        return;
    }

    bool enabled;
    bool debug;
    float min_blob;
    uint64_t sample_index;

    {
        std::lock_guard<std::mutex> lock(f->mutex);
        enabled = f->enabled;
        debug = f->debug;
        min_blob = f->min_blob_fraction;
        sample_index = ++f->frame_index;
    }

    // Always remain visually pass-through. Sampling only happens ~20 times/s
    // at 60 FPS and at <=320 px on the long edge.
    if (enabled && (sample_index % 3) == 0) {
        const uint32_t width = obs_source_get_base_width(target);
        const uint32_t height = obs_source_get_base_height(target);

        if (width && height) {
            uint32_t sample_w, sample_h;
            choose_sample_size(width, height, sample_w, sample_h);

            if (ensure_effect_resources(f, sample_w, sample_h)) {
                const int read_index = 1 - f->stage_write;
                process_previous_stage(f, read_index, min_blob, debug, sample_index);

                if (render_target_to_sample(f, target, parent,
                                            width, height,
                                            sample_w, sample_h)) {
                    gs_texture_t *tex = gs_texrender_get_texture(f->sample_render);
                    if (tex) {
                        gs_stage_texture(f->stage[f->stage_write], tex);
                        f->stage_pending[f->stage_write] = true;
                        f->stage_write = 1 - f->stage_write;
                    }
                }
            }
        }
    }

    obs_source_skip_video_filter(f->context);
}

static enum gs_color_space effect_color_space(void *data, size_t count,
                                              const enum gs_color_space *preferred)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    obs_source_t *target = obs_filter_get_target(f->context);
    return target ? obs_source_get_color_space(target, count, preferred)
                  : ((count > 0) ? preferred[0] : GS_CS_SRGB);
}

static obs_source_info make_effect_filter_info()
{
    obs_source_info info = {};
    info.id = "sixty_seven_counter_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = filter_name;
    info.create = create;
    info.destroy = destroy;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.update = update;
    info.save = save;
    info.video_render = effect_render;
    info.video_get_color_space = effect_color_space;
    return info;
}

static obs_source_info make_raw_filter_info()
{
    obs_source_info info = {};
    info.id = "sixty_seven_counter_raw_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
    info.get_name = raw_filter_name;
    info.create = create;
    info.destroy = destroy;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.update = update;
    info.save = save;
    info.filter_video = filter_video;
    return info;
}

obs_source_info sixty_seven_filter_info = make_effect_filter_info();
obs_source_info sixty_seven_raw_filter_info = make_raw_filter_info();
