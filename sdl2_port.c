#include "sdl2_port.h"
#include "graphics.h"
#include "io.h"
#include "kmalloc.h"
#include "string.h"

#define SDL_MAX_WINDOWS   4
#define SDL_MAX_RENDERERS 4
#define SDL_EVENT_Q_SIZE  32

static SDL_Window   sdl_windows[SDL_MAX_WINDOWS];
static SDL_Renderer sdl_renderers[SDL_MAX_RENDERERS];
static int          sdl_initialized = 0;
static const char  *sdl_last_error = "";

/* Mouse & keyboard state for SDL_PollEvent */
static int sdl_mouse_x = 0;
static int sdl_mouse_y = 0;
static u8  sdl_mouse_buttons = 0;
static int sdl_kbd_shifted = 0;
static u8  sdl_mouse_pkt[3];
static int sdl_mouse_pkt_idx = 0;

/* Event queue */
static SDL_Event sdl_eq[SDL_EVENT_Q_SIZE];
static int sdl_eq_head = 0;
static int sdl_eq_tail = 0;

static u32 sdl_tick_counter = 0;

static void eq_push(const SDL_Event *ev)
{
    int next = (sdl_eq_tail + 1) % SDL_EVENT_Q_SIZE;
    if (next == sdl_eq_head) return; /* full */
    sdl_eq[sdl_eq_tail] = *ev;
    sdl_eq_tail = next;
}

static int eq_pop(SDL_Event *ev)
{
    if (sdl_eq_head == sdl_eq_tail) return 0;
    *ev = sdl_eq[sdl_eq_head];
    sdl_eq_head = (sdl_eq_head + 1) % SDL_EVENT_Q_SIZE;
    return 1;
}

static char scancode_to_ascii(u8 sc, int shifted)
{
    static const char map_lo[128] = {
        0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
        'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
    };
    static const char map_hi[128] = {
        0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
        'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
        'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
    };
    if (sc >= 128) return 0;
    return shifted ? map_hi[sc] : map_lo[sc];
}

int SDL_Init(u32 flags)
{
    (void)flags;
    for (int i = 0; i < SDL_MAX_WINDOWS; i++)   sdl_windows[i].active = 0;
    for (int i = 0; i < SDL_MAX_RENDERERS; i++) sdl_renderers[i].window = 0;
    sdl_eq_head = sdl_eq_tail = 0;
    sdl_mouse_pkt_idx = 0;
    if (sdl_mouse_x == 0 && sdl_mouse_y == 0 && framebuffer_width > 0) {
        sdl_mouse_x = (int)framebuffer_width / 2;
        sdl_mouse_y = (int)framebuffer_height / 2;
    }
    sdl_initialized = 1;
    return 0;
}

void SDL_Quit(void)
{
    for (int i = 0; i < SDL_MAX_WINDOWS; i++)   sdl_windows[i].active = 0;
    for (int i = 0; i < SDL_MAX_RENDERERS; i++) sdl_renderers[i].window = 0;
    sdl_initialized = 0;
    gfx_set_deferred(0);
}

const char* SDL_GetError(void)
{
    return sdl_last_error;
}

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, u32 flags)
{
    if (!sdl_initialized) SDL_Init(SDL_INIT_VIDEO);
    for (int i = 0; i < SDL_MAX_WINDOWS; i++) {
        if (!sdl_windows[i].active) {
            SDL_Window *win = &sdl_windows[i];
            win->active = 1;
            win->flags = flags;
            win->w = (w > 0) ? w : (int)framebuffer_width;
            win->h = (h > 0) ? h : (int)framebuffer_height;
            win->x = (x == SDL_WINDOWPOS_CENTERED) ? ((int)framebuffer_width - win->w) / 2 : x;
            win->y = (y == SDL_WINDOWPOS_CENTERED) ? ((int)framebuffer_height - win->h) / 2 : y;
            if (title) {
                strncpy(win->title, title, sizeof(win->title) - 1);
                win->title[sizeof(win->title) - 1] = '\0';
            } else {
                win->title[0] = '\0';
            }
            return win;
        }
    }
    sdl_last_error = "No free SDL_Window slots";
    return 0;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (window) window->active = 0;
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    if (!window) return;
    if (w) *w = window->w;
    if (h) *h = window->h;
}

SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, u32 flags)
{
    (void)index;
    (void)flags;
    if (!window) {
        sdl_last_error = "Invalid window";
        return 0;
    }
    for (int i = 0; i < SDL_MAX_RENDERERS; i++) {
        if (!sdl_renderers[i].window) {
            SDL_Renderer *ren = &sdl_renderers[i];
            ren->window = window;
            ren->r = 0;
            ren->g = 0;
            ren->b = 0;
            ren->a = 255;
            ren->blend_mode = SDL_BLENDMODE_NONE;
            ren->has_clip = 0;
            gfx_set_deferred(1);
            return ren;
        }
    }
    sdl_last_error = "No free SDL_Renderer slots";
    return 0;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer) renderer->window = 0;
    gfx_set_deferred(0);
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, u8 r, u8 g, u8 b, u8 a)
{
    if (!renderer) return -1;
    renderer->r = r;
    renderer->g = g;
    renderer->b = b;
    renderer->a = a;
    return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    if (!renderer) return -1;
    renderer->blend_mode = blendMode;
    return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer) return -1;
    if (!rect) {
        renderer->has_clip = 0;
    } else {
        renderer->clip = *rect;
        renderer->has_clip = 1;
    }
    return 0;
}

static u32 ren_color(const SDL_Renderer *ren)
{
    u8 a = (ren->blend_mode == SDL_BLENDMODE_BLEND) ? ren->a : 255;
    return gfx_rgba(ren->r, ren->g, ren->b, a);
}

static void ren_offset(const SDL_Renderer *ren, int *ox, int *oy)
{
    if (ren && ren->window) {
        *ox = ren->window->x;
        *oy = ren->window->y;
    } else {
        *ox = 0;
        *oy = 0;
    }
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->window) return -1;
    u32 col = gfx_rgb(renderer->r, renderer->g, renderer->b);
    draw_rect(renderer->window->x, renderer->window->y,
              renderer->window->w, renderer->window->h, col);
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    sdl_tick_counter += 16;
    if (renderer && renderer->window) {
        gfx_present_rect(renderer->window->x, renderer->window->y,
                         renderer->window->w, renderer->window->h);
    } else {
        gfx_present();
    }
}

int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y)
{
    if (!renderer) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    if (renderer->blend_mode == SDL_BLENDMODE_BLEND && renderer->a < 255) {
        gfx_blend_pixel(ox + x, oy + y, ren_color(renderer));
    } else {
        put_pixel(ox + x, oy + y, gfx_rgb(renderer->r, renderer->g, renderer->b));
    }
    return 0;
}

int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2)
{
    if (!renderer) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    if (renderer->blend_mode == SDL_BLENDMODE_BLEND && renderer->a < 255) {
        int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
        int sx = (x1 < x2) ? 1 : -1;
        int dy = (y2 > y1) ? (y1 - y2) : (y2 - y1);
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx + dy;
        u32 col = ren_color(renderer);
        while (1) {
            gfx_blend_pixel(ox + x1, oy + y1, col);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    } else {
        draw_line(ox + x1, oy + y1, ox + x2, oy + y2,
                  gfx_rgb(renderer->r, renderer->g, renderer->b));
    }
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer || !rect) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_rect_outline(ox + rect->x, oy + rect->y, rect->w, rect->h, 1, ren_color(renderer));
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    SDL_Rect r;
    if (rect) {
        r = *rect;
    } else if (renderer->window) {
        r.x = 0; r.y = 0; r.w = renderer->window->w; r.h = renderer->window->h;
    } else {
        return -1;
    }
    if (renderer->blend_mode == SDL_BLENDMODE_BLEND && renderer->a < 255) {
        draw_rect_alpha(ox + r.x, oy + r.y, r.w, r.h, ren_color(renderer));
    } else {
        draw_rect(ox + r.x, oy + r.y, r.w, r.h, gfx_rgb(renderer->r, renderer->g, renderer->b));
    }
    return 0;
}

int SDL_RenderFillRects(SDL_Renderer *renderer, const SDL_Rect *rects, int count)
{
    if (!renderer || !rects) return -1;
    for (int i = 0; i < count; i++) {
        SDL_RenderFillRect(renderer, &rects[i]);
    }
    return 0;
}

int SDL_RenderFillRoundedRect(SDL_Renderer *renderer, const SDL_Rect *rect, int radius)
{
    if (!renderer || !rect) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_rounded_rect(ox + rect->x, oy + rect->y, rect->w, rect->h, radius, ren_color(renderer));
    return 0;
}

int SDL_RenderDrawRoundedRect(SDL_Renderer *renderer, const SDL_Rect *rect, int radius, int thickness)
{
    if (!renderer || !rect) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_rounded_outline(ox + rect->x, oy + rect->y, rect->w, rect->h, radius, thickness, ren_color(renderer));
    return 0;
}

int SDL_RenderFillGradientV(SDL_Renderer *renderer, const SDL_Rect *rect, SDL_Color top, SDL_Color bottom)
{
    if (!renderer || !rect) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_gradient_v(ox + rect->x, oy + rect->y, rect->w, rect->h,
                    gfx_rgb(top.r, top.g, top.b),
                    gfx_rgb(bottom.r, bottom.g, bottom.b));
    return 0;
}

int SDL_RenderFillCircle(SDL_Renderer *renderer, int cx, int cy, int radius)
{
    if (!renderer) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_circle_filled(ox + cx, oy + cy, radius, ren_color(renderer));
    return 0;
}

int SDL_RenderDrawCircle(SDL_Renderer *renderer, int cx, int cy, int radius)
{
    if (!renderer) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_circle_outline(ox + cx, oy + cy, radius, ren_color(renderer));
    return 0;
}

int SDL_RenderDrawShadow(SDL_Renderer *renderer, const SDL_Rect *rect, int radius, int spread, u8 max_alpha)
{
    if (!renderer || !rect) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_shadow(ox + rect->x, oy + rect->y, rect->w, rect->h, radius, spread, max_alpha);
    return 0;
}

int SDL_RenderDrawText(SDL_Renderer *renderer, int x, int y, const char *text, SDL_Color color)
{
    if (!renderer || !text) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_string(ox + x, oy + y, text, gfx_rgb(color.r, color.g, color.b));
    return 0;
}

int SDL_RenderDrawTextScaled(SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color)
{
    if (!renderer || !text) return -1;
    int ox, oy;
    ren_offset(renderer, &ox, &oy);
    draw_string_scaled_gfx(ox + x, oy + y, text, scale, gfx_rgb(color.r, color.g, color.b));
    return 0;
}

SDL_Surface* SDL_CreateRGBSurface(u32 flags, int width, int height, int depth,
                                  u32 Rmask, u32 Gmask, u32 Bmask, u32 Amask)
{
    (void)flags; (void)depth; (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    if (width <= 0 || height <= 0) return 0;
    SDL_Surface *surf = (SDL_Surface*)kmalloc(sizeof(SDL_Surface));
    if (!surf) return 0;
    u32 *pix = (u32*)kmalloc((size_t)(width * height) * sizeof(u32));
    if (!pix) {
        kfree(surf);
        return 0;
    }
    surf->w = width;
    surf->h = height;
    surf->pitch = width * 4;
    surf->pixels = pix;
    surf->owned = 1;
    memset(pix, 0, (u32)(width * height * 4));
    return surf;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface->owned && surface->pixels) kfree(surface->pixels);
    kfree(surface);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, u32 color)
{
    if (!dst || !dst->pixels) return -1;
    int x0 = rect ? rect->x : 0;
    int y0 = rect ? rect->y : 0;
    int w  = rect ? rect->w : dst->w;
    int h  = rect ? rect->h : dst->h;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > dst->w) w = dst->w - x0;
    if (y0 + h > dst->h) h = dst->h - y0;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            dst->pixels[y * dst->w + x] = color;
        }
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Renderer *dst_renderer, const SDL_Rect *dstrect)
{
    if (!src || !src->pixels || !dst_renderer) return -1;
    int sx = srcrect ? srcrect->x : 0;
    int sy = srcrect ? srcrect->y : 0;
    int sw = srcrect ? srcrect->w : src->w;
    int sh = srcrect ? srcrect->h : src->h;
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;
    int ox, oy;
    ren_offset(dst_renderer, &ox, &oy);

    for (int row = 0; row < sh; row++) {
        if (sy + row < 0 || sy + row >= src->h) continue;
        for (int col = 0; col < sw; col++) {
            if (sx + col < 0 || sx + col >= src->w) continue;
            u32 px = src->pixels[(sy + row) * src->w + (sx + col)];
            gfx_blend_pixel(ox + dx + col, oy + dy + row, px);
        }
    }
    return 0;
}

static void poll_ps2_hardware(void)
{
    for (int guard = 0; guard < 16; guard++) {
        u8 status = inb(0x64);
        if (!(status & 1)) break;

        u8 data = inb(0x60);
        if (status & 0x20) {
            /* Mouse byte */
            if (sdl_mouse_pkt_idx == 0 && !(data & 0x08)) {
                continue; /* sync */
            }
            sdl_mouse_pkt[sdl_mouse_pkt_idx++] = data;
            if (sdl_mouse_pkt_idx == 3) {
                sdl_mouse_pkt_idx = 0;
                u8 b0 = sdl_mouse_pkt[0];
                int dx = (int)(s8)sdl_mouse_pkt[1];
                int dy = -(int)(s8)sdl_mouse_pkt[2];
                u8 new_btns = b0 & 0x07;

                int nx = sdl_mouse_x + dx;
                int ny = sdl_mouse_y + dy;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (framebuffer_width > 0 && nx >= (int)framebuffer_width)   nx = (int)framebuffer_width - 1;
                if (framebuffer_height > 0 && ny >= (int)framebuffer_height) ny = (int)framebuffer_height - 1;

                if (nx != sdl_mouse_x || ny != sdl_mouse_y) {
                    SDL_Event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = SDL_MOUSEMOTION;
                    ev.motion.x = nx;
                    ev.motion.y = ny;
                    ev.motion.xrel = nx - sdl_mouse_x;
                    ev.motion.yrel = ny - sdl_mouse_y;
                    ev.motion.state = new_btns;
                    sdl_mouse_x = nx;
                    sdl_mouse_y = ny;
                    eq_push(&ev);
                }

                if ((new_btns & 1) != (sdl_mouse_buttons & 1)) {
                    SDL_Event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = (new_btns & 1) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    ev.button.button = SDL_BUTTON_LEFT;
                    ev.button.state = (new_btns & 1) ? 1 : 0;
                    ev.button.x = sdl_mouse_x;
                    ev.button.y = sdl_mouse_y;
                    eq_push(&ev);
                }
                if ((new_btns & 2) != (sdl_mouse_buttons & 2)) {
                    SDL_Event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.type = (new_btns & 2) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    ev.button.button = SDL_BUTTON_RIGHT;
                    ev.button.state = (new_btns & 2) ? 1 : 0;
                    ev.button.x = sdl_mouse_x;
                    ev.button.y = sdl_mouse_y;
                    eq_push(&ev);
                }
                sdl_mouse_buttons = new_btns;
            }
        } else {
            /* Keyboard scancode */
            if (data == 0x2A || data == 0x36) {
                sdl_kbd_shifted = 1;
            } else if (data == 0xAA || data == 0xB6) {
                sdl_kbd_shifted = 0;
            } else if (!(data & 0x80)) {
                char c = scancode_to_ascii(data, sdl_kbd_shifted);
                SDL_Event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = SDL_KEYDOWN;
                ev.key.scancode = data;
                ev.key.key_char = c;
                eq_push(&ev);
            }
        }
    }
}

int SDL_PollEvent(SDL_Event *event)
{
    poll_ps2_hardware();
    if (!event) return (sdl_eq_head != sdl_eq_tail);
    return eq_pop(event);
}

u32 SDL_GetMouseState(int *x, int *y)
{
    if (x) *x = sdl_mouse_x;
    if (y) *y = sdl_mouse_y;
    return sdl_mouse_buttons;
}

void SDL_SetMousePos(int x, int y)
{
    sdl_mouse_x = x;
    sdl_mouse_y = y;
}

u32 SDL_GetTicks(void)
{
    return sdl_tick_counter;
}
