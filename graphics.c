#include "graphics.h"
#include "font_modern.h"
#include "multiboot.h"
#include "string.h"

u32 framebuffer_addr   = 0;
u32 framebuffer_width  = 0;
u32 framebuffer_height = 0;
u32 framebuffer_bpp    = 0;
u32 framebuffer_pitch  = 0;

static u32 gfx_bb[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];
static int gfx_bb_ready = 0;
static int gfx_deferred = 0;

void init_graphics(multiboot_info_t *m)
{
    multiboot_tag_t *tag;
    for (tag = (multiboot_tag_t*)(m + 1);
         tag->type != 0;
         tag = (multiboot_tag_t*)((u8*)tag + ((tag->size + 7) & ~7)))
    {
        if (tag->type == 8) {
            multiboot_tag_framebuffer_t *fb = (void*)tag;
            framebuffer_addr   = (u32)fb->framebuffer_addr;
            framebuffer_width  = fb->framebuffer_width;
            framebuffer_height = fb->framebuffer_height;
            framebuffer_bpp    = fb->framebuffer_bpp;
            framebuffer_pitch  = fb->framebuffer_pitch;
            if (framebuffer_width > 0 && framebuffer_height > 0 &&
                framebuffer_width <= GFX_MAX_WIDTH &&
                framebuffer_height <= GFX_MAX_HEIGHT) {
                gfx_bb_ready = 1;
            }
            return;
        }
    }
}

void gfx_set_deferred(int deferred)
{
    gfx_deferred = (deferred && gfx_bb_ready) ? 1 : 0;
}

int gfx_is_deferred(void)
{
    return gfx_deferred;
}

u32* gfx_backbuffer(void)
{
    return gfx_bb_ready ? gfx_bb : (u32*)framebuffer_addr;
}

void gfx_present(void)
{
    if (!framebuffer_addr || !gfx_bb_ready) return;
    u32 stride = framebuffer_pitch / 4;
    u32 *front = (u32*)framebuffer_addr;
    if (stride == framebuffer_width) {
        memcpy(front, gfx_bb, framebuffer_width * framebuffer_height * 4);
    } else {
        for (u32 y = 0; y < framebuffer_height; y++) {
            memcpy(front + y * stride, gfx_bb + y * framebuffer_width, framebuffer_width * 4);
        }
    }
}

void gfx_present_rect(int x, int y, int w, int h)
{
    if (!framebuffer_addr || !gfx_bb_ready) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)framebuffer_width)  w = (int)framebuffer_width - x;
    if (y + h > (int)framebuffer_height) h = (int)framebuffer_height - y;
    if (w <= 0 || h <= 0) return;

    u32 stride = framebuffer_pitch / 4;
    u32 *front = (u32*)framebuffer_addr;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        memcpy(front + py * stride + x, gfx_bb + py * framebuffer_width + x, (u32)w * 4);
    }
}

void put_pixel(int x, int y, u32 c)
{
    if (!framebuffer_addr) return;
    if (x < 0 || y < 0 || x >= (int)framebuffer_width || y >= (int)framebuffer_height) return;
    if (gfx_bb_ready) {
        gfx_bb[y * (int)framebuffer_width + x] = c;
    }
    if (!gfx_deferred) {
        ((u32*)framebuffer_addr)[y * (framebuffer_pitch / 4) + x] = c;
    }
}

u32 get_pixel(int x, int y)
{
    if (!framebuffer_addr) return 0;
    if (x < 0 || y < 0 || x >= (int)framebuffer_width || y >= (int)framebuffer_height) return 0;
    if (gfx_bb_ready) {
        return gfx_bb[y * (int)framebuffer_width + x];
    }
    return ((u32*)framebuffer_addr)[y * (framebuffer_pitch / 4) + x];
}

void gfx_blend_pixel(int x, int y, u32 argb)
{
    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) return;
    if (a == 255) {
        put_pixel(x, y, argb & 0x00FFFFFFu);
        return;
    }
    if (!framebuffer_addr) return;
    if (x < 0 || y < 0 || x >= (int)framebuffer_width || y >= (int)framebuffer_height) return;

    u32 dst = get_pixel(x, y);
    u32 inv = 255u - a;
    u32 sr = (argb >> 16) & 0xFFu;
    u32 sg = (argb >> 8)  & 0xFFu;
    u32 sb =  argb        & 0xFFu;
    u32 dr = (dst  >> 16) & 0xFFu;
    u32 dg = (dst  >> 8)  & 0xFFu;
    u32 db =  dst         & 0xFFu;

    u32 rr = (sr * a + dr * inv) >> 8;
    u32 rg = (sg * a + dg * inv) >> 8;
    u32 rb = (sb * a + db * inv) >> 8;
    put_pixel(x, y, (rr << 16) | (rg << 8) | rb);
}

void draw_rect(int x, int y, int w, int h, u32 c)
{
    if (!framebuffer_addr || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > (int)framebuffer_width)  ? (int)framebuffer_width  : (x + w);
    int y1 = (y + h > (int)framebuffer_height) ? (int)framebuffer_height : (y + h);
    if (x0 >= x1 || y0 >= y1) return;

    if (gfx_bb_ready) {
        for (int py = y0; py < y1; py++) {
            u32 *row = &gfx_bb[py * (int)framebuffer_width + x0];
            for (int px = x0; px < x1; px++) {
                *row++ = c;
            }
        }
    }
    if (!gfx_deferred) {
        u32 stride = framebuffer_pitch / 4;
        u32 *front = (u32*)framebuffer_addr;
        for (int py = y0; py < y1; py++) {
            u32 *row = &front[py * stride + x0];
            for (int px = x0; px < x1; px++) {
                *row++ = c;
            }
        }
    }
}

void draw_rect_alpha(int x, int y, int w, int h, u32 argb)
{
    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) return;
    if (a == 255) {
        draw_rect(x, y, w, h, argb & 0x00FFFFFFu);
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > (int)framebuffer_width)  ? (int)framebuffer_width  : (x + w);
    int y1 = (y + h > (int)framebuffer_height) ? (int)framebuffer_height : (y + h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            gfx_blend_pixel(px, py, argb);
        }
    }
}

void draw_rect_outline(int x, int y, int w, int h, int thickness, u32 argb)
{
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    draw_rect_alpha(x, y, w, thickness, argb);
    draw_rect_alpha(x, y + h - thickness, w, thickness, argb);
    draw_rect_alpha(x, y + thickness, thickness, h - 2 * thickness, argb);
    draw_rect_alpha(x + w - thickness, y + thickness, thickness, h - 2 * thickness, argb);
}

static u32 lerp_rgb(u32 c0, u32 c1, int num, int den)
{
    if (den <= 0) return c0;
    int r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r = r0 + ((r1 - r0) * num) / den;
    int g = g0 + ((g1 - g0) * num) / den;
    int b = b0 + ((b1 - b0) * num) / den;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

void draw_gradient_v(int x, int y, int w, int h, u32 top_rgb, u32 bottom_rgb)
{
    if (w <= 0 || h <= 0) return;
    int den = (h > 1) ? (h - 1) : 1;
    for (int row = 0; row < h; row++) {
        u32 c = lerp_rgb(top_rgb, bottom_rgb, row, den);
        draw_rect(x, y + row, w, 1, c);
    }
}

void draw_gradient_h(int x, int y, int w, int h, u32 left_rgb, u32 right_rgb)
{
    if (w <= 0 || h <= 0) return;
    int den = (w > 1) ? (w - 1) : 1;
    for (int col = 0; col < w; col++) {
        u32 c = lerp_rgb(left_rgb, right_rgb, col, den);
        draw_rect(x + col, y, 1, h, c);
    }
}

void draw_rounded_rect(int x, int y, int w, int h, int r, u32 argb)
{
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) {
        /* Treat 0x00RRGGBB as opaque for convenience */
        argb |= 0xFF000000u;
        a = 255;
    }

    if (r == 0) {
        draw_rect_alpha(x, y, w, h, argb);
        return;
    }

    /* Center cross */
    draw_rect_alpha(x + r, y, w - 2 * r, h, argb);
    draw_rect_alpha(x, y + r, r, h - 2 * r, argb);
    draw_rect_alpha(x + w - r, y + r, r, h - 2 * r, argb);

    /* Four rounded corners */
    int r2 = r * r;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            if (dx * dx + dy * dy <= r2) {
                int lx = x + r - 1 - dx;
                int rx = x + w - r + dx;
                int ty = y + r - 1 - dy;
                int by = y + h - r + dy;
                if (a == 255) {
                    u32 rgb = argb & 0x00FFFFFFu;
                    put_pixel(lx, ty, rgb);
                    put_pixel(rx, ty, rgb);
                    put_pixel(lx, by, rgb);
                    put_pixel(rx, by, rgb);
                } else {
                    gfx_blend_pixel(lx, ty, argb);
                    gfx_blend_pixel(rx, ty, argb);
                    gfx_blend_pixel(lx, by, argb);
                    gfx_blend_pixel(rx, by, argb);
                }
            }
        }
    }
}

void draw_rounded_outline(int x, int y, int w, int h, int r, int thickness, u32 argb)
{
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) argb |= 0xFF000000u;

    draw_rect_alpha(x + r, y, w - 2 * r, thickness, argb);
    draw_rect_alpha(x + r, y + h - thickness, w - 2 * r, thickness, argb);
    draw_rect_alpha(x, y + r, thickness, h - 2 * r, argb);
    draw_rect_alpha(x + w - thickness, y + r, thickness, h - 2 * r, argb);

    int r_outer2 = r * r;
    int r_in = (r > thickness) ? (r - thickness) : 0;
    int r_inner2 = r_in * r_in;
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r_outer2 && d2 >= r_inner2) {
                int lx = x + r - 1 - dx;
                int rx = x + w - r + dx;
                int ty = y + r - 1 - dy;
                int by = y + h - r + dy;
                gfx_blend_pixel(lx, ty, argb);
                gfx_blend_pixel(rx, ty, argb);
                gfx_blend_pixel(lx, by, argb);
                gfx_blend_pixel(rx, by, argb);
            }
        }
    }
}

void draw_shadow(int x, int y, int w, int h, int r, int spread, u8 max_alpha)
{
    if (spread <= 0 || max_alpha == 0) return;
    for (int s = spread; s >= 1; s--) {
        u8 alpha = (u8)((max_alpha * (spread - s + 1)) / (spread * 2 + 1));
        draw_rounded_outline(x - s, y - s + 3, w + 2 * s, h + 2 * s, r + s, 2, gfx_rgba(4, 8, 18, alpha));
    }
}

void draw_circle_filled(int cx, int cy, int r, u32 argb)
{
    if (r <= 0) return;
    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) argb |= 0xFF000000u;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r2) {
                gfx_blend_pixel(cx + dx, cy + dy, argb);
            }
        }
    }
}

void draw_circle_outline(int cx, int cy, int r, u32 argb)
{
    if (r <= 0) return;
    u32 a = (argb >> 24) & 0xFFu;
    if (a == 0) argb |= 0xFF000000u;
    int x = r, y = 0;
    int err = 1 - x;
    while (x >= y) {
        gfx_blend_pixel(cx + x, cy + y, argb);
        gfx_blend_pixel(cx + y, cy + x, argb);
        gfx_blend_pixel(cx - y, cy + x, argb);
        gfx_blend_pixel(cx - x, cy + y, argb);
        gfx_blend_pixel(cx - x, cy - y, argb);
        gfx_blend_pixel(cx - y, cy - x, argb);
        gfx_blend_pixel(cx + y, cy - x, argb);
        gfx_blend_pixel(cx + x, cy - y, argb);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, u32 color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_char(int x, int y, char c, u32 color)
{
    u8 uc = (u8)c;
    if (uc >= 32 && uc <= 127) {
        u32 rgb = color & 0x00FFFFFFu;
        const u8 (*glyph)[FONT_UI_W] = font_ui_8x16[uc - 32];
        for (int row = 0; row < FONT_UI_H; row++) {
            for (int col = 0; col < FONT_UI_W; col++) {
                u8 a = glyph[row][col];
                if (a > 0) {
                    gfx_blend_pixel(x + col, y - 3 + row, ((u32)a << 24) | rgb);
                }
            }
        }
        return;
    }

    extern const u8 font[256][8];
    const u8 *glyph = font[uc];
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if ((glyph[i] >> (7 - j)) & 1) {
                put_pixel(x + j, y + i, color);
            }
        }
    }
}

void draw_string_hd(int x, int y, const char *s, u32 color)
{
    u32 rgb = color & 0x00FFFFFFu;
    int cx = x;
    for (int i = 0; s[i]; i++) {
        u8 uc = (u8)s[i];
        if (uc < 32 || uc > 127) uc = '?';
        int idx = uc - 32;
        const u8 (*glyph)[FONT_HD_W] = font_hd_glyphs[idx];
        for (int row = 0; row < FONT_HD_H; row++) {
            for (int col = 0; col < FONT_HD_W; col++) {
                u8 a = glyph[row][col];
                if (a > 0) {
                    gfx_blend_pixel(cx + col, y + row, ((u32)a << 24) | rgb);
                }
            }
        }
        cx += (int)font_hd_adv[idx];
    }
}

int string_px_hd(const char *s)
{
    int w = 0;
    for (int i = 0; s[i]; i++) {
        u8 uc = (u8)s[i];
        if (uc < 32 || uc > 127) uc = '?';
        w += (int)font_hd_adv[uc - 32];
    }
    return w;
}

void draw_string(int x, int y, const char *s, u32 color)
{
    for (int i = 0; s[i]; i++) {
        draw_char(x + i * 8, y, s[i], color);
    }
}

void draw_string_shadow(int x, int y, const char *s, u32 color, u32 shadow_color)
{
    draw_string(x + 1, y + 1, s, shadow_color);
    draw_string(x, y, s, color);
}

void draw_char_scaled_gfx(int x, int y, char c, int scale, u32 color)
{
    extern const u8 font[256][8];
    const u8 *glyph = font[(u8)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                draw_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void draw_string_scaled_gfx(int x, int y, const char *s, int scale, u32 color)
{
    for (int i = 0; s[i]; i++) {
        draw_char_scaled_gfx(x + i * 8 * scale, y, s[i], scale, color);
    }
}

void draw_button(int x, int y, int width, int height, u32 color, const char *text)
{
    draw_rounded_rect(x, y, width, height, 8, color | 0xFF000000u);
    draw_rounded_outline(x, y, width, height, 8, 1, gfx_rgba(255, 255, 255, 45));
    int len = (int)strlen(text);
    int tx = x + (width - len * 8) / 2;
    int ty = y + (height - 8) / 2;
    draw_string(tx, ty, text, 0xFFFFFF);
}

void draw_window(int x, int y, int width, int height, u32 color, const char *title)
{
    draw_shadow(x, y, width, height, 12, 8, 90);
    draw_rounded_rect(x, y, width, height, 12, 0xFF1E293B);
    draw_rect(x + 2, y + 2, width - 4, height - 4, 0xF8FAFC);
    draw_gradient_v(x + 2, y + 2, width - 4, 30, color & 0x00FFFFFFu, 0x0F172A);
    draw_string(x + 14, y + 13, title, 0xFFFFFF);
}

void xor_pixel(int x, int y, u32 color)
{
    if (!framebuffer_addr) return;
    if (x < 0 || y < 0 || x >= (int)framebuffer_width || y >= (int)framebuffer_height) return;
    u32 *fb = (u32*)framebuffer_addr;
    fb[y * (framebuffer_pitch / 4) + x] ^= color;
}

void xor_cursor(int x, int y)
{
    for (int dy = 0; dy < 6; dy++) {
        for (int dx = 0; dx < 6; dx++) {
            xor_pixel(x + dx, y + dy, 0x00FFFFFF);
        }
    }
}
