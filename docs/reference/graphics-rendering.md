# Graphics Deep-Dive: Rendering

How the desktop actually gets pixels on screen, complementing
[Graphics](../kernel/graphics.md) and the [API ref](../reference/api-graphics.md).

## Framebuffer mapping

`init_graphics()` reads the Multiboot2 `FRAMEBUFFER` tag (type 8) and
stores:

```
framebuffer_addr    (physical == virtual here)
framebuffer_width
framebuffer_height
framebuffer_bpp       (4 = 32 bpp)
framebuffer_pitch     (== width*4 for linear)
```

A pixel at `(x,y)` is at offset `y*(pitch/4) + x` in a `u32*`.

## Pixel format

Little-endian `0xAABBGGRR`:

```
bit 31..24  alpha (unused)
bit 23..16  blue
bit 15..8   green
bit 7..0    red
```

So `0x145DA0` = R=0xA0, G=0x5D, B=0x14 (NBU blue), and
`0x2F80ED` = R=0xED, G=0x80, B=0x2F (bright blue target).

## Draw call chain (typical window)

```
draw_desktop_background()        // 4 band rects + cards
  └─ draw_rect (per band)
draw_window_chrome(x,y,w,h,title)
  └─ draw_rounded(border) + draw_rect(interior) + draw_rect(titlebar) + draw_string(title)
draw_string(x,y,s,color)
  └─ for each char: draw_char
        └─ for 8x8: put_pixel if glyph bit set
put_pixel(x,y,c)                // bounds-checked write
```

## Text & font

`font.c` holds `const u8 font[256][8]` — 256 glyphs, 8 rows of 8 bits.
`draw_char` tests `(glyph[row] >> (7 - col)) & 1`, so MSB is the
leftmost pixel. Only the glyphs present in the array render; unmapped
codes draw as blank.

For large titles, `kernel.c` uses `draw_char_scaled` which calls
`draw_rect` per scaled pixel (scale ×8×8), giving crisp big text
(e.g. the welcome "NORTH BENGAL UNIVERSITY" at scale 3).

## The software cursor

Because there is no hardware cursor, every move is a save/restore:

```
cursor_move_to(x,y):
    cursor_restore_under()            // repaint saved 8x8 bg
    clamp(x,y)
    cursor_save_under(x,y)           // snapshot 8x8 bg into cursor_under[][]
    cursor_draw_shape(x,y,0x000000)  // 8x8 arrow (i<=j triangle)
```

`draw_*` helpers for windows call `cursor_restore_under()` *before* a
redraw so the cursor never smears, then `cursor_move_to()` afterward.

## Clipping & cost

`put_pixel`/`get_pixel` clamp to `[0,width)×[0,height)`. All drawing
is unaccelerated CPU loops — a full-window repaint iterates every pixel.
This is why the UI repaints only on input and avoids animation. At the
modest 1450×1000 resolution and simple UIs, it is plenty fast.

## Color palette (recap)

| Value | Use |
|-------|-----|
| `0x145DA0` | NBU blue (title bars) |
| `0x2B3440` | dark window border |
| `0xE8EDF3` | window interior |
| `0x87CEEB` | app background |
| `0xD62839` / `0xFF0000` | close-button red |
| `0x2F80ED` | game targets / snake head |
| `0x45B36B` | snake body |
| `0xF2B705` | maze coins |

See also: [Input](../kernel/input.md) for cursor movement drivers.
