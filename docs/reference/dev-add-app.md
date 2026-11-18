# Developer Guide: Adding a Desktop App

This walk-through shows how to add an 11th application to the desktop,
following the existing pattern in `kernel.c`.

## 1. Write the app function

Copy any existing `*_ui()` function. Keep the contract:

```c
static void myapp_ui(void) {
    draw_desktop_background();
    const int ww = 500, wh = 360;
    const int wx = (framebuffer_width  - ww) / 2;
    const int wy = (framebuffer_height - wh) / 2;
    draw_window_chrome(wx, wy, ww, wh, "My App");

    const int cx = wx + ww - 26, cy = wy + 5, cw = 20, ch = 20;
    draw_rect(cx, cy, cw, ch, 0xD62839);
    draw_string(cx + 5, cy + 2, "X", 0xFFFFFF);

    cursor_move_to(cur_x, cur_y);

    while (1) {
        net_poll();                      // keep network alive
        int dx, dy; unsigned char btn;
        if (mouse_read_packet(&dx, &dy, &btn)) {
            cursor_move_to(cur_x + dx, cur_y + dy);
            if (btn & 1) {
                if (cur_x > cx && cur_x < cx + cw &&
                    cur_y > cy && cur_y < cy + ch) {
                    cursor_restore_under();
                    return;                      // close -> desktop
                }
                /* handle your widgets here */
            }
        }
    }
}
```

**Rules of the pattern**
- Always `net_poll()` each loop iteration.
- Restore the cursor before you `return`.
- Redraw changed regions immediately after a mutation.

## 2. Add a start-menu entry

In `draw_start_menu()`, insert a new `draw_rect`+`draw_string` row (copy
an existing one and bump the `sm_y + N` offset by 30px).

## 3. Dispatch it

In `show_desktop()`, add an `if` block after the System Info check
(return value `11`), then in `kmain`'s `if (app==...)` chain add:

```c
} else if (app == 11) {
    myapp_ui();
}
```

## 4. Rebuild

```bash
./build.sh
```

Open the Start menu — your app appears at the bottom. Click its row, then
click the red **X** to return.

> Because apps are functions, not processes, you cannot run two at once and
> there is no window manager — only one window is visible at a time.
