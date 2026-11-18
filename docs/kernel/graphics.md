# Graphics Subsystem

The graphics layer (`graphics.c`, `font.c`, `graphics.h`) is a minimal
software framebuffer renderer. It draws pixels, rectangles, text using a
built-in 8×8 bitmap font, and a couple of higher-level primitives used by
the desktop chrome.

## Framebuffer model

MyOS receives a **linear (pitch = width × 4) 32-bpp RGB framebuffer** from
the Multiboot2 framebuffer tag. `init_graphics()` (`graphics.c:7`) caches:

```c
u32 framebuffer_addr;     // physical == virtual here
u32 framebuffer_width;
u32 framebuffer_height;
u32 framebuffer_bpp;
u32 framebuffer_pitch;
```

Pixels are stored little-endian as `0xAABBGGRR` words. Because pitch equals
`width*4`, the pixel at `(x,y)` is:

```c
((u32*)framebuffer_addr)[ y * (framebuffer_pitch/4) + x ] = color;
```

## Core primitives

| Function | Description |
|----------|-------------|
| `put_pixel(x,y,c)` | Bounds-checked single pixel write |
| `get_pixel(x,y)` | Read a pixel (used for cursor save/restore) |
| `draw_rect(x,y,w,h,c)` | Filled rectangle (nested loops) |
| `draw_char(x,y,c,color)` | Renders glyph `font[(int)c]` (8×8) |
| `draw_string(x,y,s,color)` | Calls `draw_char` per character (8 px advance) |
| `xor_pixel(x,y,c)` / `xor_cursor(x,y)` | XOR-based cursor (alt. to save/restore) |

The font is a 256-entry 8×8 bitmap array (`font.c`) indexed by character
code. Text is therefore limited to the Latin-1-ish glyphs present in the
array.

## Higher-level helpers (declared in `graphics.h`)

`graphics.h` advertises richer primitives — `draw_button`, `draw_window`,
`draw_line` — but the desktop implementation in `kernel.c` mostly draws with
`draw_rect` + `draw_string` plus its own `draw_rounded`/`draw_panel`/
`draw_window_chrome` helpers. Those helpers live in `kernel.c` and implement:

- `draw_rounded(x,y,w,h,r,col)` — filled rounded rectangle by composing
  rects and a per-corner Bresenham-ish circle test (`dx*dx + dy*dy <= r*r`).
- `draw_panel(x,y,w,h,border,fill)` — double rounded rect (border + inset).
- `draw_window_chrome(x,y,w,h,title)` — title-bar window used by apps.

## Color encoding

Colors are `u32` literals of the form `0xAABBGGRR`. Common palette values
used across the UI:

| Name | Value | Use |
|------|-------|-----|
| `0x145DA0` | NBU blue | Title bars, accents |
| `0x2B3440` | dark slate | Window borders |
| `0xE8EDF3` | near-white | Window interiors |
| `0x87CEEB` | sky blue | App background fill |
| `0xD62839` / `0xFF0000` | red | Close buttons |
| `0x2F80ED` | blue | Game targets, snake head |

## Scaled text

`kernel.c` adds `draw_char_scaled` / `draw_string_scaled` /
`string_px_scaled` for the welcome screen's large "NORTH BENGAL UNIVERSITY"
title (scale factor 3). These call `draw_rect` per scaled pixel rather than
`put_pixel`, for speed.

## Performance characteristics

Everything is **unaccelerated CPU drawing**. A full-window repaint is a
nested loop over pixels. This is fine at the modest 1450×1000 resolution and
for the simple UIs in this project, but it is the main reason the design
avoids animation and instead repaints only on user input.

Next: [Input](input.md).
