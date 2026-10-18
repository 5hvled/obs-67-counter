#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-67-counter", "en-US")

extern obs_source_info sixty_seven_filter_info;

bool obs_module_load(void)
{
    obs_register_source(&sixty_seven_filter_info);
    blog(LOG_INFO, "[obs-67-counter] loaded");
    return true;
}

const char *obs_module_description(void)
{
    return "Counts alternating high/low hand switches (67 gesture).";
}
