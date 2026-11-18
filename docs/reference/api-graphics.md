# Graphics API Reference (`graphics.h` / `font.c`)

Declared in `graphics.h`, implemented in `graphics.c` (+ `font.c` for the
8×8 bitmap font). Colors are `u32` literals `0xAABBGGRR`.

## Global state (`graphics.c`)

```c
u32 framebuffer_addr, framebuffer_width, framebuffer_height;
u32 framebuffer_bpp, framebuffer_pitch;
```

Populated by `init_graphics(multiboot_info_t*)` which scans Multiboot2
tags for the `FRAMEBUFFER` tag (type 8).

## Pixel & shape primitives

| Function | Signature | Notes |
|----------|-----------|-------|
| `put_pixel` | `(int x, int y, u32 c)` | Bounds-checked write. |
| `get_pixel` | `(int x, int y) → u32` | Read pixel (cursor save). |
| `draw_rect` | `(int x, int y, int w, int h, u32 c)` | Filled rectangle. |
| `draw_char` | `(int x, int y, char c, u32 color)` | 8×8 glyph from `font[]`. |
| `draw_string` | `(int x, int y, const char *s, u32 color)` | 8px per char. |
| `xor_pixel` | `(int x, int y, u32 c)` | XOR pixel (alt. cursor). |
| `xor_cursor` | `(int x, int y)` | 6×6 XOR cursor. |

## Higher-level (declared in `graphics.h`)

`draw_button`, `draw_window`, `draw_line` are declared for richer UIs;
`kernel.c` instead builds its owns `draw_rounded`, `draw_panel`,
`draw_window_chrome`, and scaled-text helpers.

## Font

`font.c` defines `const u8 font[256][8]` — 256 8-byte rows, one bit per
pixel (MSB-left). `draw_char` tests `(glyph[i] >> (7 - j)) & 1`.

## Scaled helpers (in `kernel.c`)

| Helper | Purpose |
|---------|---------|
| `draw_char_scaled(x,y,c,scale,color)` | Scale factor × 8×8 via `draw_rect`. |
| `draw_string_scaled(x,y,s,scale,color)` | Scaled string. |
| `string_px_scaled(s,scale)` | Pixel width = `strlen*8*scale`. |

## Color palette used by the UI

| Value | Name | Use |
|-------|------|-----|
| `0x145DA0` | NBU blue | Title bars, accents |
| `0x2B3440` | dark slate | Window borders |
| `0xE8EDF3` | near-white | Window interiors |
| `0x87CEEB` | sky blue | App background |
| `0xD62839` / `0xFF0000` | red | Close buttons |
| `0x2F80ED` | blue | Game targets, snake head |
| `0x45B36B` | green | Snake body |
| `0xF2B705` | amber | Maze coins |

See also: [Graphics](../kernel/graphics.md).
