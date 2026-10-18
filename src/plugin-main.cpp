#include <obs-module.h>
#include "counter-display.hpp"

OBS_DECLARE_MODULE()

extern obs_source_info sixty_seven_filter_info;
extern obs_source_info sixty_seven_raw_filter_info;

bool obs_module_load(void)
{
    // Main filter: standard Effect Filter, usable on cameras, media, browsers,
    // scenes, game/window capture, source clones, etc.
    obs_register_source(&sixty_seven_filter_info);

    // Optional raw async variant retained for direct camera-frame testing.
    obs_register_source(&sixty_seven_raw_filter_info);
    obs_register_source(&sixty_seven_display_info);

    blog(LOG_INFO, "[obs-67-counter] loaded");
    return true;
}

const char *obs_module_description(void)
{
    return "Counts alternating high/low hand switches (67 gesture).";
}
