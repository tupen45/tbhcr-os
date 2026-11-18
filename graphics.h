#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "common.h"
#include "multiboot.h"

extern u32 framebuffer_addr;
extern u32 framebuffer_width;
extern u32 framebuffer_height;
extern u32 framebuffer_bpp;
extern u32 framebuffer_pitch;

/* Maximum double-buffered resolution supported in static BSS (1600x1200x4 = ~7.3 MiB) */
#define GFX_MAX_WIDTH  1600
#define GFX_MAX_HEIGHT 1200

void init_graphics(multiboot_info_t* mbd);

/* Double-buffering control:
 * deferred = 0 (default): writes update backbuffer + front framebuffer immediately (legacy-safe).
 * deferred = 1: writes go only to backbuffer until gfx_present() / gfx_present_rect() is called.
 */
void gfx_set_deferred(int deferred);
int  gfx_is_deferred(void);
void gfx_present(void);
void gfx_present_rect(int x, int y, int w, int h);
u32* gfx_backbuffer(void);

/* Color helpers (0xAARRGGBB) */
static inline u32 gfx_rgb(u8 r, u8 g, u8 b) {
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline u32 gfx_rgba(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

/* Core pixel operations */
void put_pixel(int x, int y, u32 color);
u32  get_pixel(int x, int y);
void gfx_blend_pixel(int x, int y, u32 argb);

/* Primitives */
void draw_rect(int x, int y, int width, int height, u32 color);
void draw_rect_alpha(int x, int y, int width, int height, u32 argb);
void draw_rect_outline(int x, int y, int width, int height, int thickness, u32 argb);
void draw_gradient_v(int x, int y, int width, int height, u32 top_rgb, u32 bottom_rgb);
void draw_gradient_h(int x, int y, int width, int height, u32 left_rgb, u32 right_rgb);
void draw_rounded_rect(int x, int y, int width, int height, int radius, u32 argb);
void draw_rounded_outline(int x, int y, int width, int height, int radius, int thickness, u32 argb);
void draw_shadow(int x, int y, int width, int height, int radius, int spread, u8 max_alpha);
void draw_circle_filled(int cx, int cy, int r, u32 argb);
void draw_circle_outline(int cx, int cy, int r, u32 argb);
void draw_line(int x0, int y0, int x1, int y1, u32 color);

/* Text rendering */
void draw_char(int x, int y, char c, u32 color);
void draw_string(int x, int y, const char *s, u32 color);
void draw_string_shadow(int x, int y, const char *s, u32 color, u32 shadow_color);
void draw_char_scaled_gfx(int x, int y, char c, int scale, u32 color);
void draw_string_scaled_gfx(int x, int y, const char *s, int scale, u32 color);
void draw_string_hd(int x, int y, const char *s, u32 color);
int  string_px_hd(const char *s);

/* High-level UI widgets */
void draw_button(int x, int y, int width, int height, u32 color, const char *text);
void draw_window(int x, int y, int width, int height, u32 color, const char *title);

/* Legacy XOR cursor helpers */
void xor_pixel(int x, int y, u32 color);
void xor_cursor(int x, int y);

#endif
