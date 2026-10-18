#include "hand-detector.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace {
struct Blob {
    int n = 0;
    double sx = 0.0;
    double sy = 0.0;
};

static bool skin_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const int mx = std::max({(int)r, (int)g, (int)b});
    const int mn = std::min({(int)r, (int)g, (int)b});
    return r > 70 && g > 35 && b > 20 && (mx - mn) > 12 &&
           r > g && r > b && std::abs((int)r - (int)g) > 8;
}

static bool skin_yuv(uint8_t y, uint8_t u, uint8_t v)
{
    return y > 40 && u >= 70 && u <= 140 && v >= 125 && v <= 190;
}

static bool is_skin_at(const obs_source_frame *f, uint32_t x, uint32_t y)
{
    if (!f || x >= f->width || y >= f->height)
        return false;

    switch (f->format) {
    case VIDEO_FORMAT_BGRA:
    case VIDEO_FORMAT_BGRX: {
        const uint8_t *p = f->data[0] + (size_t)y * f->linesize[0] + (size_t)x * 4;
        return skin_rgb(p[2], p[1], p[0]);
    }
    case VIDEO_FORMAT_RGBA: {
        const uint8_t *p = f->data[0] + (size_t)y * f->linesize[0] + (size_t)x * 4;
        return skin_rgb(p[0], p[1], p[2]);
    }
    case VIDEO_FORMAT_BGR3: {
        const uint8_t *p = f->data[0] + (size_t)y * f->linesize[0] + (size_t)x * 3;
        return skin_rgb(p[2], p[1], p[0]);
    }
    case VIDEO_FORMAT_NV12: {
        const uint8_t yy = f->data[0][(size_t)y * f->linesize[0] + x];
        const uint32_t cx = x & ~1u;
        const uint8_t *uv = f->data[1] + (size_t)(y / 2) * f->linesize[1] + cx;
        return skin_yuv(yy, uv[0], uv[1]);
    }
    case VIDEO_FORMAT_I420: {
        const uint8_t yy = f->data[0][(size_t)y * f->linesize[0] + x];
        const uint8_t uu = f->data[1][(size_t)(y / 2) * f->linesize[1] + x / 2];
        const uint8_t vv = f->data[2][(size_t)(y / 2) * f->linesize[2] + x / 2];
        return skin_yuv(yy, uu, vv);
    }
    case VIDEO_FORMAT_I422: {
        const uint8_t yy = f->data[0][(size_t)y * f->linesize[0] + x];
        const uint8_t uu = f->data[1][(size_t)y * f->linesize[1] + x / 2];
        const uint8_t vv = f->data[2][(size_t)y * f->linesize[2] + x / 2];
        return skin_yuv(yy, uu, vv);
    }
    case VIDEO_FORMAT_I444: {
        const uint8_t yy = f->data[0][(size_t)y * f->linesize[0] + x];
        const uint8_t uu = f->data[1][(size_t)y * f->linesize[1] + x];
        const uint8_t vv = f->data[2][(size_t)y * f->linesize[2] + x];
        return skin_yuv(yy, uu, vv);
    }
    case VIDEO_FORMAT_YUY2:
    case VIDEO_FORMAT_YVYU:
    case VIDEO_FORMAT_UYVY: {
        const uint8_t *p = f->data[0] + (size_t)y * f->linesize[0] + (size_t)(x / 2) * 4;
        uint8_t yy, uu, vv;
        if (f->format == VIDEO_FORMAT_UYVY) {
            uu = p[0];
            yy = p[(x & 1u) ? 3 : 1];
            vv = p[2];
        } else if (f->format == VIDEO_FORMAT_YVYU) {
            yy = p[(x & 1u) ? 2 : 0];
            vv = p[1];
            uu = p[3];
        } else {
            yy = p[(x & 1u) ? 2 : 0];
            uu = p[1];
            vv = p[3];
        }
        return skin_yuv(yy, uu, vv);
    }
    default:
        return false;
    }
}
}

bool detect_hand_pair_frame(const obs_source_frame *frame, float min_frac,
                            HandPoint &a, HandPoint &b)
{
    a = {};
    b = {};
    if (!frame || !frame->data[0] || frame->width < 32 || frame->height < 32)
        return false;

    const uint32_t w = frame->width;
    const uint32_t h = frame->height;
    const int step = 4;
    const int gw = (int)((w + step - 1) / step);
    const int gh = (int)((h + step - 1) / step);

    std::vector<uint8_t> mask((size_t)gw * gh);
    std::vector<uint8_t> seen(mask.size());

    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            const uint32_t px = std::min<uint32_t>((uint32_t)gx * step, w - 1);
            const uint32_t py = std::min<uint32_t>((uint32_t)gy * step, h - 1);
            mask[(size_t)gy * gw + gx] = is_skin_at(frame, px, py) ? 1 : 0;
        }
    }

    std::vector<Blob> blobs;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            const size_t id = (size_t)gy * gw + gx;
            if (!mask[id] || seen[id])
                continue;

            Blob z;
            std::queue<std::pair<int, int>> q;
            q.push({gx, gy});
            seen[id] = 1;

            while (!q.empty()) {
                const auto cur = q.front();
                q.pop();
                const int cx = cur.first;
                const int cy = cur.second;
                ++z.n;
                z.sx += cx;
                z.sy += cy;

                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + dx[k];
                    const int ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh)
                        continue;
                    const size_t ni = (size_t)ny * gw + nx;
                    if (mask[ni] && !seen[ni]) {
                        seen[ni] = 1;
                        q.push({nx, ny});
                    }
                }
            }

            const float frac = (float)z.n / (float)(gw * gh);
            if (frac >= min_frac && frac < 0.20f)
                blobs.push_back(z);
        }
    }

    if (blobs.size() < 2)
        return false;

    auto to_point = [&](const Blob &z) {
        HandPoint p;
        p.x = (float)(z.sx / z.n) / (float)gw;
        p.y = (float)(z.sy / z.n) / (float)gh;
        p.area = (float)z.n / (float)(gw * gh);
        p.valid = true;
        return p;
    };

    float best_score = -1.0f;
    HandPoint best_a, best_b;
    for (size_t i = 0; i < blobs.size(); ++i) {
        const HandPoint p = to_point(blobs[i]);
        for (size_t j = i + 1; j < blobs.size(); ++j) {
            const HandPoint q = to_point(blobs[j]);
            const float sep = std::fabs(p.x - q.x);
            if (sep < 0.12f)
                continue;
            const float score = p.area + q.area + sep * 0.015f;
            if (score > best_score) {
                best_score = score;
                best_a = p;
                best_b = q;
            }
        }
    }

    if (best_score < 0.0f)
        return false;

    a = best_a;
    b = best_b;
    if (a.x > b.x)
        std::swap(a, b);
    return true;
}

bool detect_hand_pair_bgra(const uint8_t *data, uint32_t width, uint32_t height,
                           uint32_t stride, float min_blob_fraction,
                           HandPoint &a, HandPoint &b)
{
    obs_source_frame frame = {};
    frame.data[0] = const_cast<uint8_t *>(data);
    frame.linesize[0] = stride;
    frame.width = width;
    frame.height = height;
    frame.format = VIDEO_FORMAT_BGRA;
    return detect_hand_pair_frame(&frame, min_blob_fraction, a, b);
}
