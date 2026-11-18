# Input Subsystem

MyOS supports two input sources: a **PS/2 mouse** (with software cursor) and
a **PS/2 keyboard** (decoded to ASCII via scancode tables). Both are
polled from the kernel's event loops — there are no interrupt-driven input
handlers in the active path.

## Mouse (`mouse.c`)

### Initialization — `init_mouse()`

The routine talks to the PS/2 controller (command port `0x64`, data port
`0x60`):

1. `0xA8` — enable the auxiliary (mouse) device.
2. `0x20` read, set bit 2, `0x60` write — enable mouse IRQ (IRQ12) in the
   "compile" configuration byte.
3. `0xF6` then read — set mouse defaults.
4. `0xF4` then read — enable data reporting.
5. Center the cursor at `framebuffer_width/2, framebuffer_height/2`.

Low-level helpers `mouse_wait_read`/`mouse_wait_write` spin on the controller
status register (`0x64`) until it is safe to read/write, and `mouse_write`
sends a command by first writing `0xD4` to the command port.

### Packet decoding — `mouse_read_packet()` (in `kernel.c:190`)

The mouse emits 3-byte packets. The decoder (in `kernel.c`) reads status
from `0x64`, ignores keyboard data, and assembles a packet:

```c
signed char sdx = (signed char)pkt[1];   // X delta
signed char sdy = (signed char)pkt[2];   // Y delta
*dx = (int)sdx;
*dy = -(int)sdy;                          // screen Y is inverted
*buttons = pkt[0];                         // bit 0 = left
```

Callers accumulate `cur_x += dx`, `cur_y += dy` and clamp to the screen.

## Software cursor

Because there is no hardware cursor, `kernel.c` implements an 8×8 save/
restore cursor:

- `cursor_save_under(x,y)` snapshots 64 pixels into `cursor_under[][]`.
- `cursor_restore_under()` paints them back.
- `cursor_draw_shape()` paints an arrow triangle (`for j: for i<=j`).
- `cursor_move_to(x,y)` = restore previous + clamp + save + draw.

Every mouse movement and every redraw first restores the saved background so
the desktop never shows cursor "trails".

## Keyboard (`kernel.c` scancode decoder)

`keyboard_read_char()` (`kernel.c:267`) reads scancodes from `0x60`, tracks
the **shift** state (`0x2A`/`0x36` make, `0xAA`/`0xB6` break), and maps a
hard-coded scancode→char table. Special keys:

| Scancode | Result |
|----------|--------|
| `0x0E` | backspace (`\b`) |
| `0x1C` | Enter (`\n`) |
| `0x0F` | Tab (`\t`) |
| `& 0x80` | key-up (ignored) |

The `keyboard_scancode_to_char()` table covers digits, letters (with shifted
symbols), and punctuation. Non-mapped keys return `0` (no character).

Text-entry apps (text editor, grocery shop, card game) call
`keyboard_read_char()` in their loops; the calculator and games use mouse
clicks instead.

## Integration notes

- Input is **polling-only**. The UI loops call `mouse_read_packet()` and
  `keyboard_read_char()` each iteration; whichever has data is processed.
- Events are mutually exclusive checks within one loop body; there is no
  event queue, so very fast typing could drop keys (rare at human speed).

Next: [Network Core](networking/network-core.md).
