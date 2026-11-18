# Control Flow

Because MyOS is a single-address-space, cooperative kernel, "control flow" is
really about **event loops** rather than threads or processes. This page
documents the principal loops and how data moves between them.

## The two top-level loops

### `kmain` main loop (`kernel.c:2025`)

```c
if (show_welcome()) {
    while (1) {
        usb_poll();
        net_poll();              // advances USB enum + NIC RX + DHCP
        if (no netif) retry NIC + dhcp_start();
        int app = show_desktop();  // blocks until an app is chosen
        launch_app(app);          // runs app, returns on close
    }
}
```

### `net_poll()` — the network heartbeat (`net.c:132`)

Every UI loop calls `net_poll()` on each iteration. It is the single point
that keeps the network stack alive:

```c
void net_poll(void) {
    usb_poll();                       // keep USB host progressing
    if (g_active_netif && g_active_netif->poll)
        g_active_netif->poll();        // drain NIC RX into net_rx()
    dhcp_poll();                     // advance DHCP state machine
}
```

`net_rx()` demultiplexes incoming frames by EtherType (ARP vs IPv4), then by
IPv4 protocol (UDP vs TCP). UDP datagrams are routed to DNS, DHCP, or the
weak `udp_on_datagram` hook; TCP segments are handed to `tcp_on_rx()`.

## Event loop anatomy (applications)

Every desktop application follows the same pattern:

```c
void some_app_ui(void) {
    draw_...();                       // paint window once
    cursor_move_to(cur_x, cur_y);
    while (1) {
        net_poll();                   // keep network alive while idle
        if (keyboard_read_char(&k)) { /* handle key */ redraw(); }
        if (mouse_read_packet(&dx,&dy,&btn)) {
            cursor_move_to(cur_x+dx, cur_y+dy);
            if (btn & 1) {            // left click
                if (inside close button) return;
                if (inside widget)     { /* handle */ redraw(); }
            }
        }
    }
}
```

Key invariants:

- **Cursor save/restore.** `cursor_move_to()` calls `cursor_restore_under()`
  first, so the 8×8 software cursor never smears the desktop.
- **Redraw after mutation.** Any state change (typing, clicking) immediately
  repaints the relevant region. There is no dirty-rectangle compositor.
- **No blocking.** `net_poll()` is non-blocking; the apps never `hlt` waiting
  for a packet, so the mouse stays responsive.

## Network request flow (HTTP browser example)

```
browser_ui()
  └─ parse_url("http://host:port/path")
  └─ dns_resolve(host) ──────────────► dns.c
        queues DNS query, spins net_poll() until cached
  └─ http_get_by_ip_port(ip, port, host, path, buf)
        └─ tcp_connect()  (SYN)         ─► tcp.c
        └─ wait: net_poll until ESTABLISHED
        └─ tcp_send(GET)                 ─► tcp.c
        └─ loop: net_poll + tcp_recv     ─► accumulates raw HTTP
        └─ parse status, follow redirects (≤5)
        └─ extract body after "\r\n\r\n"
```

The TLS path (`tls_http_get_by_ip`) is identical except `tcp.c` is wrapped by
mbedTLS BIO callbacks (`net_send`/`net_recv` in `tls_mbedtls.c`) and the
application data is read via `mbedtls_ssl_read`.

## IRQ vs polling

MyOS is **entirely polling-based**. The `irqstubs.S` file and the `int 0x80`
IDT entry exist (the latter for the user-program syscall gate), but NIC and USB
receive are driven by periodic `poll()` calls from `net_poll()` rather than by
hardware interrupt handlers. This dramatically simplifies the kernel at the cost
of some CPU busy-looping — an acceptable trade-off for a teaching OS.

Next: [System Overview](../architecture/overview.md) · [Boot Flow](boot-flow.md).
