// kernel.c — Modern double-buffered UI + SDL2 port (welcome -> desktop -> apps)
#include "multiboot.h"
#include "graphics.h"
#include "sdl2_port.h"
#include "scheduler.h"
#include "wm.h"
#include "mouse.h"
#include "io.h"
#include "string.h"
#include "http.h"
#include "dns.h"
#include "tls.h"
#include "userprog_hello.h"
#include "exec_elf.h"
#include "syscalls.h"
#include "stdio.h"
#include "json.h"

/* Expose mbedTLS debug buffer accessor implemented in platform_shim.c */
extern const char *mbedtls_get_debug(void);


// Networking
#include "net.h"
// USB host API (for status display)
#include "drivers/usb/usb_host.h"
#include "drivers/usb/usb_ipheth.h"
#include "drivers/usb/usb_rndis.h"
#include "drivers/usb/usb_cdc_ecm.h"
#include "drivers/e1000e.h"
#include "dhcp.h"

typedef unsigned int u32;

static inline int clampi(int v,int lo,int hi){
    if(v<lo) return lo;
    if(v>hi) return hi;
    return v;
}

static void draw_rounded(int x,int y,int w,int h,int r,u32 col){
    draw_rounded_rect(x, y, w, h, r, col | 0xFF000000u);
}

static void draw_panel(int x,int y,int w,int h,u32 border,u32 fill){
    draw_rounded_rect(x, y, w, h, 10, border | 0xFF000000u);
    draw_rounded_rect(x+1, y+1, w-2, h-2, 9, fill | 0xFF000000u);
}

static void draw_window_chrome(int x,int y,int w,int h,const char *title){
    draw_shadow(x, y, w, h, 12, 8, 90);
    draw_rounded_rect(x, y, w, h, 12, 0xFF1E293B);
    draw_rect(x+2, y+2, w-4, h-4, 0xF8FAFC);
    draw_gradient_v(x+2, y+2, w-4, 30, 0x1E3A5F, 0x0F172A);
    draw_rect(x+2, y+31, w-4, 1, 0x38BDF8);
    draw_string(x+14, y+13, title, 0xF8FAFC);
}

static void draw_desktop_background(void){
    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;
    int mid = h / 2;
    draw_gradient_v(0, 0, w, mid, 0x09111E, 0x10243E);
    draw_gradient_v(0, mid, w, h - mid, 0x10243E, 0x163859);

    /* Subtle atmospheric ambient glows */
    draw_circle_filled(w - 180, 140, 160, gfx_rgba(56, 189, 248, 16));
    draw_circle_filled(220, h - 180, 200, gfx_rgba(99, 102, 241, 14));

    /* Top glass status bar */
    draw_rect_alpha(0, 0, w, 34, gfx_rgba(9, 17, 30, 215));
    draw_rect_alpha(0, 34, w, 1, gfx_rgba(148, 163, 184, 45));
    draw_rounded_rect(12, 6, 84, 22, 6, 0xFF0EA5E9);
    draw_string(22, 13, "MyOS 2.0", 0xFFFFFF);
    draw_string(110, 13, "North Bengal University  |  x86 Workstation", 0xCBD5E1);
}

static void draw_taskbar(int bar_h,int sb_x,int sb_y,int sb_w,int sb_h){
    int w = (int)framebuffer_width;
    int y = (int)framebuffer_height - bar_h;
    draw_rect_alpha(0, y, w, bar_h, gfx_rgba(11, 18, 32, 232));
    draw_rect_alpha(0, y, w, 1, gfx_rgba(56, 189, 248, 95));

    draw_rounded_rect(sb_x, sb_y, sb_w, sb_h, 10, 0xFF0284C7);
    draw_rounded_outline(sb_x, sb_y, sb_w, sb_h, 10, 1, gfx_rgba(255, 255, 255, 55));
    draw_rect(sb_x+10, sb_y+8,  10, 10, 0xFACC15);
    draw_rect(sb_x+22, sb_y+8,  10, 10, 0x4ADE80);
    draw_rect(sb_x+10, sb_y+20, 10, 10, 0x38BDF8);
    draw_rect(sb_x+22, sb_y+20, 10, 10, 0xF472B6);
    draw_string(sb_x+42, sb_y+15, "Start", 0xFFFFFF);

    draw_string(w - 208, y + (bar_h - 8)/2, "MyOS SDL2  |  NBU OS", 0x94A3B8);
}

static void draw_char_scaled(int x,int y,char c,int scale,u32 color){
    extern const u8 font[256][8];
    const u8 *glyph = font[(int)c];
    for(int row=0; row<8; row++){
        for(int col=0; col<8; col++){
            if((glyph[row] >> (7-col)) & 1){
                draw_rect(x + col*scale, y + row*scale, scale, scale, color);
            }
        }
    }
}

static void draw_string_scaled(int x,int y,const char *s,int scale,u32 color){
    for(int i=0; s[i]; i++){
        draw_char_scaled(x + i*8*scale, y, s[i], scale, color);
    }
}

static int string_px_scaled(const char *s,int scale){
    return strlen(s) * 8 * scale;
}

/* ------------------------------------------------------------
Static software cursor (12x16 arrow with white border)
----------------------------------------------------------*/
#define CUR_W 12
#define CUR_H 16
static u32 cursor_under[CUR_H][CUR_W];
static int cur_saved = 0;
static int cur_x = 0, cur_y = 0;

/* UDP proxy state for host-side UDP->HTTP helper */
static volatile int udp_proxy_ready = 0;
static int udp_proxy_len = 0;
static char udp_proxy_buf[2048];

static void cursor_save_under(int x, int y){
    for(int j=0;j<CUR_H;j++){
        for(int i=0;i<CUR_W;i++){
            int px = x+i, py = y+j;
            if(px>=0 && py>=0 && px<(int)framebuffer_width && py<(int)framebuffer_height){
                cursor_under[j][i] = get_pixel(px,py);
            }else{
                cursor_under[j][i] = 0;
            }
        }
    }
    cur_saved = 1;
}

static void cursor_restore_under(void){
    if(!cur_saved) return;
    for(int j=0;j<CUR_H;j++){
        for(int i=0;i<CUR_W;i++){
            int px = cur_x+i, py = cur_y+j;
            if(px>=0 && py>=0 && px<(int)framebuffer_width && py<(int)framebuffer_height){
                put_pixel(px,py,cursor_under[j][i]);
            }
        }
    }
    cur_saved = 0;
}

static void cursor_draw_shape(int x, int y, u32 col){
    static const char *arrow[CUR_H] = {
        "W...........",
        "WW..........",
        "WBW.........",
        "WBBW........",
        "WBBBW.......",
        "WBBBBW......",
        "WBBBBBW.....",
        "WBBBBBBW....",
        "WBBBBBBBW...",
        "WBBBBBBBBW..",
        "WBBBBBBWWWW.",
        "WBBWBBW.....",
        "WBW.WBBW....",
        "WW..WBBW....",
        "W....WW.....",
        "............"
    };
    for(int j=0;j<CUR_H;j++){
        for(int i=0;i<CUR_W;i++){
            char c = arrow[j][i];
            if(c == '.') continue;
            int px=x+i, py=y+j;
            if(px>=0 && py>=0 && px<(int)framebuffer_width && py<(int)framebuffer_height){
                put_pixel(px,py,(c=='W') ? 0xFFFFFF : col);
            }
        }
    }
}

static void cursor_draw_front(int x, int y){
    if(!framebuffer_addr) return;
    static const char *arrow[CUR_H] = {
        "W...........",
        "WW..........",
        "WBW.........",
        "WBBW........",
        "WBBBW.......",
        "WBBBBW......",
        "WBBBBBW.....",
        "WBBBBBBW....",
        "WBBBBBBBW...",
        "WBBBBBBBBW..",
        "WBBBBBBWWWW.",
        "WBBWBBW.....",
        "WBW.WBBW....",
        "WW..WBBW....",
        "W....WW.....",
        "............"
    };
    u32 stride = framebuffer_pitch / 4;
    u32 *front = (u32*)framebuffer_addr;
    for(int j=0;j<CUR_H;j++){
        int py = y + j;
        if(py < 0 || py >= (int)framebuffer_height) continue;
        for(int i=0;i<CUR_W;i++){
            char c = arrow[j][i];
            if(c == '.') continue;
            int px = x + i;
            if(px >= 0 && px < (int)framebuffer_width){
                front[py * stride + px] = (c=='W') ? 0xFFFFFF : 0x0F172A;
            }
        }
    }
}

static void cursor_move_to(int x, int y){
    cursor_restore_under();
    cur_x = clampi(x,0,(int)framebuffer_width-1);
    cur_y = clampi(y,0,(int)framebuffer_height-1);
    cursor_save_under(cur_x,cur_y);
    cursor_draw_shape(cur_x,cur_y,0x0F172A);
}

/* ------------------------------------------------------------
- 
Input: read one PS/2 mouse packet (3 bytes) if available
----------------------------------------------------------*/
static int mouse_read_packet(int *dx, int *dy, unsigned char *buttons){
    static unsigned char pkt[3];
    static int idx = 0;

    unsigned char status = inb(0x64);
    if(!(status&1)) return 0; // no data
    if(!(status&0x20)) {
        (void)inb(0x60); // drain keyboard data in mouse-only screens
        return 0;
    }
    unsigned char d = inb(0x60);
    pkt[idx++] = d;
    if(idx<3) return 0;

    idx = 0; // complete packet
    signed char sdx = (signed char)pkt[1];
    signed char sdy = (signed char)pkt[2];
    *dx = (int)sdx;
    *dy = -(int)sdy;
    *buttons = pkt[0];
    return 1;
}

static char keyboard_scancode_to_char(unsigned char sc, int shifted){
    switch(sc){
        case 0x02: return shifted ? '!' : '1';
        case 0x03: return shifted ? '@' : '2';
        case 0x04: return shifted ? '#' : '3';
        case 0x05: return shifted ? '$' : '4';
        case 0x06: return shifted ? '%' : '5';
        case 0x07: return shifted ? '^' : '6';
        case 0x08: return shifted ? '&' : '7';
        case 0x09: return shifted ? '*' : '8';
        case 0x0A: return shifted ? '(' : '9';
        case 0x0B: return shifted ? ')' : '0';
        case 0x0C: return shifted ? '_' : '-';
        case 0x0D: return shifted ? '+' : '=';
        case 0x10: return shifted ? 'Q' : 'q';
        case 0x11: return shifted ? 'W' : 'w';
        case 0x12: return shifted ? 'E' : 'e';
        case 0x13: return shifted ? 'R' : 'r';
        case 0x14: return shifted ? 'T' : 't';
        case 0x15: return shifted ? 'Y' : 'y';
        case 0x16: return shifted ? 'U' : 'u';
        case 0x17: return shifted ? 'I' : 'i';
        case 0x18: return shifted ? 'O' : 'o';
        case 0x19: return shifted ? 'P' : 'p';
        case 0x1A: return shifted ? '{' : '[';
        case 0x1B: return shifted ? '}' : ']';
        case 0x1E: return shifted ? 'A' : 'a';
        case 0x1F: return shifted ? 'S' : 's';
        case 0x20: return shifted ? 'D' : 'd';
        case 0x21: return shifted ? 'F' : 'f';
        case 0x22: return shifted ? 'G' : 'g';
        case 0x23: return shifted ? 'H' : 'h';
        case 0x24: return shifted ? 'J' : 'j';
        case 0x25: return shifted ? 'K' : 'k';
        case 0x26: return shifted ? 'L' : 'l';
        case 0x27: return shifted ? ':' : ';';
        case 0x28: return shifted ? '"' : '\'';
        case 0x29: return shifted ? '~' : '`';
        case 0x2B: return shifted ? '|' : '\\';
        case 0x2C: return shifted ? 'Z' : 'z';
        case 0x2D: return shifted ? 'X' : 'x';
        case 0x2E: return shifted ? 'C' : 'c';
        case 0x2F: return shifted ? 'V' : 'v';
        case 0x30: return shifted ? 'B' : 'b';
        case 0x31: return shifted ? 'N' : 'n';
        case 0x32: return shifted ? 'M' : 'm';
        case 0x33: return shifted ? '<' : ',';
        case 0x34: return shifted ? '>' : '.';
        case 0x35: return shifted ? '?' : '/';
        case 0x39: return ' ';
        default: return 0;
    }
}

static int keyboard_read_char(char *out){
    static int shifted = 0;
    unsigned char status = inb(0x64);
    if(!(status&1)) return 0;
    if(status&0x20) return 0;

    unsigned char sc = inb(0x60);
    if(sc == 0x2A || sc == 0x36){
        shifted = 1;
        return 0;
    }
    if(sc == 0xAA || sc == 0xB6){
        shifted = 0;
        return 0;
    }
    if(sc & 0x80) return 0;

    if(sc == 0x0E){ *out = '\b'; return 1; }
    if(sc == 0x1C){ *out = '\n'; return 1; }
    if(sc == 0x0F){ *out = '\t'; return 1; }

    char c = keyboard_scancode_to_char(sc, shifted);
    if(c){
        *out = c;
        return 1;
    }
    return 0;
}

/* ===========================================================
Helpers for start menu (file-scope, no nested funcs)
===========================================================*/
typedef struct {
    const char *label;
    int app_id;
    u32 accent;
} start_menu_item_t;

static const start_menu_item_t start_items[] = {
    { "Calculator",          1,  0x38BDF8 },
    { "Browser (Posts)",     2,  0x60A5FA },
    { "JSON Viewer (Users)", 3,  0x818CF8 },
    { "Text Editor",         4,  0x34D399 },
    { "SDL2 Studio (Demo)",  11, 0xF472B6 },
    { "Click Game",          5,  0xFBBF24 },
    { "High-Low Cards",      6,  0xFB923C },
    { "Keyboard Maze",       7,  0x4ADE80 },
    { "Snake Game",          8,  0x2DD4BF },
    { "Grocery Shop",        9,  0xA78BFA },
    { "System Info",         10, 0x94A3B8 },
    { "Task Manager",        12, 0x22D3EE },
    { "Billing POS",         13, 0xF59E0B },
    { "Run embedded hello",  99, 0xE879F9 }
};
#define START_ITEM_COUNT ((int)(sizeof(start_items) / sizeof(start_items[0])))

static void draw_start_menu(int sm_x,int sm_y,int sm_w,int sm_h,int hover_idx){
    draw_shadow(sm_x, sm_y, sm_w, sm_h, 14, 10, 110);
    draw_rounded_rect(sm_x, sm_y, sm_w, sm_h, 14, gfx_rgba(15, 23, 42, 244));
    draw_rounded_outline(sm_x, sm_y, sm_w, sm_h, 14, 1, gfx_rgba(148, 163, 184, 70));

    draw_gradient_v(sm_x+2, sm_y+2, sm_w-4, 38, 0x1E3A5F, 0x0F172A);
    draw_rect_alpha(sm_x+2, sm_y+40, sm_w-4, 1, gfx_rgba(56, 189, 248, 90));
    draw_string(sm_x+16, sm_y+16, "Applications", 0xF8FAFC);
    draw_string(sm_x+sm_w-74, sm_y+16, "MyOS", 0x38BDF8);

    for(int i=0; i<START_ITEM_COUNT; i++){
        int iy = sm_y + 48 + i * 30;
        int is_hover = (i == hover_idx);
        u32 row_col = is_hover ? gfx_rgba(56, 189, 248, 55) : gfx_rgba(30, 41, 59, 175);
        draw_rounded_rect(sm_x+10, iy, sm_w-20, 26, 7, row_col);
        if (is_hover) {
            draw_rounded_outline(sm_x+10, iy, sm_w-20, 26, 7, 1, gfx_rgba(56, 189, 248, 160));
        }
        draw_rounded_rect(sm_x+16, iy+6, 4, 14, 2, start_items[i].accent | 0xFF000000u);
        draw_string(sm_x+28, iy+9, start_items[i].label, is_hover ? 0xFFFFFF : 0xE2E8F0);
    }
}

static void clear_start_menu(int sm_x,int sm_y,int sm_w,int sm_h,
                            int bar_h,int sb_x,int sb_y,int sb_w,int sb_h){
    (void)sm_x; (void)sm_y; (void)sm_w; (void)sm_h;
    draw_desktop_background();
    draw_taskbar(bar_h,sb_x,sb_y,sb_w,sb_h);
}

/* ===========================================================
WELCOME SCREEN
===========================================================*/
#define USB_CLASS_HUB 0x09

static int usb_device_is_hub(const struct usb_device_info *dev) {
    if (!dev) return 0;
    if (dev->dev_class == USB_CLASS_HUB) return 1;
    for (int i = 0; i < dev->num_interfaces; ++i) {
        if (dev->interfaces[i].intf_class == USB_CLASS_HUB) {
            return 1;
        }
    }
    return 0;
}

static int usb_only_hub_devices_connected(void) {
    int usb_count = usb_get_device_count();
    if (usb_count <= 0) return 0;
    for (int i = 0; i < usb_count; ++i) {
        if (!usb_device_is_hub(usb_get_device(i))) {
            return 0;
        }
    }
    return 1;
}

static int usb_has_rndis_candidate_device(void) {
    int usb_count = usb_get_device_count();
    for (int i = 0; i < usb_count; ++i) {
        const struct usb_device_info *dev = usb_get_device(i);
        if (!dev) continue;
        for (int j = 0; j < dev->num_interfaces; ++j) {
            const struct usb_interface_info *intf = &dev->interfaces[j];
            if ((intf->intf_class == USB_CLASS_WIRELESS &&
                 intf->intf_subclass == USB_SUBCLASS_RNDIS &&
                 intf->intf_protocol == USB_PROTOCOL_RNDIS) ||
                intf->intf_class == USB_CLASS_CDC_DATA) {
                return 1;
            }
        }
    }
    return 0;
}

static int usb_has_ipheth_candidate_device(void) {
    int usb_count = usb_get_device_count();
    for (int i = 0; i < usb_count; ++i) {
        const struct usb_device_info *dev = usb_get_device(i);
        if (!dev || dev->vid != USB_VENDOR_APPLE) continue;
        return 1;
        for (int j = 0; j < dev->num_interfaces; ++j) {
            const struct usb_interface_info *intf = &dev->interfaces[j];
            if (usb_ipheth_is_apple_candidate_interface(intf->intf_number,
                                                        intf->intf_class,
                                                        intf->intf_subclass,
                                                        intf->intf_protocol)) {
                return 1;
            }
        }
    }
    return 0;
}

static int welcome_status_code(void) {
    if (usb_host_has_unsupported_xhci()) return 4;
    if (!usb_host_has_controller()) return 5;
    if (usb_get_device_count() <= 0) return 0;
    if (usb_only_hub_devices_connected()) return 6;
    if (!rndis_active && !ipheth_active && !cdc_ecm_active) return 1;
    if (!dhcp_is_bound()) return 2;
    return 3;
}

static void draw_welcome_status(int bx, int by, int bw, int bh) {
    int usb_count = usb_get_device_count();
    int sy = by + 172;

    draw_rect(bx + 40, sy, bw - 80, 52, 0xE2EDF3);

    switch (welcome_status_code()) {
        case 4: {
            int wx = bx - 60;
            int wy = by + bh + 20;
            int ww = bw + 120;
            int wh = 90;
            draw_rect(wx, wy, ww, wh, 0xAA0000);
            draw_rect(wx + 4, wy + 4, ww - 8, wh - 8, 0xFFF0F0);
            draw_string(wx + 16, wy + 14, "USB TETHERING BLOCKED", 0xAA0000);
            draw_string(wx + 16, wy + 38, "Laptop exposes xHCI USB only.", 0x000000);
            draw_string(wx + 16, wy + 54, "This kernel supports EHCI only.", 0x000000);
            draw_string(wx + 16, wy + 70, "Phone tethering will not work yet.", 0x000000);
            draw_string(bx + 40, sy + 4, "USB: xHCI detected (unsupported)", 0xFF0000);
            break;
        }
        case 0:
            draw_string(bx + 80, sy + 4, "USB: Not connected", 0xFF0000);
            break;
        case 5:
            draw_string(bx + 56, sy + 4, "USB: Host controller not detected", 0xFF0000);
            break;
        case 1:
            if (usb_has_ipheth_candidate_device()) {
                draw_string(bx + 52, sy + 4, "USB: iPhone tether found (bind failed)", 0xA66A00);
            } else if (usb_has_rndis_candidate_device()) {
                draw_string(bx + 52, sy + 4, "USB: Tethering device found (bind failed)", 0xA66A00);
            } else {
                draw_string(bx + 44, sy + 4, "USB: Device found, but not a net adapter", 0xA66A00);
            }
            break;
        case 2:
            if (ipheth_active) {
                draw_string(bx + 80, sy + 4, "USB: iPhone attached (DHCP pending)", 0xA66A00);
            } else if (cdc_ecm_active) {
                draw_string(bx + 80, sy + 4, "USB: CDC-ECM attached (DHCP pending)", 0xA66A00);
            } else {
                draw_string(bx + 80, sy + 4, "USB: RNDIS attached (DHCP pending)", 0xA66A00);
            }
            break;
        case 6: {
            int wx = bx - 60;
            int wy = by + bh + 20;
            int ww = bw + 120;
            int wh = 90;
            draw_rect(wx, wy, ww, wh, 0xCC8800);
            draw_rect(wx + 4, wy + 4, ww - 8, wh - 8, 0xFFF8E8);
            draw_string(wx + 16, wy + 14, "USB HUB DETECTED", 0xAA6600);
            draw_string(wx + 16, wy + 38, "Only hub-class devices are visible.", 0x000000);
            draw_string(wx + 16, wy + 54, "Direct phone tethering is not reachable yet.", 0x000000);
            draw_string(wx + 16, wy + 70, "Try a direct USB port or simpler adapter.", 0x000000);
            draw_string(bx + 64, sy + 4, "USB: Only hub-class devices found", 0xCC8800);
            break;
        }
        default:
            if (ipheth_active) {
                draw_string(bx + 80, sy + 4, "USB: iPhone DHCP ready", 0x00AA00);
            } else if (cdc_ecm_active) {
                draw_string(bx + 80, sy + 4, "USB: CDC-ECM + DHCP ready", 0x00AA00);
            } else {
                draw_string(bx + 80, sy + 4, "USB: RNDIS + DHCP ready", 0x00AA00);
            }
            break;
    }

    char diag[64];
    snprintf(diag, sizeof(diag), "USB devices: %d", usb_count);
    draw_string(bx + 80, sy + 29, diag, 0x444444);

    char ctrl[64];
    if (usb_host_using_ehci()) {
        snprintf(ctrl, sizeof(ctrl), "USB host: EHCI active");
    } else if (usb_host_using_xhci()) {
        snprintf(ctrl, sizeof(ctrl), "USB host: xHCI active");
    } else if (usb_host_has_xhci()) {
        snprintf(ctrl, sizeof(ctrl), "USB host: xHCI init failed");
    } else if (usb_host_has_ehci()) {
        snprintf(ctrl, sizeof(ctrl), "USB host: EHCI init failed");
    } else {
        snprintf(ctrl, sizeof(ctrl), "USB host: none");
    }
    draw_string(bx + 80, sy + 44, ctrl, 0x444444);

    char ports[64];
    snprintf(ports, sizeof(ports), "USB ports: %d", usb_host_connected_port_count());
    draw_string(bx + 80, sy + 59, ports, 0x444444);

    if (usb_host_first_connected_portsc() != 0) {
        char portsc[64];
        snprintf(portsc, sizeof(portsc), "PORTSC: %08x", usb_host_first_connected_portsc());
        draw_string(bx + 80, sy + 74, portsc, 0x444444);
    }

    if (usb_count == 0 && usb_host_connected_port_count() > 0) {
        char err[64];
        snprintf(err, sizeof(err), "USB err: %s", usb_host_last_error());
        draw_string(bx + 80, sy + 89, err, 0xAA0000);
    } else if (usb_count > 0) {
        const struct usb_device_info *dev = usb_get_device(0);
        if (dev) {
            char devline[64];
            snprintf(devline, sizeof(devline), "VID:PID %04x:%04x ifs:%d", dev->vid, dev->pid, dev->num_interfaces);
            draw_string(bx + 80, sy + 89, devline, 0x444444);
            if (dev->num_interfaces > 0) {
                const struct usb_interface_info *intf = &dev->interfaces[0];
                char ifline[64];
                snprintf(ifline, sizeof(ifline), "IF0 n%d a%d c%02x s%02x p%02x ep%d",
                         intf->intf_number, intf->alt_setting,
                         intf->intf_class, intf->intf_subclass,
                         intf->intf_protocol, intf->num_endpoints);
                draw_string(bx + 80, sy + 104, ifline, 0x444444);
            }
        }
    }
}

static void render_welcome(int bx, int by, int bw, int bh, int nx, int ny, int btn_hover) {
    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;

    /* Modern atmospheric gradient backdrop */
    draw_gradient_v(0, 0, w, h / 2, 0x070D19, 0x0F1F38);
    draw_gradient_v(0, h / 2, w, h - h / 2, 0x0F1F38, 0x163254);

    /* Subtle ambient glow spheres */
    draw_circle_filled(w / 4, h / 4, 220, gfx_rgba(14, 165, 233, 18));
    draw_circle_filled((w * 3) / 4, (h * 3) / 4, 240, gfx_rgba(99, 102, 241, 16));

    /* Top system strip */
    draw_rect_alpha(0, 0, w, 34, gfx_rgba(7, 13, 25, 210));
    draw_rect_alpha(0, 34, w, 1, gfx_rgba(148, 163, 184, 40));
    draw_string(18, 13, "MyOS 2.0  |  SDL2 Graphics Subsystem", 0xCBD5E1);
    char res_buf[48];
    snprintf(res_buf, sizeof(res_buf), "%ux%u @ %ubpp", framebuffer_width, framebuffer_height, framebuffer_bpp);
    draw_string(w - 176, 13, res_buf, 0x94A3B8);

    /* Glassmorphic welcome card */
    draw_shadow(bx, by, bw, bh, 18, 14, 120);
    draw_rounded_rect(bx, by, bw, bh, 18, gfx_rgba(15, 23, 42, 238));
    draw_rounded_outline(bx, by, bw, bh, 18, 1, gfx_rgba(56, 189, 248, 90));

    /* Header banner inside card */
    draw_gradient_v(bx + 2, by + 2, bw - 4, 58, 0x163557, 0x0F172A);
    draw_rect_alpha(bx + 2, by + 60, bw - 4, 1, gfx_rgba(56, 189, 248, 80));

    /* 4-tile OS emblem */
    draw_rect(bx + 24, by + 18, 10, 10, 0xFACC15);
    draw_rect(bx + 36, by + 18, 10, 10, 0x4ADE80);
    draw_rect(bx + 24, by + 30, 10, 10, 0x38BDF8);
    draw_rect(bx + 36, by + 30, 10, 10, 0xF472B6);
    draw_string(bx + 58, by + 26, "NORTH BENGAL UNIVERSITY  |  WORKSTATION", 0xE2E8F0);

    const char *line1 = "MyOS Professional";
    int l1x = bx + (bw - string_px_hd(line1)) / 2;
    draw_string_hd(l1x + 1, by + 76 + 1, line1, 0x070D19);
    draw_string_hd(l1x,     by + 76,     line1, 0xF8FAFC);

    const char *sub = "i686 Protected Mode  *  SDL2 Double-Buffered Engine";
    int subx = bx + (bw - (int)strlen(sub) * 8) / 2;
    draw_string(subx, by + 122, sub, 0x38BDF8);

    draw_rect_alpha(bx + 36, by + 146, bw - 72, 1, gfx_rgba(148, 163, 184, 55));

    /* Hardware & Network Status Box */
    draw_rounded_rect(bx + 36, by + 162, bw - 72, 134, 10, 0xFFF8FAFC);
    draw_welcome_status(bx, by + 14, bw, bh);

    /* Primary CTA button */
    u32 btn_col = btn_hover ? 0xFF0EA5E9 : 0xFF0284C7;
    draw_rounded_rect(nx, ny, 168, 42, 10, btn_col);
    draw_rounded_outline(nx, ny, 168, 42, 10, 1, gfx_rgba(255, 255, 255, btn_hover ? 140 : 65));
    draw_string(nx + 24, ny + 17, "Enter Desktop ->", 0xFFFFFF);
}

static int show_welcome(void){
    int bw = 660, bh = 382;
    int bx = ((int)framebuffer_width - bw) / 2;
    int by = ((int)framebuffer_height - bh) / 2;

    int nx = bx + bw - 36 - 168;
    int ny = by + bh - 56;
    int last_status = -1;
    int last_usb_count = -1;
    int last_hover = -1;
    int need_redraw = 1;
    int drawn_cx = cur_x, drawn_cy = cur_y;

    gfx_set_deferred(1);
    cur_saved = 0;

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        int dx, dy; unsigned char btn;
        int clicked = 0;
        while(mouse_read_packet(&dx,&dy,&btn)){
            cur_x = clampi(cur_x + dx, 0, (int)framebuffer_width - 1);
            cur_y = clampi(cur_y + dy, 0, (int)framebuffer_height - 1);
            if(btn & 1) clicked = 1;
        }

        if(clicked && cur_x >= nx && cur_x <= nx + 168 && cur_y >= ny && cur_y <= ny + 42){
            gfx_set_deferred(0);
            return 1;
        }

        int cur_status = welcome_status_code();
        int cur_usb_count = usb_get_device_count();
        int btn_hover = (cur_x >= nx && cur_x <= nx + 168 && cur_y >= ny && cur_y <= ny + 42);

        if (cur_status != last_status || cur_usb_count != last_usb_count || btn_hover != last_hover) {
            last_status = cur_status;
            last_usb_count = cur_usb_count;
            last_hover = btn_hover;
            need_redraw = 1;
        }

        if (need_redraw) {
            render_welcome(bx, by, bw, bh, nx, ny, btn_hover);
            gfx_present();
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
            need_redraw = 0;
        } else if (cur_x != drawn_cx || cur_y != drawn_cy) {
            gfx_present_rect(drawn_cx, drawn_cy, CUR_W, CUR_H);
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
        }
    }
}

/* ===========================================================
DESKTOP + TASKBAR
Returns: app id (1..11)
===========================================================*/
typedef struct {
    const char *title;
    const char *sub;
    int app_id;
    u32 accent;
} desktop_tile_t;

static const desktop_tile_t desktop_tiles[] = {
    { "Calculator",   "Math",     1,  0x38BDF8 },
    { "Browser",      "HTTP",     2,  0x60A5FA },
    { "JSON Viewer",  "API",      3,  0x818CF8 },
    { "Text Editor",  "Notes",    4,  0x34D399 },
    { "SDL2 Studio",  "Graphics", 11, 0xF472B6 },
    { "Grocery Shop", "POS",      9,  0xA78BFA },
    { "Billing POS",  "API",      13, 0xF59E0B },
    { "System Info",  "Hardware", 10, 0x94A3B8 },
    { "Task Manager", "Sched",    12, 0x22D3EE }
};
#define DESKTOP_TILE_COUNT ((int)(sizeof(desktop_tiles) / sizeof(desktop_tiles[0])))

static int desktop_net_state_code(void) {
    if (!g_active_netif) return 0;
    if (dhcp_is_bound() && g_active_netif->ip != 0) return 3;
    if (rndis_active || ipheth_active || cdc_ecm_active) return 2;
    if (g_active_netif->ip != 0) return 4;
    return 1;
}

static void draw_desktop_network_status(void) {
    int w = (int)framebuffer_width;
    int pw = 320, ph = 200;
    int px = w - pw - 28;
    int py = 56;

    /* Top bar live status pill */
    int state = desktop_net_state_code();
    u32 dot_col = (state >= 3) ? 0x22C55E : ((state >= 1) ? 0xF59E0B : 0xEF4444);
    const char *usb_mode = ipheth_active ? "iPhone USB" : (cdc_ecm_active ? "CDC-ECM USB" : "RNDIS USB");
    const char *pill_txt = (state == 3) ? "DHCP Online" :
                           (state == 4) ? "Static IP" :
                           (state == 2) ? usb_mode :
                           (state == 1) ? "NIC No IP" : "Offline";
    draw_rounded_rect(w - 150, 6, 136, 22, 11, gfx_rgba(30, 41, 59, 220));
    draw_circle_filled(w - 136, 17, 4, dot_col | 0xFF000000u);
    draw_string(w - 124, 13, pill_txt, 0xE2E8F0);

    /* Telemetry Glass Card */
    draw_shadow(px, py, pw, ph, 14, 8, 95);
    draw_rounded_rect(px, py, pw, ph, 14, gfx_rgba(15, 23, 42, 225));
    draw_rounded_outline(px, py, pw, ph, 14, 1, gfx_rgba(148, 163, 184, 65));

    draw_string(px + 20, py + 18, "System Telemetry", 0xF8FAFC);
    draw_rect_alpha(px + 20, py + 36, pw - 40, 1, gfx_rgba(56, 189, 248, 80));

    char line[64];
    snprintf(line, sizeof(line), "Display:  %ux%u 32bpp", framebuffer_width, framebuffer_height);
    draw_string(px + 20, py + 50, line, 0xCBD5E1);

    draw_string(px + 20, py + 72, "Graphics: SDL2 Double-Buffer", 0x38BDF8);

    snprintf(line, sizeof(line), "USB Dev:  %d connected", usb_get_device_count());
    draw_string(px + 20, py + 94, line, 0xCBD5E1);

    draw_string(px + 20, py + 118, "Network:", 0x94A3B8);
    if (state == 3) {
        u32 ip = g_active_netif->ip;
        snprintf(line, sizeof(line), "IP: %u.%u.%u.%u",
                 (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        draw_string(px + 92, py + 118, line, 0x4ADE80);
    } else if (state == 2) {
        draw_string(px + 92, py + 118, usb_mode, 0xFBBF24);
    } else if (state == 4) {
        draw_string(px + 92, py + 118, "NIC: Static IP ready", 0x4ADE80);
    } else if (state == 1) {
        draw_string(px + 92, py + 118, "NIC: Ready (no IP)", 0xFBBF24);
    } else {
        draw_string(px + 92, py + 118, "NIC: NOT READY", 0xF87171);
    }

    draw_rounded_rect(px + 20, py + 150, pw - 40, 32, 8, gfx_rgba(30, 41, 59, 190));
    draw_string(px + 32, py + 162, "Click a tile or Start to launch", 0x94A3B8);
}

static void draw_desktop_tiles(int hover_tile) {
    int start_x = 32;
    int start_y = 60;
    int tw = 148, th = 96;
    int gap_x = 18, gap_y = 18;

    for (int i = 0; i < DESKTOP_TILE_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;
        int tx = start_x + col * (tw + gap_x);
        int ty = start_y + row * (th + gap_y);
        int is_hover = (i == hover_tile);

        u32 bg = is_hover ? gfx_rgba(30, 58, 95, 235) : gfx_rgba(15, 23, 42, 205);
        u32 border = is_hover ? gfx_rgba(56, 189, 248, 190) : gfx_rgba(148, 163, 184, 55);
        draw_rounded_rect(tx, ty, tw, th, 12, bg);
        draw_rounded_outline(tx, ty, tw, th, 12, 1, border);

        /* Icon badge */
        draw_rounded_rect(tx + 14, ty + 14, 34, 34, 9, desktop_tiles[i].accent | 0xFF000000u);
        draw_rect_alpha(tx + 21, ty + 21, 20, 20, gfx_rgba(255, 255, 255, 55));

        draw_string(tx + 14, ty + 58, desktop_tiles[i].title, 0xF8FAFC);
        draw_string(tx + 14, ty + 74, desktop_tiles[i].sub, 0x94A3B8);
    }
}

static int show_desktop(void){
    const int bar_h = 52;
    const int sb_x = 12, sb_y = (int)framebuffer_height - bar_h + 7, sb_w = 114, sb_h = 38;

    const int sm_x = 12, sm_w = 276, sm_h = 482;
    int sm_y = (int)framebuffer_height - bar_h - sm_h - 10;
    if (sm_y < 42) sm_y = 42;

    int start_open = 0;
    int last_hover_menu = -1;
    int last_hover_tile = -1;
    int last_net_state = -1;
    int last_usb_count = -1;
    int need_redraw = 1;
    int drawn_cx = cur_x, drawn_cy = cur_y;
    int prev_btn = 0;

    gfx_set_deferred(1);
    cur_saved = 0;

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char kch;
        while (keyboard_read_char(&kch)) {
            if (wm_handle_key(kch)) {
                need_redraw = 1;
            }
        }

        int dx, dy; unsigned char btn;
        int cur_btn = prev_btn;
        int pressed = 0, released = 0;
        while(mouse_read_packet(&dx,&dy,&btn)){
            cur_x = clampi(cur_x + dx, 0, (int)framebuffer_width - 1);
            cur_y = clampi(cur_y + dy, 0, (int)framebuffer_height - 1);
            int down = (btn & 1) ? 1 : 0;
            if (down && !cur_btn) pressed = 1;
            if (!down && cur_btn) released = 1;
            cur_btn = down;
        }
        prev_btn = cur_btn;
        int held = cur_btn;

        if (wm_tick()) {
            need_redraw = 1;
        }

        int cur_net_state = desktop_net_state_code();
        int cur_usb_count = usb_get_device_count();
        if (cur_net_state != last_net_state || cur_usb_count != last_usb_count) {
            last_net_state = cur_net_state;
            last_usb_count = cur_usb_count;
            need_redraw = 1;
        }

        int hover_menu = -1;
        if (start_open && cur_x > sm_x + 10 && cur_x < sm_x + sm_w - 10) {
            for (int i = 0; i < START_ITEM_COUNT; i++) {
                int iy = sm_y + 48 + i * 30;
                if (cur_y >= iy && cur_y <= iy + 26) {
                    hover_menu = i;
                    break;
                }
            }
        }

        int hover_tile = -1;
        if (!start_open && !wm_is_dragging()) {
            int start_x = 32, start_y = 60, tw = 148, th = 96, gap_x = 18, gap_y = 18;
            for (int i = 0; i < DESKTOP_TILE_COUNT; i++) {
                int tx = start_x + (i % 2) * (tw + gap_x);
                int ty = start_y + (i / 2) * (th + gap_y);
                if (cur_x >= tx && cur_x <= tx + tw && cur_y >= ty && cur_y <= ty + th) {
                    hover_tile = i;
                    break;
                }
            }
        }

        if (pressed || held || released) {
            if (start_open && pressed) {
                if(cur_x > sb_x && cur_x < sb_x + sb_w && cur_y > sb_y && cur_y < sb_y + sb_h){
                    start_open = 0;
                    need_redraw = 1;
                } else if (hover_menu >= 0 && hover_menu < START_ITEM_COUNT) {
                    int app_id = start_items[hover_menu].app_id;
                    start_open = 0;
                    if (app_id == 99) {
                        gfx_set_deferred(0);
                        if (get_hello_ptr && get_hello_len && get_hello_len() > 0) {
                            console_puts("Running embedded program...\n");
                            int rc = elf32_load_and_run((const void*)get_hello_ptr(), (size_t)get_hello_len());
                            char numbuf[16]; int n=0; int t = rc;
                            if(t==0) numbuf[n++]='0';
                            else {
                                if(t<0){ numbuf[n++]='-'; t=-t; }
                                int st=0; int tmp=t;
                                while(tmp>0){ numbuf[n+st++] = '0' + (tmp%10); tmp/=10; }
                                for(int i=0;i<st/2;i++){
                                    char c=n+i; char d=n+st-1-i; char tmpc = numbuf[c]; numbuf[c]=numbuf[d]; numbuf[d]=tmpc;
                                }
                                n += st;
                            }
                            numbuf[n]=0;
                            console_puts("User program exited with code: ");
                            console_puts(numbuf);
                            console_puts("\n");
                        } else {
                            console_puts("No embedded program present. Run make userprog and embed-userprog first.\n");
                        }
                        gfx_set_deferred(1);
                        need_redraw = 1;
                    } else if (wm_open(app_id)) {
                        need_redraw = 1;
                    } else {
                        gfx_set_deferred(0);
                        return app_id;
                    }
                } else if(!(cur_x > sm_x && cur_x < sm_x + sm_w && cur_y > sm_y && cur_y < sm_y + sm_h)){
                    start_open = 0;
                    need_redraw = 1;
                }
            } else if (wm_handle_mouse(cur_x, cur_y, pressed, held, released, bar_h)) {
                need_redraw = 1;
            } else if (pressed) {
                if(cur_x > sb_x && cur_x < sb_x + sb_w && cur_y > sb_y && cur_y < sb_y + sb_h){
                    start_open = !start_open;
                    need_redraw = 1;
                } else if (hover_tile >= 0 && hover_tile < DESKTOP_TILE_COUNT) {
                    int app_id = desktop_tiles[hover_tile].app_id;
                    if (wm_open(app_id)) {
                        need_redraw = 1;
                    } else {
                        gfx_set_deferred(0);
                        return app_id;
                    }
                }
            }
        }

        if (hover_menu != last_hover_menu || hover_tile != last_hover_tile) {
            last_hover_menu = hover_menu;
            last_hover_tile = hover_tile;
            need_redraw = 1;
        }

        if (need_redraw) {
            draw_desktop_background();
            draw_desktop_tiles(hover_tile);
            draw_desktop_network_status();
            wm_draw_all();
            draw_taskbar(bar_h, sb_x, sb_y, sb_w, sb_h);
            wm_draw_taskbar_tabs((int)framebuffer_height - bar_h, bar_h, sb_x + sb_w + 14);
            if (start_open) {
                draw_start_menu(sm_x, sm_y, sm_w, sm_h, hover_menu);
            }
            gfx_present();
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
            need_redraw = 0;
        } else if (cur_x != drawn_cx || cur_y != drawn_cy) {
            gfx_present_rect(drawn_cx, drawn_cy, CUR_W, CUR_H);
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
        }
    }
}

/* ===========================================================
CALCULATOR
===========================================================*/
#define BW 60
#define BH 40
static char expr[64]={0}; static int expr_len=0;

static void calc_add(char c){ if(expr_len<63){expr[expr_len++]=c; expr[expr_len]=0;}}
static void calc_clear(void){ expr_len=0; expr[0]=0; }
static void calc_eval(void){
    int res=0; char op='+';
    for(int i=0;i<expr_len;i++){
        int v=0;
        while(i<expr_len && expr[i]>='0' && expr[i]<='9'){ v=v*10+(expr[i]-'0'); i++; }
        if(op=='+')res+=v;
        else if(op=='-')res-=v;
        else if(op=='*')res*=v;
        else if(op=='/')res/= (v? v:1);
        if(i<expr_len){ op=expr[i]; i++; }
    }
    calc_clear();
    // write result back
    char buf[20]; int n=0, t=res;
    if(t==0) buf[n++]='0';
    int neg=0; if(t<0){ neg=1; t=-t; }
    while(t>0){ buf[n++]=(t%10)+'0'; t/=10; }
    if(neg) buf[n++]='-';
    while(n--) calc_add(buf[n]);
}

static void draw_key(int x,int y,char c){
    draw_rect(x,y,BW,BH,0xAAAAAA);
    char s[2]={c,0};
    draw_string(x+25,y+13,s,0x000000);
}

static void calculator_ui(void){
    // redraw desktop bg behind window (no animation)
    draw_rect(0,0,framebuffer_width,framebuffer_height,0x87CEEB);

    int winw=300, winh=350;
    int wx=(framebuffer_width-winw)/2;
    int wy=(framebuffer_height-winh)/2;

    draw_rounded(wx,wy,winw,winh,15,0xBBBBBB);
    draw_rect(wx+10,wy+10,winw-20,60,0xFFFFFF);

    // close button
    int cx=wx+winw-30, cy=wy+10, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xFF0000);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    // keys
    const char *keys="789/456*123-0.=+C";
    int idx=0;
    for(int r=0;r<4;r++){
        for(int c=0;c<4;c++){
            char k=keys[idx++];
            int bx=wx+10+c*(BW+5);
            int by=wy+80+r*(BH+5);
            draw_key(bx,by,k);
        }
    }

    // show cursor at current pos
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        // refresh display area (expression)
        draw_rect(wx+12,wy+12,winw-24,56,0xFFFFFF);
        draw_string(wx+20,wy+32,expr,0x000000);

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx, cur_y+dy);

            if(btn & 1){
                // close
                if(cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch) {
                    cursor_restore_under();
                    return;
                }
                // keys
                idx=0;
                for(int rr=0;rr<4;rr++){
                    for(int cc=0;cc<4;cc++){
                        char k=keys[idx++];
                        int bx=wx+10+cc*(BW+5);
                        int by=wy+80+rr*(BH+5);
                        if(cur_x>bx && cur_x<bx+BW && cur_y>by && cur_y<by+BH){
                            if(k=='C') calc_clear();
                            else if(k=='=') calc_eval();
                            else            calc_add(k);
                        }
                    }
                }
            }
        }
    }
}

/* ===========================================================
BROWSER (layout window) with close - updated to fetch HTTP body
===========================================================*/

// very basic parser for http://host:port/path
static int parse_url(const char *url, char *host_buf, int host_buf_size, const char **path, uint16_t *port) {
    if (strncmp(url, "http://", 7) != 0) return 0;
    const char *host_start = url + 7;
    const char *p = host_start;
    while (*p && *p != ':' && *p != '/') p++;

    int host_len = p - host_start;
    if (host_len >= host_buf_size) return 0; // host too long
    memcpy(host_buf, host_start, host_len);
    host_buf[host_len] = 0;

    *port = 80;
    if (*p == ':') {
        *port = 0;
        p++;
        while (*p && *p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    if (*p == '/') {
        *path = p;
    } else {
        *path = "/";
    }
    return 1;
}


typedef struct {
    const char *url;
    char *bodybuf;
    int bodybuf_len;
    volatile int done;
    volatile int cancel;
    int try_tls;
    int dns_ok;
    uint32_t resolved_ip;
    int got;
} http_fetch_job_t;

static void async_http_fetch_worker(void *arg) {
    http_fetch_job_t *job = (http_fetch_job_t *)arg;
    char host[128];
    const char *path;
    uint16_t port;
    int parsed_ok = parse_url(job->url, host, sizeof(host), &path, &port);

    job->resolved_ip = 0;
    job->dns_ok = 0;
    job->got = 0;

    /* Wait briefly for DHCP/IPv4 if link is still coming up */
    for (int w = 0; w < 5000 && !net_has_ipv4() && !job->cancel; w++) {
        net_poll();
    }

    if (parsed_ok && !job->cancel) {
        job->dns_ok = dns_resolve(host, &job->resolved_ip);
    }

    if (job->dns_ok && !job->cancel) {
        char host_hdr[160];
        if (port == 80 || port == 443) {
            snprintf(host_hdr, sizeof(host_hdr), "%s", host);
        } else {
            snprintf(host_hdr, sizeof(host_hdr), "%s:%u", host, (unsigned)port);
        }
        job->got = http_get_by_ip_port(job->resolved_ip, port, host_hdr, path,
                                       job->bodybuf, job->bodybuf_len);
    }

    if (job->try_tls && job->got <= 0 && job->resolved_ip != 0 && !job->cancel) {
        int tgot = tls_http_get_by_ip(job->resolved_ip, host, path,
                                      job->bodybuf, job->bodybuf_len);
        if (tgot > 0) job->got = tgot;
    }

    job->done = 1;
}

static void browser_ui(void){
    draw_desktop_background();

    const int ww=660, wh=440;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;

    draw_window_chrome(wx, wy, ww, wh, "MyOS Browser");

    const int cx=wx+ww-28, cy=wy+6, cw=22, ch=22;
    draw_rounded_rect(cx,cy,cw,ch,5,0xFFDC2626);
    draw_string(cx+7,cy+7,"X",0xFFFFFF);

    const int ab_x=wx+12, ab_y=wy+38, ab_w=ww-24, ab_h=26;
    draw_rounded_rect(ab_x,ab_y,ab_w,ab_h,6,0xFFE2E8F0);
    const char *url = "http://jsonplaceholder.typicode.com/posts/2";
    draw_string(ab_x+10,ab_y+9,url,0x0F172A);

    const int ct_x=wx+12, ct_y=ab_y+ab_h+8, ct_w=ww-24, ct_h=wh - (38+ab_h+8+14);
    draw_rect(ct_x,ct_y,ct_w,ct_h,0xFFFFFF);
    draw_string(ct_x+10,ct_y+12,"Fetching in background worker (cursor stays responsive)...",0x0284C7);

    static char bodybuf[8192];
    static http_fetch_job_t bjob;
    bjob.url = url;
    bjob.bodybuf = bodybuf;
    bjob.bodybuf_len = sizeof(bodybuf);
    bjob.done = 0;
    bjob.cancel = 0;
    bjob.try_tls = 1;
    bjob.dns_ok = 0;
    bjob.resolved_ip = 0;
    bjob.got = 0;
    worker_submit("browser-http", async_http_fetch_worker, &bjob);

    cursor_move_to(cur_x,cur_y);

    int rendered_result = 0;
    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();
        sched_yield();

        if (bjob.done && !rendered_result) {
            rendered_result = 1;
            cursor_restore_under();
            draw_rect(ct_x,ct_y,ct_w,ct_h,0xFFFFFF);
            if (bjob.got > 0) {
                int max_lines = (ct_h - 16) / 14;
                int line = 0;
                char* p = bodybuf;
                while (line < max_lines && *p) {
                    char tmp[128]; int ti = 0;
                    while (*p && *p != '\n' && ti < (int)sizeof(tmp)-1) tmp[ti++] = *p++;
                    tmp[ti] = '\0';
                    if (*p == '\n') p++;
                    int start = 0; while (tmp[start] == ' ' && start < ti) start++;
                    draw_string(ct_x+10, ct_y+10 + line*14, tmp + start, 0x0F172A);
                    line++;
                }
                if (*p) draw_string(ct_x+10, ct_y + ct_h - 18, "...(truncated)", 0x64748B);
            } else {
                char dbg_buf[128];
                snprintf(dbg_buf, sizeof(dbg_buf), "Fetch failed! dns_ok=%d, ip=%u, got=%d",
                         bjob.dns_ok, bjob.resolved_ip, bjob.got);
                draw_string(ct_x+10, ct_y+12, dbg_buf, 0xDC2626);
            }
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        int moved = 0, clicked = 0;
        while(mouse_read_packet(&dx,&dy,&btn)){
            cur_x = clampi(cur_x + dx, 0, (int)framebuffer_width - 1);
            cur_y = clampi(cur_y + dy, 0, (int)framebuffer_height - 1);
            moved = 1;
            if(btn & 1) clicked = 1;
        }
        if (moved) {
            cursor_move_to(cur_x, cur_y);
        }
        if(clicked && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
            bjob.cancel = 1;
            cursor_restore_under();
            return;
        }
    }
}

/* ===========================================================
JSON VIEWER
===========================================================*/
static void json_viewer_ui(void){
    draw_desktop_background();

    const int ww=660, wh=440;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;

    draw_window_chrome(wx, wy, ww, wh, "JSON Viewer");

    const int cx=wx+ww-28, cy=wy+6, cw=22, ch=22;
    draw_rounded_rect(cx,cy,cw,ch,5,0xFFDC2626);
    draw_string(cx+7,cy+7,"X",0xFFFFFF);

    const int ct_x=wx+12, ct_y=wy+38, ct_w=ww-24, ct_h=wh - 52;
    draw_rect(ct_x,ct_y,ct_w,ct_h,0xFFFFFF);
    draw_string(ct_x+10,ct_y+12,"Fetching JSON in background worker...",0x0284C7);

    static char bodybuf[8192];
    static http_fetch_job_t jjob;
    jjob.url = "http://jsonplaceholder.typicode.com/users/1";
    jjob.bodybuf = bodybuf;
    jjob.bodybuf_len = sizeof(bodybuf);
    jjob.done = 0;
    jjob.cancel = 0;
    jjob.try_tls = 0;
    jjob.dns_ok = 0;
    jjob.resolved_ip = 0;
    jjob.got = 0;
    worker_submit("json-http", async_http_fetch_worker, &jjob);

    cursor_move_to(cur_x,cur_y);

    int rendered_result = 0;
    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();
        sched_yield();

        if (jjob.done && !rendered_result) {
            rendered_result = 1;
            cursor_restore_under();
            draw_rect(ct_x,ct_y,ct_w,ct_h,0xFFFFFF);
            if (jjob.got > 0) {
                jsmn_parser p;
                jsmntok_t t[128];
                jsmn_init(&p);
                int r = jsmn_parse(&p, bodybuf, strlen(bodybuf), t, 128);
                if (r < 0) {
                    draw_string(ct_x+10, ct_y+12, "Failed to parse JSON", 0xDC2626);
                } else {
                    int line = 0;
                    int max_lines = (ct_h - 16) / 14;
                    for (int i = 1; i < r; i++) {
                        if (t[i].type == JSMN_STRING && t[i-1].type == JSMN_STRING) {
                            char key[64], val[64], line_buf[128];
                            int key_len = t[i-1].end - t[i-1].start;
                            int val_len = t[i].end - t[i].start;
                            if (key_len > 63) key_len = 63;
                            if (val_len > 63) val_len = 63;
                            memcpy(key, bodybuf + t[i-1].start, key_len);
                            key[key_len] = 0;
                            memcpy(val, bodybuf + t[i].start, val_len);
                            val[val_len] = 0;
                            snprintf(line_buf, sizeof(line_buf), "%s: %s", key, val);
                            draw_string(ct_x+10, ct_y+10 + line*14, line_buf, 0x0F172A);
                            line++;
                            if (line >= max_lines) break;
                        }
                    }
                }
            } else {
                char dbg_buf[128];
                snprintf(dbg_buf, sizeof(dbg_buf), "Fetch failed! dns_ok=%d, ip=%u, got=%d",
                         jjob.dns_ok, jjob.resolved_ip, jjob.got);
                draw_string(ct_x+10, ct_y+12, dbg_buf, 0xDC2626);
            }
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        int moved = 0, clicked = 0;
        while(mouse_read_packet(&dx,&dy,&btn)){
            cur_x = clampi(cur_x + dx, 0, (int)framebuffer_width - 1);
            cur_y = clampi(cur_y + dy, 0, (int)framebuffer_height - 1);
            moved = 1;
            if(btn & 1) clicked = 1;
        }
        if (moved) {
            cursor_move_to(cur_x, cur_y);
        }
        if(clicked && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
            jjob.cancel = 1;
            cursor_restore_under();
            return;
        }
    }
}

/* ===========================================================
TEXT EDITOR
===========================================================*/
static char editor_text[1024] = "North Bengal University Project";
static char editor_saved_text[1024] = "";
static int editor_len = 31;
static int editor_saved_len = 0;
static int editor_dirty = 0;
static char editor_status[64] = "Type with keyboard, then Save";

static void editor_append(const char *s){
    while(*s && editor_len < (int)sizeof(editor_text)-1){
        editor_text[editor_len++] = *s++;
    }
    editor_text[editor_len] = 0;
    editor_dirty = 1;
}

static void editor_append_char(char c){
    if(editor_len < (int)sizeof(editor_text)-1){
        editor_text[editor_len++] = c;
        editor_text[editor_len] = 0;
        editor_dirty = 1;
    }
}

static void editor_backspace(void){
    if(editor_len > 0){
        editor_len--;
        editor_text[editor_len] = 0;
        editor_dirty = 1;
    }
}

static void editor_clear(void){
    editor_len = 0;
    editor_text[0] = 0;
    editor_dirty = 1;
}

static void editor_save(void){
    memcpy(editor_saved_text, editor_text, editor_len + 1);
    editor_saved_len = editor_len;
    editor_dirty = 0;
    snprintf(editor_status, sizeof(editor_status), "Saved %d chars to memory", editor_saved_len);
}

static void editor_load(void){
    memcpy(editor_text, editor_saved_text, editor_saved_len + 1);
    editor_len = editor_saved_len;
    editor_dirty = 0;
    snprintf(editor_status, sizeof(editor_status), "Loaded saved note");
}

static void draw_editor_content(int x,int y,int w,int h){
    draw_rect(x,y,w,h,0xFFFFFF);
    draw_string(x+10,y+10,"Real keyboard text editor",0x145DA0);
    draw_rect(x+10,y+32,w-20,h-82,0xF7FBFD);

    int px = x + 20;
    int py = y + 48;
    int col = 0;
    int line = 0;
    char one[2] = {0,0};
    for(int i=0; editor_text[i] && line < 13; i++){
        if(editor_text[i] == '\n' || col >= 66){
            line++;
            col = 0;
            if(editor_text[i] == '\n') continue;
        }
        one[0] = editor_text[i];
        draw_string(px + col*8, py + line*12, one, 0x0B2437);
        col++;
    }
    if(line < 13){
        draw_rect(px + col*8, py + line*12 + 9, 8, 2, 0x145DA0);
    }

    char count[80];
    snprintf(count,sizeof(count),"%s | chars: %d%s",editor_status,editor_len,editor_dirty ? " *" : "");
    draw_string(x+10,y+h-38,count,0x3D566E);
}

static void text_editor_ui(void){
    draw_desktop_background();

    const int ww=620, wh=380;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"NBU Text Editor");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    const int ct_x=wx+14, ct_y=wy+44, ct_w=ww-28, ct_h=250;
    draw_editor_content(ct_x,ct_y,ct_w,ct_h);

    const char *labels[] = {"Save","Load","Back","Clear","NBU"};
    const int count = 5;
    const int bw = 82, bh = 32, gap = 10;
    const int by = wy + wh - 60;
    for(int i=0;i<count;i++){
        int bx = wx + 20 + i*(bw+gap);
        u32 col = (i == 0) ? 0xDDF3E4 : ((i >= 2 && i <= 3) ? 0xF1D5D8 : 0xDCEFF8);
        draw_rounded(bx,by,bw,bh,8,col);
        draw_string(bx+10,by+12,labels[i],0x0B2437);
    }

    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char typed;
        if(keyboard_read_char(&typed)){
            cursor_restore_under();
            if(typed == '\b') editor_backspace();
            else if(typed == '\t') editor_append("    ");
            else editor_append_char(typed);
            snprintf(editor_status, sizeof(editor_status), "Editing");
            draw_editor_content(ct_x,ct_y,ct_w,ct_h);
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx, cur_y+dy);

            if(btn & 1){
                if(cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                    cursor_restore_under();
                    return;
                }
                for(int i=0;i<count;i++){
                    int bx = wx + 20 + i*(bw+gap);
                    if(cur_x>bx && cur_x<bx+bw && cur_y>by && cur_y<by+bh){
                        cursor_restore_under();
                        if(i == 0) editor_save();
                        else if(i == 1) editor_load();
                        else if(i == 2){ editor_backspace(); snprintf(editor_status, sizeof(editor_status), "Editing"); }
                        else if(i == 3){ editor_clear(); snprintf(editor_status, sizeof(editor_status), "Cleared"); }
                        else if(i == 4){ editor_append("North Bengal University "); snprintf(editor_status, sizeof(editor_status), "Editing"); }
                        else editor_append(labels[i]);
                        draw_editor_content(ct_x,ct_y,ct_w,ct_h);
                        cursor_move_to(cur_x,cur_y);
                    }
                }
            }
        }
    }
}

/* ===========================================================
CLICK GAME
===========================================================*/
static void draw_game_screen(int wx,int wy,int ww,int wh,int tx,int ty,int score){
    draw_rect(wx+14,wy+44,ww-28,wh-58,0xF7FBFD);
    draw_string(wx+24,wy+56,"Click the blue target",0x0B3D5C);
    char score_buf[32];
    snprintf(score_buf,sizeof(score_buf),"Score: %d",score);
    draw_string(wx+ww-118,wy+56,score_buf,0x185B2A);
    draw_rounded(tx,ty,42,42,18,0x2F80ED);
    draw_rect(tx+16,ty+16,10,10,0xFFFFFF);
}

static void click_game_ui(void){
    draw_desktop_background();

    const int ww=520, wh=360;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"Click Game");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    int score = 0;
    int target_idx = 0;
    int tx = wx + 80;
    int ty = wy + 118;
    draw_game_screen(wx,wy,ww,wh,tx,ty,score);
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx, cur_y+dy);

            if(btn & 1){
                if(cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                    cursor_restore_under();
                    return;
                }
                if(cur_x>tx && cur_x<tx+42 && cur_y>ty && cur_y<ty+42){
                    score++;
                    target_idx++;
                    tx = wx + 50 + ((target_idx * 83) % (ww - 120));
                    ty = wy + 92 + ((target_idx * 47) % (wh - 150));
                    cursor_restore_under();
                    draw_game_screen(wx,wy,ww,wh,tx,ty,score);
                    cursor_move_to(cur_x,cur_y);
                }
            }
        }
    }
}

/* ===========================================================
HIGH-LOW CARD GAME
===========================================================*/
static const char *card_rank_name(int rank){
    switch(rank){
        case 1: return "A";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default: break;
    }
    static char buf[3];
    if(rank == 10){
        buf[0] = '1'; buf[1] = '0'; buf[2] = 0;
    }else{
        buf[0] = '0' + rank; buf[1] = 0;
    }
    return buf;
}

static int next_card_rank(int *seed){
    *seed = (*seed * 37 + 17) & 0x7fff;
    return (*seed % 13) + 1;
}

static void draw_playing_card(int x,int y,int rank,u32 accent){
    draw_rounded(x,y,96,132,10,0x1E2933);
    draw_rect(x+4,y+4,88,124,0xFFFFFF);
    draw_string(x+12,y+14,card_rank_name(rank),accent);
    draw_string(x+12,y+32,"NBU",0x9AA7B2);
    draw_rect(x+26,y+56,44,34,accent);
    draw_string(x+34,y+68,card_rank_name(rank),0xFFFFFF);
    draw_string(x+58,y+108,card_rank_name(rank),accent);
}

static void draw_high_low_game(int wx,int wy,int ww,int wh,int current,int next,int show_next,int score,int best,const char *status){
    draw_rect(wx+14,wy+44,ww-28,wh-58,0xF7FBFD);
    draw_string(wx+24,wy+58,"High-Low Cards",0x0B3D5C);
    draw_string(wx+24,wy+80,"H higher  L lower  N next card  R reset  Q close",0x3D566E);

    char score_buf[64];
    snprintf(score_buf,sizeof(score_buf),"Score: %d   Best: %d",score,best);
    draw_string(wx+24,wy+106,score_buf,0x185B2A);
    draw_string(wx+24,wy+128,status,0x0B2437);

    draw_string(wx+138,wy+160,"Current",0x3D566E);
    draw_playing_card(wx+126,wy+180,current,0xD62839);

    draw_string(wx+346,wy+160,"Next",0x3D566E);
    if(show_next){
        draw_playing_card(wx+330,wy+180,next,0x145DA0);
    }else{
        draw_rounded(wx+330,wy+180,96,132,10,0x1E2933);
        draw_rect(wx+334,wy+184,88,124,0x145DA0);
        draw_string(wx+354,wy+240,"?",0xFFFFFF);
    }
}

static void high_low_card_ui(void){
    draw_desktop_background();

    const int ww=560, wh=410;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"High-Low Cards");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    int seed = (int)(framebuffer_width + framebuffer_height + 97);
    int current = next_card_rank(&seed);
    int next = next_card_rank(&seed);
    int show_next = 0;
    int score = 0;
    int best = 0;
    char status[80] = "Guess whether the next card is higher or lower.";

    draw_high_low_game(wx,wy,ww,wh,current,next,show_next,score,best,status);
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char key;
        if(keyboard_read_char(&key)){
            cursor_restore_under();
            if(key == 'q' || key == 'Q'){
                return;
            }else if(key == 'r' || key == 'R'){
                score = 0;
                current = next_card_rank(&seed);
                next = next_card_rank(&seed);
                show_next = 0;
                snprintf(status,sizeof(status),"New round ready.");
            }else if(key == 'n' || key == 'N'){
                current = next;
                next = next_card_rank(&seed);
                show_next = 0;
                snprintf(status,sizeof(status),"New hidden card ready.");
            }else if(key == 'h' || key == 'H' || key == 'l' || key == 'L'){
                int correct = ((key == 'h' || key == 'H') && next > current) ||
                              ((key == 'l' || key == 'L') && next < current);
                show_next = 1;
                if(next == current){
                    snprintf(status,sizeof(status),"Same rank. Push. Press N.");
                }else if(correct){
                    score++;
                    if(score > best) best = score;
                    snprintf(status,sizeof(status),"Correct. Press N for the next card.");
                }else{
                    score = 0;
                    snprintf(status,sizeof(status),"Wrong guess. Press N or R.");
                }
            }
            draw_high_low_game(wx,wy,ww,wh,current,next,show_next,score,best,status);
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx,cur_y+dy);
            if((btn & 1) && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                cursor_restore_under();
                return;
            }
        }
    }
}

/* ===========================================================
KEYBOARD MAZE
===========================================================*/
static void maze_reset(char maze[8][13], int *px, int *py, int *score, int *won){
    const char *rows[8] = {
        "############",
        "#P..#.....G#",
        "#.#.#.###..#",
        "#.#...#....#",
        "#.#####.##.#",
        "#.....#....#",
        "###.#...#..#",
        "############"
    };
    for(int y=0;y<8;y++){
        for(int x=0;x<13;x++){
            maze[y][x] = rows[y][x];
        }
    }
    *px = 1;
    *py = 1;
    *score = 0;
    *won = 0;
}

static void draw_maze_game(int wx,int wy,int ww,int wh,char maze[8][13],int px,int py,int score,int won){
    draw_rect(wx+14,wy+44,ww-28,wh-58,0xF7FBFD);
    draw_string(wx+24,wy+58,"Keyboard Maze",0x0B3D5C);
    draw_string(wx+24,wy+80,"WASD move  collect dots  reach G  R reset  Q close",0x3D566E);

    char score_buf[64];
    snprintf(score_buf,sizeof(score_buf),"Coins: %d",score);
    draw_string(wx+24,wy+104,score_buf,0x185B2A);
    if(won) draw_string(wx+120,wy+104,"You reached the goal!",0x185B2A);

    int cell = 30;
    int gx = wx + 88;
    int gy = wy + 132;
    for(int y=0;y<8;y++){
        for(int x=0;x<12;x++){
            int sx = gx + x*cell;
            int sy = gy + y*cell;
            char c = maze[y][x];
            u32 col = 0xFFFFFF;
            if(c == '#') col = 0x26323F;
            else if(c == 'G') col = 0xDDF3E4;
            else col = 0xFFFFFF;
            draw_rect(sx,sy,cell-2,cell-2,col);
            if(c == '.'){
                draw_rect(sx+12,sy+12,6,6,0xF2B705);
            }else if(c == 'G'){
                draw_string(sx+10,sy+11,"G",0x185B2A);
            }
        }
    }
    draw_rounded(gx+px*cell+4,gy+py*cell+4,cell-10,cell-10,8,0x2F80ED);
}

static void keyboard_maze_ui(void){
    draw_desktop_background();

    const int ww=560, wh=430;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"Keyboard Maze");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    char maze[8][13];
    int px, py, score, won;
    maze_reset(maze,&px,&py,&score,&won);
    draw_maze_game(wx,wy,ww,wh,maze,px,py,score,won);
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char key;
        if(keyboard_read_char(&key)){
            int nx = px;
            int ny = py;
            if(key == 'q' || key == 'Q'){
                cursor_restore_under();
                return;
            }else if(key == 'r' || key == 'R'){
                maze_reset(maze,&px,&py,&score,&won);
            }else if(!won){
                if(key == 'w' || key == 'W') ny--;
                else if(key == 's' || key == 'S') ny++;
                else if(key == 'a' || key == 'A') nx--;
                else if(key == 'd' || key == 'D') nx++;

                if(nx != px || ny != py){
                    char tile = maze[ny][nx];
                    if(tile != '#'){
                        if(tile == '.'){
                            score++;
                            maze[ny][nx] = ' ';
                        }else if(tile == 'G'){
                            won = 1;
                        }
                        px = nx;
                        py = ny;
                    }
                }
            }
            cursor_restore_under();
            draw_maze_game(wx,wy,ww,wh,maze,px,py,score,won);
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx,cur_y+dy);
            if((btn & 1) && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                cursor_restore_under();
                return;
            }
        }
    }
}

/* ===========================================================
SNAKE GAME
===========================================================*/
#define SNAKE_W 16
#define SNAKE_H 12
#define SNAKE_MAX (SNAKE_W*SNAKE_H)

static void snake_place_food(int sx[], int sy[], int len, int *fx, int *fy, int *seed){
    for(int tries=0; tries<64; tries++){
        *seed = (*seed * 41 + 23) & 0x7fff;
        int x = *seed % SNAKE_W;
        *seed = (*seed * 37 + 19) & 0x7fff;
        int y = *seed % SNAKE_H;
        int hit = 0;
        for(int i=0;i<len;i++){
            if(sx[i] == x && sy[i] == y){
                hit = 1;
                break;
            }
        }
        if(!hit){
            *fx = x;
            *fy = y;
            return;
        }
    }
    *fx = 1;
    *fy = 1;
}

static void snake_reset(int sx[], int sy[], int *len, int *dx, int *dy, int *score, int *game_over, int *fx, int *fy, int *seed){
    *len = 4;
    sx[0] = 7; sy[0] = 6;
    sx[1] = 6; sy[1] = 6;
    sx[2] = 5; sy[2] = 6;
    sx[3] = 4; sy[3] = 6;
    *dx = 1;
    *dy = 0;
    *score = 0;
    *game_over = 0;
    snake_place_food(sx,sy,*len,fx,fy,seed);
}

static void draw_snake_game(int wx,int wy,int ww,int wh,int sx[],int sy[],int len,int fx,int fy,int score,int game_over){
    draw_rect(wx+14,wy+44,ww-28,wh-58,0xF7FBFD);
    draw_string(wx+24,wy+58,"Snake Game",0x0B3D5C);
    draw_string(wx+24,wy+80,"WASD move one step  eat food  R reset  Q close",0x3D566E);
    char score_buf[64];
    snprintf(score_buf,sizeof(score_buf),"Score: %d",score);
    draw_string(wx+24,wy+104,score_buf,0x185B2A);
    if(game_over) draw_string(wx+120,wy+104,"Game over. Press R.",0xB00020);

    int cell = 24;
    int gx = wx + 88;
    int gy = wy + 132;
    draw_rect(gx-4,gy-4,SNAKE_W*cell+8,SNAKE_H*cell+8,0x26323F);
    for(int y=0;y<SNAKE_H;y++){
        for(int x=0;x<SNAKE_W;x++){
            draw_rect(gx+x*cell,gy+y*cell,cell-2,cell-2,0xFFFFFF);
        }
    }
    draw_rounded(gx+fx*cell+5,gy+fy*cell+5,cell-10,cell-10,6,0xD62839);
    for(int i=len-1;i>=0;i--){
        u32 col = (i == 0) ? 0x0E4F7C : 0x45B36B;
        draw_rounded(gx+sx[i]*cell+3,gy+sy[i]*cell+3,cell-8,cell-8,6,col);
    }
}

static void snake_game_ui(void){
    draw_desktop_background();

    const int ww=560, wh=470;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"Snake Game");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    int sx[SNAKE_MAX];
    int sy[SNAKE_MAX];
    int len, dx, dy, score, game_over, fx, fy;
    int seed = (int)(framebuffer_width + framebuffer_height + 313);
    snake_reset(sx,sy,&len,&dx,&dy,&score,&game_over,&fx,&fy,&seed);
    draw_snake_game(wx,wy,ww,wh,sx,sy,len,fx,fy,score,game_over);
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char key;
        if(keyboard_read_char(&key)){
            if(key == 'q' || key == 'Q'){
                cursor_restore_under();
                return;
            }
            if(key == 'r' || key == 'R'){
                snake_reset(sx,sy,&len,&dx,&dy,&score,&game_over,&fx,&fy,&seed);
            }else if(!game_over){
                int ndx = dx;
                int ndy = dy;
                if((key == 'w' || key == 'W') && dy != 1){ ndx = 0; ndy = -1; }
                else if((key == 's' || key == 'S') && dy != -1){ ndx = 0; ndy = 1; }
                else if((key == 'a' || key == 'A') && dx != 1){ ndx = -1; ndy = 0; }
                else if((key == 'd' || key == 'D') && dx != -1){ ndx = 1; ndy = 0; }

                dx = ndx;
                dy = ndy;
                int nx = sx[0] + dx;
                int ny = sy[0] + dy;
                if(nx < 0 || ny < 0 || nx >= SNAKE_W || ny >= SNAKE_H){
                    game_over = 1;
                }else{
                    for(int i=0;i<len;i++){
                        if(sx[i] == nx && sy[i] == ny){
                            game_over = 1;
                            break;
                        }
                    }
                    if(!game_over){
                        int grow = (nx == fx && ny == fy);
                        if(grow && len < SNAKE_MAX) len++;
                        for(int i=len-1;i>0;i--){
                            sx[i] = sx[i-1];
                            sy[i] = sy[i-1];
                        }
                        sx[0] = nx;
                        sy[0] = ny;
                        if(grow){
                            score++;
                            snake_place_food(sx,sy,len,&fx,&fy,&seed);
                        }
                    }
                }
            }
            cursor_restore_under();
            draw_snake_game(wx,wy,ww,wh,sx,sy,len,fx,fy,score,game_over);
            cursor_move_to(cur_x,cur_y);
        }

        int mdx, mdy; unsigned char btn;
        if(mouse_read_packet(&mdx,&mdy,&btn)){
            cursor_move_to(cur_x+mdx,cur_y+mdy);
            if((btn & 1) && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                cursor_restore_under();
                return;
            }
        }
    }
}

/* ===========================================================
GROCERY SHOP
===========================================================*/
typedef struct grocery_item {
    const char *name;
    int price;
} grocery_item_t;

static const grocery_item_t grocery_items[] = {
    {"Rice 1kg", 60},
    {"Milk 1L", 52},
    {"Bread", 35},
    {"Eggs 6pc", 48},
    {"Potato 1kg", 30},
    {"Apple 500g", 75},
    {"Tea Pack", 90},
    {"Soap", 28}
};

static void draw_fake_qr(int x,int y,int size,int total){
    draw_rect(x,y,size,size,0xFFFFFF);
    draw_rect(x+4,y+4,size-8,size-8,0xE7EEF3);
    int cell = (size-16)/9;
    for(int r=0;r<9;r++){
        for(int c=0;c<9;c++){
            int v = (r*17 + c*11 + total/3 + r*c) % 5;
            if(v == 0 || v == 3){
                draw_rect(x+8+c*cell,y+8+r*cell,cell-1,cell-1,0x0B2437);
            }
        }
    }
    draw_rect(x+10,y+10,cell*2,cell*2,0x0B2437);
    draw_rect(x+size-10-cell*2,y+10,cell*2,cell*2,0x0B2437);
    draw_rect(x+10,y+size-10-cell*2,cell*2,cell*2,0x0B2437);
}

static int grocery_total(const int qty[], int count){
    int total = 0;
    for(int i=0;i<count;i++){
        total += grocery_items[i].price * qty[i];
    }
    return total;
}

static void draw_grocery_app(int wx,int wy,int ww,int wh,const int qty[],int selected,const char *status){
    int count = sizeof(grocery_items)/sizeof(grocery_items[0]);
    int total = grocery_total(qty,count);

    draw_rect(wx+14,wy+44,ww-28,wh-58,0xF7FBFD);
    draw_string(wx+24,wy+58,"NBU Grocery Shop",0x0B3D5C);
    draw_string(wx+24,wy+80,"Click product to add. Keyboard: 1-8 add, Back remove, C clear.",0x3D566E);

    int list_x = wx + 24;
    int list_y = wy + 110;
    for(int i=0;i<count;i++){
        int row_y = list_y + i*32;
        u32 bg = (i == selected) ? 0xDCEFF8 : ((i % 2) ? 0xFFFFFF : 0xEAF7EF);
        draw_rect(list_x,row_y,300,28,bg);
        char line[80];
        snprintf(line,sizeof(line),"%d. %s",i+1,grocery_items[i].name);
        draw_string(list_x+10,row_y+10,line,0x0B2437);
        char price[32];
        snprintf(price,sizeof(price),"Rs %d",grocery_items[i].price);
        draw_string(list_x+178,row_y+10,price,0x185B2A);
        char q[24];
        snprintf(q,sizeof(q),"x%d",qty[i]);
        draw_string(list_x+254,row_y+10,q,0x145DA0);
    }

    int side_x = wx + 350;
    draw_panel(side_x,wy+110,220,258,0xD6EAF2,0xFFFFFF);
    draw_string(side_x+18,wy+128,"Payment QR",0x0B3D5C);
    draw_fake_qr(side_x+50,wy+152,120,total);

    char total_line[48];
    snprintf(total_line,sizeof(total_line),"Total: Rs %d",total);
    draw_string(side_x+18,wy+292,total_line,0x185B2A);
    draw_string(side_x+18,wy+316,status,0x3D566E);

    draw_rounded(side_x+18,wy+334,86,28,8,0x0E4F7C);
    draw_string(side_x+34,wy+344,"Collect",0xFFFFFF);
    draw_rounded(side_x+114,wy+334,68,28,8,0xF1D5D8);
    draw_string(side_x+132,wy+344,"Clear",0x0B2437);
}

static void grocery_shop_ui(void){
    draw_desktop_background();

    const int ww=620, wh=430;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"Grocery Shop");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    int count = sizeof(grocery_items)/sizeof(grocery_items[0]);
    int qty[8] = {0};
    int selected = -1;
    char status[64] = "Select products";
    draw_grocery_app(wx,wy,ww,wh,qty,selected,status);
    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        char key;
        if(keyboard_read_char(&key)){
            cursor_restore_under();
            if(key == 'q' || key == 'Q'){
                return;
            }else if(key >= '1' && key <= '8'){
                int idx = key - '1';
                if(idx < count && qty[idx] < 9){
                    qty[idx]++;
                    selected = idx;
                    snprintf(status,sizeof(status),"Added to cart");
                }
            }else if(key == '\b'){
                if(selected >= 0 && qty[selected] > 0){
                    qty[selected]--;
                    snprintf(status,sizeof(status),"Removed one item");
                }
            }else if(key == 'c' || key == 'C'){
                for(int i=0;i<count;i++) qty[i] = 0;
                selected = -1;
                snprintf(status,sizeof(status),"Cart cleared");
            }else if(key == '\n'){
                snprintf(status,sizeof(status),grocery_total(qty,count) ? "Paid. Collect product." : "Cart is empty");
            }
            draw_grocery_app(wx,wy,ww,wh,qty,selected,status);
            cursor_move_to(cur_x,cur_y);
        }

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx,cur_y+dy);
            if(btn & 1){
                if(cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                    cursor_restore_under();
                    return;
                }
                int list_x = wx + 24;
                int list_y = wy + 110;
                for(int i=0;i<count;i++){
                    int row_y = list_y + i*32;
                    if(cur_x>list_x && cur_x<list_x+300 && cur_y>row_y && cur_y<row_y+28){
                        if(qty[i] < 9) qty[i]++;
                        selected = i;
                        snprintf(status,sizeof(status),"Added to cart");
                    }
                }
                int side_x = wx + 350;
                if(cur_x>side_x+18 && cur_x<side_x+104 && cur_y>wy+334 && cur_y<wy+362){
                    snprintf(status,sizeof(status),grocery_total(qty,count) ? "Paid. Collect product." : "Cart is empty");
                }
                if(cur_x>side_x+114 && cur_x<side_x+182 && cur_y>wy+334 && cur_y<wy+362){
                    for(int i=0;i<count;i++) qty[i] = 0;
                    selected = -1;
                    snprintf(status,sizeof(status),"Cart cleared");
                }
                cursor_restore_under();
                draw_grocery_app(wx,wy,ww,wh,qty,selected,status);
                cursor_move_to(cur_x,cur_y);
            }
        }
    }
}

/* ===========================================================
SYSTEM INFO
===========================================================*/
static void system_info_ui(void){
    draw_desktop_background();

    const int ww=560, wh=330;
    const int wx=(framebuffer_width - ww)/2;
    const int wy=(framebuffer_height - wh)/2;
    draw_window_chrome(wx,wy,ww,wh,"System Info");

    const int cx=wx+ww-26, cy=wy+5, cw=20, ch=20;
    draw_rect(cx,cy,cw,ch,0xD62839);
    draw_string(cx+5,cy+2,"X",0xFFFFFF);

    draw_rect(wx+16,wy+48,ww-32,wh-64,0xFFFFFF);
    draw_string(wx+32,wy+68,"North Bengal University Project",0x145DA0);
    draw_string(wx+32,wy+96,"Kernel UI: MyOS desktop",0x0B2437);

    char fb[64];
    snprintf(fb,sizeof(fb),"Display: %ux%u",framebuffer_width,framebuffer_height);
    draw_string(wx+32,wy+122,fb,0x0B2437);

    char usb[64];
    snprintf(usb,sizeof(usb),"USB devices: %d",usb_get_device_count());
    draw_string(wx+32,wy+148,usb,0x0B2437);

    draw_string(wx+32,wy+174,g_active_netif ? "Network: adapter ready" : "Network: adapter not ready",0x0B2437);
    draw_string(wx+32,wy+210,"Programs: Calc, Browser, JSON, Editor, Grocery",0x3D566E);
    draw_string(wx+32,wy+226,"Games: Click, Cards, Maze, Snake",0x3D566E);
    draw_string(wx+32,wy+252,"Close this window to return to Start menu.",0x3D566E);

    cursor_move_to(cur_x,cur_y);

    int usb_tick = 0;
    while(1){
        if ((++usb_tick & 0xFFF) == 0) {
            usb_poll();
        }
        net_poll();

        int dx, dy; unsigned char btn;
        if(mouse_read_packet(&dx,&dy,&btn)){
            cursor_move_to(cur_x+dx, cur_y+dy);
            if((btn & 1) && cur_x>cx && cur_x<cx+cw && cur_y>cy && cur_y<cy+ch){
                cursor_restore_under();
                return;
            }
        }
    }
}

/* ===========================================================
SDL2 STUDIO (Interactive SDL2 Port Demo)
===========================================================*/
static void sdl2_demo_ui(void){
    draw_desktop_background();
    gfx_present();

    const int ww = 720, wh = 460;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    SDL_Window *win = SDL_CreateWindow("SDL2 Studio - Modern Graphics Port",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       ww, wh, SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetMousePos(cur_x, cur_y);

    int running = 1;
    int paused = 0;
    int show_grid = 1;
    int frame = 0;
    int mx = cur_x - win->x;
    int my = cur_y - win->y;

    /* Simple bouncing orbs */
    int ox[4] = { 120, 260, 420, 560 };
    int oy[4] = { 160, 280, 200, 320 };
    int vx[4] = {   3,  -2,   2,  -3 };
    int vy[4] = {   2,   3,  -2,  -3 };
    SDL_Color cols[4] = {
        { 56, 189, 248, 170 },
        { 244, 114, 182, 170 },
        { 74, 222, 128, 170 },
        { 250, 204, 21, 170 }
    };

    while (running) {
        net_poll();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.key_char == 'q' || ev.key.key_char == 'Q' || ev.key.scancode == 1) {
                    running = 0;
                } else if (ev.key.key_char == ' ') {
                    paused = !paused;
                } else if (ev.key.key_char == 'g' || ev.key.key_char == 'G') {
                    show_grid = !show_grid;
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                cur_x = ev.motion.x;
                cur_y = ev.motion.y;
                mx = cur_x - win->x;
                my = cur_y - win->y;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int lx = ev.button.x - win->x;
                int ly = ev.button.y - win->y;
                /* Close button */
                if (lx >= ww - 30 && lx <= ww - 8 && ly >= 6 && ly <= 28) {
                    running = 0;
                }
                /* Pause/Play button */
                if (lx >= 18 && lx <= 126 && ly >= wh - 46 && ly <= wh - 14) {
                    paused = !paused;
                }
                /* Grid toggle button */
                if (lx >= 138 && lx <= 254 && ly >= wh - 46 && ly <= wh - 14) {
                    show_grid = !show_grid;
                }
            }
        }

        if (!paused) {
            frame++;
            for (int i = 0; i < 4; i++) {
                ox[i] += vx[i];
                oy[i] += vy[i];
                if (ox[i] < 48 || ox[i] > ww - 48) vx[i] = -vx[i];
                if (oy[i] < 80 || oy[i] > wh - 90) vy[i] = -vy[i];
            }
        }

        /* Window frame & gradient canvas */
        SDL_Rect full = { 0, 0, ww, wh };
        SDL_SetRenderDrawColor(ren, 15, 23, 42, 255);
        SDL_RenderFillRoundedRect(ren, &full, 12);

        SDL_Rect titlebar = { 2, 2, ww - 4, 32 };
        SDL_Color tb_top = { 30, 58, 95, 255 };
        SDL_Color tb_bot = { 15, 23, 42, 255 };
        SDL_RenderFillGradientV(ren, &titlebar, tb_top, tb_bot);
        SDL_Color white = { 248, 250, 252, 255 };
        SDL_RenderDrawText(ren, 14, 14, "SDL2 Studio  |  Double-Buffered Alpha Renderer", white);

        /* Close button */
        SDL_Rect close_r = { ww - 30, 6, 22, 22 };
        SDL_SetRenderDrawColor(ren, 220, 38, 38, 255);
        SDL_RenderFillRoundedRect(ren, &close_r, 5);
        SDL_RenderDrawText(ren, ww - 23, 13, "X", white);

        /* Canvas backdrop */
        SDL_Rect canvas = { 14, 44, ww - 28, wh - 104 };
        SDL_Color c_top = { 9, 17, 30, 255 };
        SDL_Color c_bot = { 20, 42, 72, 255 };
        SDL_RenderFillGradientV(ren, &canvas, c_top, c_bot);

        /* Optional SDL2 grid */
        if (show_grid) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 148, 163, 184, 35);
            for (int gx = 34; gx < ww - 20; gx += 32) {
                SDL_RenderDrawLine(ren, gx, 46, gx, wh - 62);
            }
            for (int gy = 64; gy < wh - 62; gy += 32) {
                SDL_RenderDrawLine(ren, 16, gy, ww - 16, gy);
            }
        }

        /* Alpha-blended orbs + connecting lines */
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 56, 189, 248, 90);
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            SDL_RenderDrawLine(ren, ox[i], oy[i], ox[j], oy[j]);
        }
        for (int i = 0; i < 4; i++) {
            SDL_SetRenderDrawColor(ren, cols[i].r, cols[i].g, cols[i].b, cols[i].a);
            SDL_RenderFillCircle(ren, ox[i], oy[i], 34);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 110);
            SDL_RenderDrawCircle(ren, ox[i], oy[i], 34);
        }

        /* Interactive mouse spotlight inside canvas */
        if (mx > 14 && mx < ww - 14 && my > 44 && my < wh - 60) {
            SDL_SetRenderDrawColor(ren, 56, 189, 248, 45);
            SDL_RenderFillCircle(ren, mx, my, 42);
            SDL_SetRenderDrawColor(ren, 248, 250, 252, 140);
            SDL_RenderDrawCircle(ren, mx, my, 42);
        }

        /* Bottom control bar */
        SDL_Rect b1 = { 18, wh - 46, 108, 32 };
        SDL_SetRenderDrawColor(ren, 2, 132, 199, 255);
        SDL_RenderFillRoundedRect(ren, &b1, 8);
        SDL_RenderDrawText(ren, 34, wh - 34, paused ? "Resume" : "Pause", white);

        SDL_Rect b2 = { 138, wh - 46, 116, 32 };
        SDL_SetRenderDrawColor(ren, 51, 65, 85, 255);
        SDL_RenderFillRoundedRect(ren, &b2, 8);
        SDL_RenderDrawText(ren, 152, wh - 34, show_grid ? "Grid: ON" : "Grid: OFF", white);

        char stats[64];
        snprintf(stats, sizeof(stats), "Frame: %d   |   Space: Pause   G: Grid   Q: Exit", frame);
        SDL_Color muted = { 148, 163, 184, 255 };
        SDL_RenderDrawText(ren, 274, wh - 34, stats, muted);

        /* Draw cursor inside window before present */
        cursor_draw_shape(cur_x, cur_y, 0x0F172A);
        SDL_RenderPresent(ren);

        for (volatile int d = 0; d < 180000; d++);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    cur_saved = 0;
}

/* ===========================================================
BACKGROUND SYSTEM WORKERS & TASK MANAGER (app 12)
===========================================================*/
static void net_bg_worker(void *arg) {
    (void)arg;
    while (1) {
        net_poll();
        sched_yield();
    }
}

static void sample_worker_job(void *arg) {
    int iterations = (int)(uintptr_t)arg;
    volatile u32 acc = 0;
    for (int i = 0; i < iterations; i++) {
        acc += (u32)(i * 7 + 3);
        if ((i & 0x3FF) == 0) {
            sched_yield();
        }
    }
}

static void task_manager_ui(void) {
    const int ww = 720, wh = 480;
    const int wx = ((int)framebuffer_width - ww) / 2;
    const int wy = ((int)framebuffer_height - wh) / 2;
    const int cx = wx + ww - 28, cy = wy + 6, cw = 22, ch = 22;
    const int bx1 = wx + 20, by1 = wy + wh - 46, bw1 = 160, bh1 = 30;

    gfx_set_deferred(1);
    cur_saved = 0;
    int drawn_cx = cur_x, drawn_cy = cur_y;
    int tick = 0;

    while (1) {
        sched_yield();

        int dx, dy; unsigned char btn;
        int clicked = 0;
        while (mouse_read_packet(&dx, &dy, &btn)) {
            cur_x = clampi(cur_x + dx, 0, (int)framebuffer_width - 1);
            cur_y = clampi(cur_y + dy, 0, (int)framebuffer_height - 1);
            if (btn & 1) clicked = 1;
        }

        if (clicked) {
            if (cur_x >= cx && cur_x <= cx + cw && cur_y >= cy && cur_y <= cy + ch) {
                gfx_set_deferred(0);
                return;
            }
            if (cur_x >= bx1 && cur_x <= bx1 + bw1 && cur_y >= by1 && cur_y <= by1 + bh1) {
                worker_submit("compute-job", sample_worker_job, (void *)(uintptr_t)25000);
                worker_submit("compute-job", sample_worker_job, (void *)(uintptr_t)25000);
            }
        }

        if ((tick++ & 0x1F) == 0 || clicked) {
            draw_desktop_background();
            draw_window_chrome(wx, wy, ww, wh, "Task Manager  |  Multitask Scheduler & Workers");
            draw_rounded_rect(cx, cy, cw, ch, 5, 0xFFDC2626);
            draw_string(cx + 7, cy + 7, "X", 0xFFFFFF);

            sched_stats_t st;
            sched_get_stats(&st);

            char hdr[96];
            snprintf(hdr, sizeof(hdr), "Tasks: %d   Workers: %d   Switches: %d   Jobs Done: %d (Pending: %d)",
                     st.active_tasks, st.workers_count, st.total_switches, st.jobs_completed, st.jobs_pending);
            draw_rounded_rect(wx + 16, wy + 42, ww - 32, 30, 6, 0xFFE2E8F0);
            draw_string(wx + 26, wy + 53, hdr, 0x0F172A);

            /* Table Header */
            draw_rect(wx + 16, wy + 82, ww - 32, 24, 0x1E293B);
            draw_string(wx + 24,  wy + 90, "PID",      0xF8FAFC);
            draw_string(wx + 74,  wy + 90, "NAME",     0xF8FAFC);
            draw_string(wx + 274, wy + 90, "STATE",    0xF8FAFC);
            draw_string(wx + 394, wy + 90, "PRIO",     0xF8FAFC);
            draw_string(wx + 474, wy + 90, "SWITCHES", 0xF8FAFC);
            draw_string(wx + 594, wy + 90, "TICKS",    0xF8FAFC);

            int row = 0;
            for (int i = 0; i < SCHED_MAX_TASKS && row < 12; i++) {
                sched_task_info_t ti;
                if (!sched_get_task_info(i, &ti)) continue;
                int ry = wy + 108 + row * 24;
                u32 bg = (row % 2) ? 0xF1F5F9 : 0xFFFFFF;
                draw_rect(wx + 16, ry, ww - 32, 22, bg);

                const char *sname = (ti.state == TASK_RUNNING) ? "RUNNING" :
                                    (ti.state == TASK_READY)   ? "READY" :
                                    (ti.state == TASK_SLEEPING)? "SLEEP" : "DONE";
                u32 scol = (ti.state == TASK_RUNNING) ? 0x16A34A :
                           (ti.state == TASK_READY)   ? 0x0284C7 : 0x64748B;

                char b_id[16], b_pr[16], b_sw[24], b_tk[24];
                snprintf(b_id, sizeof(b_id), "%d", ti.id);
                snprintf(b_pr, sizeof(b_pr), "%d", ti.priority);
                snprintf(b_sw, sizeof(b_sw), "%u", ti.switches);
                snprintf(b_tk, sizeof(b_tk), "%u", ti.ticks);

                draw_string(wx + 24,  ry + 7, b_id,    0x0F172A);
                draw_string(wx + 74,  ry + 7, ti.name, 0x0F172A);
                draw_string(wx + 274, ry + 7, sname,   scol);
                draw_string(wx + 394, ry + 7, b_pr,    0x334155);
                draw_string(wx + 474, ry + 7, b_sw,    0x334155);
                draw_string(wx + 594, ry + 7, b_tk,    0x334155);
                row++;
            }

            draw_rounded_rect(bx1, by1, bw1, bh1, 8, 0xFF0284C7);
            draw_string(bx1 + 14, by1 + 11, "Spawn Worker Jobs", 0xFFFFFF);
            draw_string(bx1 + bw1 + 18, by1 + 11, "Click X to return to Desktop", 0x475569);

            gfx_present();
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
        } else if (cur_x != drawn_cx || cur_y != drawn_cy) {
            gfx_present_rect(drawn_cx, drawn_cy, CUR_W, CUR_H);
            cursor_draw_front(cur_x, cur_y);
            drawn_cx = cur_x;
            drawn_cy = cur_y;
        }
    }
}

/* ===========================================================
kmain
===========================================================*/
void kmain(unsigned magic,unsigned addr){
    (void)magic;
    init_graphics((void*)addr);
    init_mouse();
    sched_init();
    wm_init();
    worker_pool_init(4);
    sched_spawn("net-bg", net_bg_worker, 0, 4);

    int usb_net_found = 0;
    int fallback_nic_started = 0;
    usb_host_init();
    // Give the host controller a few polls to detect already-connected devices
    for (int i = 0; i < 8; ++i) {
        usb_poll();
    }
    if (rndis_active || ipheth_active || cdc_ecm_active) {
        usb_net_found = 1;
    }
    // If no USB netif, fall back to NIC
    if (!usb_net_found) {
        // Try e1000e first, then rtl8139
        if (e1000e_init() != 0) {
            rtl8139_init();
        }
        fallback_nic_started = 1;
    }
    if (g_active_netif) {
        dhcp_start();
    }

    // Start with cursor at center (save & draw once)
    cursor_move_to((int)framebuffer_width/2,(int)framebuffer_height/2);

    if(show_welcome()){
        while(1){
            usb_poll();
            net_poll();
            if (!rndis_active && !ipheth_active && !cdc_ecm_active && !fallback_nic_started && !g_active_netif) {
                if (e1000e_init() != 0) {
                    rtl8139_init();
                }
                fallback_nic_started = 1;
                if (g_active_netif) dhcp_start();
            }
            int app = show_desktop();
            if(app==1){
                calculator_ui();
            }else if(app==2){
                browser_ui();
            }else if(app==3){
                json_viewer_ui();
            }else if(app==4){
                text_editor_ui();
            }else if(app==5){
                click_game_ui();
            }else if(app==6){
                high_low_card_ui();
            }else if(app==7){
                keyboard_maze_ui();
            }else if(app==8){
                snake_game_ui();
            }else if(app==9){
                grocery_shop_ui();
            }else if(app==10){
                system_info_ui();
            }else if(app==11){
                sdl2_demo_ui();
            }else if(app==12){
                task_manager_ui();
            }
        }
    }
}
