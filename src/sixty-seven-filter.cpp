#include "sixty-seven-filter.hpp"
#include "hand-detector.hpp"
#include "counter-state.hpp"
#include <util/platform.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char *filter_name(void *) { return "67 Counter"; }
static const char *raw_filter_name(void *) { return "67 Counter (Raw Camera)"; }

static void defaults(obs_data_t *s)
{
    obs_data_set_default_bool(s, "enabled", true);
    obs_data_set_default_bool(s, "debug", true);
    obs_data_set_default_bool(s, "show_tracking_markers", true);
    obs_data_set_default_double(s, "dead_zone", 0.08);
    obs_data_set_default_double(s, "min_blob", 0.0035);
    obs_data_set_default_int(s, "stable_frames", 4);
    obs_data_set_default_int(s, "cooldown_ms", 250);
    obs_data_set_default_int(s, "count", 0);
    obs_data_set_default_string(s, "text_source", "");
    obs_data_set_default_string(s, "text_prefix", "");
    obs_data_set_default_string(s, "text_postfix", "");
    obs_data_set_default_string(s, "challenge_text_source", "");
    obs_data_set_default_string(s, "challenge_prefix", "");
    obs_data_set_default_string(s, "challenge_postfix", "");
    obs_data_set_default_int(s, "challenge_duration_seconds", 10);
}

struct TextUpdateTask {
    obs_weak_source_t *weak = nullptr;
    std::string text;
};

static bool is_text_source(obs_source_t *source)
{
    if (!source)
        return false;

    const char *id = obs_source_get_unversioned_id(source);
    return id && (!strcmp(id, "text_gdiplus") ||
                  !strcmp(id, "text_ft2_source"));
}

static void apply_text_update(void *param)
{
    auto *task = static_cast<TextUpdateTask *>(param);
    obs_source_t *source = obs_weak_source_get_source(task->weak);

    if (source) {
        obs_data_t *settings = obs_source_get_settings(source);

        // Force manual-text mode for both OBS text implementations.
        obs_data_set_bool(settings, "read_from_file", false);
        obs_data_set_bool(settings, "from_file", false);
        obs_data_set_string(settings, "text", task->text.c_str());

        obs_source_update(source, settings);
        obs_data_release(settings);
        obs_source_release(source);
    }

    obs_weak_source_release(task->weak);
    delete task;
}

static void queue_text_source_update(const std::string &source_name,
                                     const std::string &text)
{
    if (source_name.empty())
        return;

    obs_source_t *source = obs_get_source_by_name(source_name.c_str());
    if (!source)
        return;

    if (!is_text_source(source)) {
        obs_source_release(source);
        return;
    }

    auto *task = new TextUpdateTask;
    task->weak = obs_source_get_weak_source(source);
    task->text = text;
    obs_source_release(source);

    obs_queue_task(OBS_TASK_UI, apply_text_update, task, false);
}

static void sync_text_source(SixtySevenFilter *f, uint64_t value)
{
    std::string source_name;
    std::string prefix;
    std::string postfix;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        source_name = f->text_source_name;
        prefix = f->text_prefix;
        postfix = f->text_postfix;
    }

    const std::string text =
        prefix + std::to_string((unsigned long long)value) + postfix;
    queue_text_source_update(source_name, text);
}

static void sync_challenge_text_source(SixtySevenFilter *f,
                                       uint64_t value, bool visible)
{
    std::string source_name;
    std::string prefix;
    std::string postfix;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        source_name = f->challenge_text_source_name;
        prefix = f->challenge_prefix;
        postfix = f->challenge_postfix;
    }

    if (!visible) {
        queue_text_source_update(source_name, "");
        return;
    }

    const std::string text =
        prefix + std::to_string((unsigned long long)value) + postfix;
    queue_text_source_update(source_name, text);
}

static void start_challenge(SixtySevenFilter *f)
{
    uint64_t duration_ns;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->challenge_count = 0;
        f->challenge_active = true;
        duration_ns = (uint64_t)std::max(1, f->challenge_duration_seconds) *
                      1000000000ULL;
        f->challenge_end_ns = os_gettime_ns() + duration_ns;
    }

    sync_challenge_text_source(f, 0, true);
    blog(LOG_INFO, "[obs-67-counter] temporary challenge started");
}

static void maybe_expire_challenge(SixtySevenFilter *f)
{
    bool expired = false;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (f->challenge_active && os_gettime_ns() >= f->challenge_end_ns) {
            f->challenge_active = false;
            f->challenge_count = 0;
            expired = true;
        }
    }

    if (expired) {
        sync_challenge_text_source(f, 0, false);
        blog(LOG_INFO, "[obs-67-counter] temporary challenge ended");
    }
}

static void count_temporary_challenge(SixtySevenFilter *f)
{
    bool counted = false;
    bool expired = false;
    uint64_t value = 0;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (f->challenge_active) {
            if (os_gettime_ns() >= f->challenge_end_ns) {
                f->challenge_active = false;
                f->challenge_count = 0;
                expired = true;
            } else {
                value = ++f->challenge_count;
                counted = true;
            }
        }
    }

    if (expired)
        sync_challenge_text_source(f, 0, false);
    else if (counted)
        sync_challenge_text_source(f, value, true);
}

static void update(void *data, obs_data_t *s)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->enabled = obs_data_get_bool(s, "enabled");
        f->debug = obs_data_get_bool(s, "debug");
        f->show_tracking_markers = obs_data_get_bool(s, "show_tracking_markers");
        f->dead_zone = (float)obs_data_get_double(s, "dead_zone");
        f->min_blob_fraction = (float)obs_data_get_double(s, "min_blob");
        f->stable_frames_required = (int)obs_data_get_int(s, "stable_frames");
        f->cooldown_ns = (uint64_t)obs_data_get_int(s, "cooldown_ms") * 1000000ULL;
        f->text_source_name = obs_data_get_string(s, "text_source");
        f->text_prefix = obs_data_get_string(s, "text_prefix");
        f->text_postfix = obs_data_get_string(s, "text_postfix");
        f->challenge_text_source_name = obs_data_get_string(s, "challenge_text_source");
        f->challenge_prefix = obs_data_get_string(s, "challenge_prefix");
        f->challenge_postfix = obs_data_get_string(s, "challenge_postfix");
        f->challenge_duration_seconds =
            (int)obs_data_get_int(s, "challenge_duration_seconds");
    }

    sync_text_source(f, counter_get());

    bool challenge_active;
    uint64_t challenge_value;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        challenge_active = f->challenge_active;
        challenge_value = f->challenge_count;
    }
    sync_challenge_text_source(f, challenge_value, challenge_active);
}

static void adjust_count(SixtySevenFilter *f, int delta, bool reset)
{
    const uint64_t value = reset ? (counter_set(0), 0) : counter_add(delta);
    blog(LOG_INFO, "[obs-67-counter] manual count=%llu",
         (unsigned long long)value);
    sync_text_source(f, value);
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

static void hotkey_start_challenge(void *data, obs_hotkey_id,
                                   obs_hotkey_t *, bool pressed)
{
    if (pressed)
        start_challenge(static_cast<SixtySevenFilter *>(data));
}

static void *create(obs_data_t *s, obs_source_t *source)
{
    auto *f = new SixtySevenFilter;
    f->context = source;
    update(f, s);

    {
        const int64_t saved_count = obs_data_get_int(s, "count");
        counter_set(saved_count > 0 ? (uint64_t)saved_count : 0);
    }
    sync_text_source(f, counter_get());

    f->hotkey_reset = obs_hotkey_register_source(
        source, "67Counter.Reset", "67 Counter: Reset", hotkey_reset, f);
    f->hotkey_plus = obs_hotkey_register_source(
        source, "67Counter.PlusOne", "67 Counter: +1", hotkey_plus, f);
    f->hotkey_minus = obs_hotkey_register_source(
        source, "67Counter.MinusOne", "67 Counter: -1", hotkey_minus, f);
    f->hotkey_start_challenge = obs_hotkey_register_source(
        source, "67Counter.StartTemporary",
        "67 Counter: Start temporary counter", hotkey_start_challenge, f);

    sync_challenge_text_source(f, 0, false);
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
    if (f->debug_marker_texture) {
        gs_texture_destroy(f->debug_marker_texture);
        f->debug_marker_texture = nullptr;
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

    if (f->sample_render || f->stage[0] || f->stage[1] || f->debug_marker_texture) {
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

static bool button_start_challenge(obs_properties_t *, obs_property_t *, void *data)
{
    start_challenge(static_cast<SixtySevenFilter *>(data));
    return true;
}

static bool enum_text_source(void *param, obs_source_t *source)
{
    auto *list = static_cast<obs_property_t *>(param);
    if (!is_text_source(source))
        return true;

    const char *name = obs_source_get_name(source);
    if (name && *name)
        obs_property_list_add_string(list, name, name);

    return true;
}

static obs_properties_t *properties(void *data)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    obs_properties_t *p = obs_properties_create();

    obs_property_t *text_sources = obs_properties_add_list(
        p, "text_source", "Counter text source",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(text_sources, "None", "");
    obs_enum_sources(enum_text_source, text_sources);

    obs_properties_add_text(p, "text_prefix", "Prefix", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "text_postfix", "Postfix", OBS_TEXT_DEFAULT);

    obs_property_t *challenge_sources = obs_properties_add_list(
        p, "challenge_text_source", "Temporary counter text source",
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(challenge_sources, "None", "");
    obs_enum_sources(enum_text_source, challenge_sources);
    obs_properties_add_text(p, "challenge_prefix",
                            "Temporary prefix", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "challenge_postfix",
                            "Temporary postfix", OBS_TEXT_DEFAULT);
    obs_properties_add_int_slider(p, "challenge_duration_seconds",
                                  "Temporary counter duration (seconds)",
                                  1, 60, 1);

    if (f) {
        const uint64_t count = counter_get();
        char status[96];
        snprintf(status, sizeof(status), "Current count: %llu",
                 (unsigned long long)count);
        obs_properties_add_text(p, "count_status", status, OBS_TEXT_INFO);
        obs_properties_add_button2(p, "count_plus", "+1", button_plus, f);
        obs_properties_add_button2(p, "count_minus", "-1", button_minus, f);
        obs_properties_add_button2(p, "count_reset", "Reset counter", button_reset, f);
        obs_properties_add_button2(p, "challenge_start",
                                   "Start temporary counter now",
                                   button_start_challenge, f);
    }
    obs_properties_add_bool(p, "enabled", "Enable detection");
    obs_properties_add_bool(p, "debug", "Debug logging");
    obs_properties_add_bool(p, "show_tracking_markers",
                            "Show green hand tracking markers");
    obs_properties_add_float_slider(p, "dead_zone", "Vertical dead zone", 0.02, 0.30, 0.01);
    obs_properties_add_float_slider(p, "min_blob", "Minimum hand region", 0.001, 0.03, 0.0005);
    obs_properties_add_int_slider(p, "stable_frames", "Stable samples", 1, 15, 1);
    obs_properties_add_int_slider(p, "cooldown_ms", "Cooldown (ms)", 0, 1500, 25);
    return p;
}

static void save(void *, obs_data_t *settings)
{
    obs_data_set_int(settings, "count", (long long)counter_get());
}

static void reset_detection_after_loss(SixtySevenFilter *f)
{
    ++f->missing_frames;
    f->candidate = VerticalState::Unknown;
    f->candidate_frames = 0;
    f->a.valid = false;
    f->b.valid = false;
    if (f->missing_frames >= 10)
        f->committed = VerticalState::Unknown;
}

static uint64_t consume_pair(SixtySevenFilter *f, const HandPoint &a, const HandPoint &b)
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
        return UINT64_MAX;
    }

    if (observed != f->candidate) {
        f->candidate = observed;
        f->candidate_frames = 1;
        return UINT64_MAX;
    }

    ++f->candidate_frames;
    if (f->candidate_frames < f->stable_frames_required)
        return UINT64_MAX;
    if (observed == f->committed)
        return UINT64_MAX;

    uint64_t changed_count = UINT64_MAX;
    const uint64_t now = os_gettime_ns();

    if (f->committed != VerticalState::Unknown &&
        now - f->last_count_ns >= f->cooldown_ns) {
        changed_count = counter_add(1);
        f->last_count_ns = now;
        blog(LOG_INFO, "[obs-67-counter] 67! count=%llu",
             (unsigned long long)changed_count);
    }

    // Commit the new high/low state whether or not cooldown allowed a count.
    // This prevents one crossover from counting repeatedly while held.
    f->committed = observed;
    return changed_count;
}

static void accept_detection(SixtySevenFilter *f, bool found,
                             const HandPoint &a, const HandPoint &b,
                             bool debug, uint64_t sample_index,
                             const char *path_name)
{
    uint64_t changed_count = UINT64_MAX;

    {
        std::lock_guard<std::mutex> lock(f->mutex);

        if (!found) {
            reset_detection_after_loss(f);
            return;
        }

        f->missing_frames = 0;
        f->a = a;
        f->b = b;
        changed_count = consume_pair(f, a, b);

        if (debug && (sample_index % 90) == 0) {
            blog(LOG_DEBUG,
                 "[obs-67-counter] %s A=(%.3f,%.3f) B=(%.3f,%.3f) count=%llu",
                 path_name, a.x, a.y, b.x, b.y,
                 (unsigned long long)counter_get());
        }
    }

    if (changed_count != UINT64_MAX) {
        sync_text_source(f, changed_count);
        count_temporary_challenge(f);
    }
}

/* ------------------------------------------------------------------------- */
/* Raw asynchronous camera path (Audio/Video Filters)                        */

static obs_source_frame *filter_video(void *data, obs_source_frame *frame)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    if (!frame)
        return frame;

    maybe_expire_challenge(f);

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

static bool ensure_debug_marker_texture(SixtySevenFilter *f)
{
    if (f->debug_marker_texture)
        return true;

    const uint32_t size = 40;
    std::vector<uint8_t> pixels((size_t)size * size * 4, 0);
    const float center = (float)(size - 1) * 0.5f;
    const float outer2 = 18.0f * 18.0f;
    const float inner2 = 13.0f * 13.0f;
    const float dot2 = 3.0f * 3.0f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float dx = (float)x - center;
            const float dy = (float)y - center;
            const float d2 = dx * dx + dy * dy;
            if ((d2 <= outer2 && d2 >= inner2) || d2 <= dot2) {
                const size_t off = ((size_t)y * size + x) * 4;
                pixels[off + 0] = 0;
                pixels[off + 1] = 255;
                pixels[off + 2] = 0;
                pixels[off + 3] = 255;
            }
        }
    }

    const uint8_t *initial = pixels.data();
    f->debug_marker_texture =
        gs_texture_create(size, size, GS_RGBA, 1, &initial, 0);
    return f->debug_marker_texture != nullptr;
}

static void draw_tracking_markers(SixtySevenFilter *f,
                                  uint32_t width, uint32_t height)
{
    bool show;
    HandPoint a;
    HandPoint b;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        show = f->show_tracking_markers;
        a = f->a;
        b = f->b;
    }

    if (!show || (!a.valid && !b.valid) ||
        !ensure_debug_marker_texture(f))
        return;

    const float marker_size = 40.0f;
    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, f->debug_marker_texture);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

    const auto draw_one = [&](const HandPoint &p) {
        if (!p.valid)
            return;

        const float x = p.x * (float)width - marker_size * 0.5f;
        const float y = p.y * (float)height - marker_size * 0.5f;

        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        while (gs_effect_loop(effect, "Draw"))
            gs_draw_sprite(f->debug_marker_texture, 0,
                           (uint32_t)marker_size,
                           (uint32_t)marker_size);
        gs_matrix_pop();
    };

    draw_one(a);
    draw_one(b);
    gs_blend_state_pop();
}

static void effect_render(void *data, gs_effect_t *)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    maybe_expire_challenge(f);

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

    const uint32_t output_width = obs_source_get_base_width(target);
    const uint32_t output_height = obs_source_get_base_height(target);
    if (output_width && output_height)
        draw_tracking_markers(f, output_width, output_height);
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
