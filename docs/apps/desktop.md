# Desktop & Shell (`kernel.c`)

The graphical shell lives entirely in `kernel.c`. After the welcome screen,
`show_desktop()` presents a taskbar, a start button, and a popup start menu
with ten entries. Selecting an entry returns a small integer that `kmain`
dispatches to the corresponding application function.

## Welcome screen — `show_welcome()`

Painted as a branded NBU panel with a live **USB/network status** block
(`draw_welcome_status`, `welcome_status_code`). The status code drives a
color-coded message:

| Code | Meaning | Color |
|------|----------|--------|
| 0 | USB not connected | red |
| 1 | Tether device found, bind failed | amber |
| 2 | Attached, DHCP pending | amber |
| 3 | RNDIS/iPheth + DHCP ready | green |
| 4 | xHCI only — tether unsupported | red (banner) |
| 5 | No host controller | red |
| 6 | Only USB hubs visible | amber (banner) |

The screen also prints enumerated device diagnostics (VID:PID, interface
class/subclass/protocol, PORTSC) when devices are present. Clicking
**Start** returns `1` to `kmain`.

## Desktop — `show_desktop()`

- **Background** (`draw_desktop_background`) — four horizontal bands in
  NBU blues, a "North Bengal University OS" card, and a "Project Console"
  panel.
- **Network status** (`draw_desktop_network_status`) — shows NIC state:
  `DHCP BOUND`, `RNDIS/iPhone attached`, `Static IP ready`, etc.
- **Taskbar** (`draw_taskbar`) — dark bar with a Start button (four colored
  squares + "Start" label) and a "MyOS NBU Project" caption.
- **Start menu** (`draw_start_menu`) — a popup listing all apps; toggled by
  clicking the Start button.

Each menu entry maps to a return value:

| Return | App | Return | App |
|--------|-----|--------|-----|
| 1 | Calculator | 6 | High-Low Cards |
| 2 | Browser (Posts) | 7 | Keyboard Maze |
| 3 | JSON Viewer (Users) | 8 | Snake Game |
| 4 | Text Editor | 9 | Grocery Shop |
| 5 | Click Game | 10 | System Info |

## Common UI conventions

All apps share a look-and-feel:

- A window drawn with `draw_window_chrome()` (rounded border + blue title
  bar) or a manually `draw_rounded` frame.
- A red **close button** in the top-right; clicking it returns to the desktop.
- A centered window (`wx = (width-winw)/2`, etc.).
- An event loop that calls `net_poll()` every iteration, reads mouse/keyboard,
  mutates state, and repaints.
- The 8×8 software cursor (`cursor_move_to`) is restored/redrawn on every
  move so it never smears.

## Returning to the shell

Every app's loop returns when its close button is clicked. Control flows
back to `show_desktop()` (still inside the `kmain` `while(1)`), which
re-paints the desktop and waits for the next selection. There is no "logout"
— the shell runs until the VM is powered off.

Next: [App Catalog](app-catalog.md).
