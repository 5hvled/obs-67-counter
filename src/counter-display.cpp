#include "counter-display.hpp"
#include "counter-state.hpp"
#include <graphics/graphics.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct CounterDisplay {
    obs_source_t *source = nullptr;
    gs_texture_t *texture = nullptr;
    uint64_t last_count = UINT64_MAX;
    bool dirty = true;
    bool show_label = true;
    uint32_t scale = 12;
    uint32_t width = 1;
    uint32_t height = 1;
};

static const std::array<uint8_t, 7> DIGITS[10] = {{
    {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
    {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
    {{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
    {{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
    {{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
    {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
    {{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
}};

static bool glyph_pixel(char ch, int x, int y)
{
    if (ch >= '0' && ch <= '9')
        return (DIGITS[ch - '0'][y] & (1u << (4 - x))) != 0;

    if (ch == ':')
        return x == 2 && (y == 2 || y == 4);

    return false;
}

static std::string display_text(const CounterDisplay *d)
{
    char buf[64];
    if (d->show_label)
        snprintf(buf, sizeof(buf), "67:%llu", (unsigned long long)counter_get());
    else
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)counter_get());
    return buf;
}

static void rebuild(CounterDisplay *d)
{
    const std::string text = display_text(d);
    const uint32_t cells_w = std::max<uint32_t>(1, (uint32_t)text.size() * 6 - 1);
    const uint32_t cells_h = 7;
    const uint32_t w = cells_w * d->scale;
    const uint32_t h = cells_h * d->scale;

    std::vector<uint8_t> pixels((size_t)w * h * 4, 0);

    for (size_t ci = 0; ci < text.size(); ++ci) {
        const char ch = text[ci];
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (!glyph_pixel(ch, gx, gy))
                    continue;

                const uint32_t x0 = ((uint32_t)ci * 6 + (uint32_t)gx) * d->scale;
                const uint32_t y0 = (uint32_t)gy * d->scale;

                for (uint32_t py = 0; py < d->scale; ++py) {
                    for (uint32_t px = 0; px < d->scale; ++px) {
                        const size_t off = ((size_t)(y0 + py) * w + (x0 + px)) * 4;
                        pixels[off + 0] = 255;
                        pixels[off + 1] = 255;
                        pixels[off + 2] = 255;
                        pixels[off + 3] = 255;
                    }
                }
            }
        }
    }

    if (!d->texture || d->width != w || d->height != h) {
        if (d->texture)
            gs_texture_destroy(d->texture);
        const uint8_t *initial = pixels.data();
        d->texture = gs_texture_create(w, h, GS_RGBA, 1, &initial, GS_DYNAMIC);
    } else {
        gs_texture_set_image(d->texture, pixels.data(), w * 4, false);
    }

    d->width = w;
    d->height = h;
    d->last_count = counter_get();
    d->dirty = false;
}

static const char *get_name(void *)
{
    return "67 Counter Display";
}

static void defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, "show_label", true);
    obs_data_set_default_int(settings, "scale", 12);
}

static void update(void *data, obs_data_t *settings)
{
    auto *d = static_cast<CounterDisplay *>(data);
    d->show_label = obs_data_get_bool(settings, "show_label");
    d->scale = (uint32_t)std::clamp<int64_t>(obs_data_get_int(settings, "scale"), 4, 32);
    d->dirty = true;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
    auto *d = new CounterDisplay;
    d->source = source;
    update(d, settings);
    return d;
}

static void destroy(void *data)
{
    auto *d = static_cast<CounterDisplay *>(data);
    if (!d)
        return;

    if (d->texture) {
        obs_enter_graphics();
        gs_texture_destroy(d->texture);
        obs_leave_graphics();
    }
    delete d;
}

static bool plus_button(obs_properties_t *, obs_property_t *, void *)
{
    counter_add(1);
    return true;
}

static bool minus_button(obs_properties_t *, obs_property_t *, void *)
{
    counter_add(-1);
    return true;
}

static bool reset_button(obs_properties_t *, obs_property_t *, void *)
{
    counter_set(0);
    return true;
}

static obs_properties_t *properties(void *)
{
    obs_properties_t *p = obs_properties_create();

    char status[96];
    snprintf(status, sizeof(status), "Current count: %llu",
             (unsigned long long)counter_get());
    obs_properties_add_text(p, "count_status", status, OBS_TEXT_INFO);

    obs_properties_add_button(p, "plus", "+1", plus_button);
    obs_properties_add_button(p, "minus", "-1", minus_button);
    obs_properties_add_button(p, "reset", "Reset counter", reset_button);
    obs_properties_add_bool(p, "show_label", "Show 67: label");
    obs_properties_add_int_slider(p, "scale", "Display size", 4, 32, 1);
    return p;
}

static uint32_t get_width(void *data)
{
    auto *d = static_cast<CounterDisplay *>(data);
    const std::string text = display_text(d);
    return std::max<uint32_t>(1, ((uint32_t)text.size() * 6 - 1) * d->scale);
}

static uint32_t get_height(void *data)
{
    auto *d = static_cast<CounterDisplay *>(data);
    return 7 * d->scale;
}

static void render(void *data, gs_effect_t *effect)
{
    auto *d = static_cast<CounterDisplay *>(data);
    const uint64_t count = counter_get();

    if (d->dirty || d->last_count != count)
        rebuild(d);

    if (!d->texture)
        return;

    const bool prev_srgb = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(true);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture_srgb(image, d->texture);
    gs_draw_sprite(d->texture, 0, d->width, d->height);

    gs_blend_state_pop();
    gs_enable_framebuffer_srgb(prev_srgb);
}

static obs_source_info make_info()
{
    obs_source_info info = {};
    info.id = "sixty_seven_counter_display";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
    info.get_name = get_name;
    info.create = create;
    info.destroy = destroy;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.update = update;
    info.get_width = get_width;
    info.get_height = get_height;
    info.video_render = render;
    return info;
}

} // namespace

obs_source_info sixty_seven_display_info = make_info();
