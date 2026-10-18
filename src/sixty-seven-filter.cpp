#include "sixty-seven-filter.hpp"
#include "hand-detector.hpp"

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
    update(f, s);
    return f;
}

static void destroy(void *data)
{
    delete static_cast<SixtySevenFilter *>(data);
}

static obs_properties_t *properties(void *)
{
    obs_properties_t *p = obs_properties_create();
    obs_properties_add_bool(p, "enabled", "Enable detection");
    obs_properties_add_bool(p, "debug", "Debug logging");
    obs_properties_add_float_slider(p, "dead_zone", "Vertical dead zone", 0.02, 0.30, 0.01);
    obs_properties_add_float_slider(p, "min_blob", "Minimum hand region", 0.001, 0.03, 0.0005);
    obs_properties_add_int_slider(p, "stable_frames", "Stable samples", 1, 15, 1);
    obs_properties_add_int_slider(p, "cooldown_ms", "Cooldown (ms)", 0, 1500, 25);
    return p;
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

    {
        std::lock_guard<std::mutex> lock(f->mutex);

        if (!found) {
            ++f->missing_frames;
            f->candidate = VerticalState::Unknown;
            f->candidate_frames = 0;
            if (f->missing_frames >= 10)
                f->committed = VerticalState::Unknown;
            return frame;
        }

        f->missing_frames = 0;
        f->a = a;
        f->b = b;
        consume_pair(f, a, b);

        if (debug && (sample_index % 90) == 0) {
            blog(LOG_DEBUG,
                 "[obs-67-counter] format=%s A=(%.3f,%.3f) B=(%.3f,%.3f) count=%llu",
                 get_video_format_name(frame->format), a.x, a.y, b.x, b.y,
                 (unsigned long long)f->count);
        }
    }

    return frame;
}

static obs_source_info make_filter_info()
{
    obs_source_info info = {};
    info.id = "sixty_seven_counter_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
    info.get_name = filter_name;
    info.create = create;
    info.destroy = destroy;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.update = update;
    info.filter_video = filter_video;
    return info;
}

obs_source_info sixty_seven_filter_info = make_filter_info();
