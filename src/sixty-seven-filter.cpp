#include "sixty-seven-filter.hpp"
#include <graphics/graphics.h>
#include <algorithm>
#include <cmath>

static const char *filter_name(void *) { return "67 Counter"; }

static void defaults(obs_data_t *s)
{
    obs_data_set_default_bool(s, "enabled", true);
    obs_data_set_default_bool(s, "debug", true);
    obs_data_set_default_double(s, "dead_zone", 0.08);
    obs_data_set_default_double(s, "min_blob", 0.0035);
    obs_data_set_default_int(s, "stable_frames", 4);
    obs_data_set_default_int(s, "cooldown_ms", 250);
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

static void *create(obs_data_t *s, obs_source_t *source)
{
    auto *f = new SixtySevenFilter;
    f->context = source;
    f->render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    update(f, s);
    return f;
}

static void destroy(void *data)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    if (f->stage) gs_stagesurface_destroy(f->stage);
    if (f->render) gs_texrender_destroy(f->render);
    delete f;
}

static obs_properties_t *properties(void *)
{
    obs_properties_t *p = obs_properties_create();
    obs_properties_add_bool(p, "enabled", "Enable detection");
    obs_properties_add_bool(p, "debug", "Debug overlay");
    obs_properties_add_float_slider(p, "dead_zone", "Vertical dead zone", 0.02, 0.30, 0.01);
    obs_properties_add_float_slider(p, "min_blob", "Minimum hand region", 0.001, 0.03, 0.0005);
    obs_properties_add_int_slider(p, "stable_frames", "Stable frames", 1, 15, 1);
    obs_properties_add_int_slider(p, "cooldown_ms", "Cooldown (ms)", 0, 1500, 25);
    return p;
}

/*
 * V1 calibration detector.
 *
 * We intentionally keep the OBS capture/state-machine plumbing separate from
 * the detector. detect_hands() will be replaced by the landmark backend after
 * validating camera orientation and performance on the target setup.
 *
 * Returning false means "no confident pair" and therefore can never increment
 * the counter. This fail-closed behavior prevents false counts in this scaffold.
 */
static bool detect_hands(const uint8_t *, uint32_t, uint32_t, uint32_t,
                         float, HandPoint &, HandPoint &)
{
    return false;
}

static void consume_pair(SixtySevenFilter *f, const HandPoint &a, const HandPoint &b)
{
    const float dy = a.y - b.y;
    VerticalState observed = VerticalState::Unknown;
    if (dy < -f->dead_zone) observed = VerticalState::FirstAbove;
    else if (dy > f->dead_zone) observed = VerticalState::SecondAbove;

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

    if (++f->candidate_frames < f->stable_frames_required) return;
    if (observed == f->committed) return;

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

static void video_render(void *data, gs_effect_t *)
{
    auto *f = static_cast<SixtySevenFilter *>(data);
    obs_source_t *target = obs_filter_get_target(f->context);
    if (!target) return;

    const uint32_t w = obs_source_get_base_width(target);
    const uint32_t h = obs_source_get_base_height(target);
    if (!w || !h) return;

    /*
     * Pass-through is deliberate in this first commit. The next detector commit
     * stages a downscaled frame for CPU inference; inference must never block
     * OBS's render path.
     */
    obs_source_skip_video_filter(f->context);
}

obs_source_info sixty_seven_filter_info = {
    .id = "sixty_seven_counter_filter",
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = filter_name,
    .create = create,
    .destroy = destroy,
    .get_defaults = defaults,
    .get_properties = properties,
    .update = update,
    .video_render = video_render,
};
