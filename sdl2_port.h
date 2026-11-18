#ifndef SDL2_PORT_H
#define SDL2_PORT_H

#include "common.h"

/* SDL2-compatible initialization & window/renderer flags */
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_EVERYTHING     (SDL_INIT_VIDEO | SDL_INIT_EVENTS)

#define SDL_WINDOWPOS_CENTERED  0x2FFF0000
#define SDL_WINDOW_SHOWN        0x00000004u
#define SDL_WINDOW_BORDERLESS   0x00000010u

#define SDL_RENDERER_SOFTWARE     0x00000001u
#define SDL_RENDERER_ACCELERATED  0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u

/* Mouse buttons */
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3

/* Event types */
typedef enum {
    SDL_FIRSTEVENT      = 0,
    SDL_QUIT            = 0x100,
    SDL_KEYDOWN         = 0x300,
    SDL_KEYUP           = 0x301,
    SDL_MOUSEMOTION     = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP   = 0x402
} SDL_EventType;

typedef enum {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001
} SDL_BlendMode;

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} SDL_Color;

typedef struct {
    int x;
    int y;
} SDL_Point;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct {
    int w;
    int h;
    int pitch;
    u32 *pixels;
    int owned;
} SDL_Surface;

typedef struct {
    char title[64];
    int x;
    int y;
    int w;
    int h;
    u32 flags;
    int active;
} SDL_Window;

typedef struct {
    SDL_Window *window;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    SDL_BlendMode blend_mode;
    SDL_Rect clip;
    int has_clip;
} SDL_Renderer;

typedef struct {
    u32 type;
    int x;
    int y;
    int xrel;
    int yrel;
    u32 state;
} SDL_MouseMotionEvent;

typedef struct {
    u32 type;
    u8 button;
    u8 state;
    int x;
    int y;
} SDL_MouseButtonEvent;

typedef struct {
    u32 type;
    char key_char;
    u8 scancode;
} SDL_KeyboardEvent;

typedef union {
    u32 type;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_KeyboardEvent key;
} SDL_Event;

/* Lifecycle */
int  SDL_Init(u32 flags);
void SDL_Quit(void);
const char* SDL_GetError(void);

/* Window & Renderer */
SDL_Window*   SDL_CreateWindow(const char *title, int x, int y, int w, int h, u32 flags);
void          SDL_DestroyWindow(SDL_Window *window);
void          SDL_GetWindowSize(SDL_Window *window, int *w, int *h);

SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, u32 flags);
void          SDL_DestroyRenderer(SDL_Renderer *renderer);
int           SDL_SetRenderDrawColor(SDL_Renderer *renderer, u8 r, u8 g, u8 b, u8 a);
int           SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode);
int           SDL_RenderSetClipRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int           SDL_RenderClear(SDL_Renderer *renderer);
void          SDL_RenderPresent(SDL_Renderer *renderer);

/* 2D Primitives */
int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y);
int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2);
int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderFillRects(SDL_Renderer *renderer, const SDL_Rect *rects, int count);

/* Modern Port Extensions */
int SDL_RenderFillRoundedRect(SDL_Renderer *renderer, const SDL_Rect *rect, int radius);
int SDL_RenderDrawRoundedRect(SDL_Renderer *renderer, const SDL_Rect *rect, int radius, int thickness);
int SDL_RenderFillGradientV(SDL_Renderer *renderer, const SDL_Rect *rect, SDL_Color top, SDL_Color bottom);
int SDL_RenderFillCircle(SDL_Renderer *renderer, int cx, int cy, int radius);
int SDL_RenderDrawCircle(SDL_Renderer *renderer, int cx, int cy, int radius);
int SDL_RenderDrawShadow(SDL_Renderer *renderer, const SDL_Rect *rect, int radius, int spread, u8 max_alpha);
int SDL_RenderDrawText(SDL_Renderer *renderer, int x, int y, const char *text, SDL_Color color);
int SDL_RenderDrawTextScaled(SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color);

/* Surfaces */
SDL_Surface* SDL_CreateRGBSurface(u32 flags, int width, int height, int depth,
                                  u32 Rmask, u32 Gmask, u32 Bmask, u32 Amask);
void         SDL_FreeSurface(SDL_Surface *surface);
int          SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, u32 color);
int          SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                             SDL_Renderer *dst_renderer, const SDL_Rect *dstrect);

/* Events & Timing */
int  SDL_PollEvent(SDL_Event *event);
u32  SDL_GetMouseState(int *x, int *y);
void SDL_SetMousePos(int x, int y);
u32  SDL_GetTicks(void);

#endif
