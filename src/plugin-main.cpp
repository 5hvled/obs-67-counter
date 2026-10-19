#include <obs-module.h>
#include "pose-detector.hpp"
#include "sixty-seven-filter.hpp"
#include <obs-hotkey.h>

OBS_DECLARE_MODULE()

extern obs_source_info sixty_seven_filter_info;
extern obs_source_info sixty_seven_raw_filter_info;

static obs_hotkey_id g_start_temporary_hotkey = OBS_INVALID_HOTKEY_ID;

static void global_start_temporary_hotkey(void *, obs_hotkey_id,
                                          obs_hotkey_t *, bool pressed)
{
    if (pressed)
        sixty_seven_start_all_temporary_counters();
}

bool obs_module_load(void)
{
    // Main filter: standard Effect Filter, usable on cameras, media, browsers,
    // scenes, game/window capture, source clones, etc.
    obs_register_source(&sixty_seven_filter_info);

    // Optional raw async variant retained for direct camera-frame testing.
    obs_register_source(&sixty_seven_raw_filter_info);

    g_start_temporary_hotkey = obs_hotkey_register_frontend(
        "67Counter.StartTemporary",
        "67 Counter: Start temporary counter",
        global_start_temporary_hotkey, nullptr);

    if (g_start_temporary_hotkey == OBS_INVALID_HOTKEY_ID) {
        blog(LOG_ERROR,
             "[obs-67-counter] failed to register global temporary counter hotkey");
    }

    blog(LOG_INFO, "[obs-67-counter] loaded");
    return true;
}

void obs_module_unload(void)
{
    if (g_start_temporary_hotkey != OBS_INVALID_HOTKEY_ID) {
        obs_hotkey_unregister(g_start_temporary_hotkey);
        g_start_temporary_hotkey = OBS_INVALID_HOTKEY_ID;
    }

    pose_detector_shutdown();
}

const char *obs_module_description(void)
{
    return "Counts alternating high/low hand switches (67 gesture).";
}
