#include "wm.h"
#include "billing.h"
#include "graphics.h"
#include "scheduler.h"
#include "string.h"
#include "stdio.h"
#include "net.h"
#include "dhcp.h"
#include "dns.h"
#include "http.h"
#include "tls.h"
#define JSMN_HEADER
#include "json.h"
#include "drivers/usb/usb_host.h"
#include "drivers/usb/usb_rndis.h"
#include "drivers/usb/usb_ipheth.h"
#include "drivers/usb/usb_cdc_ecm.h"

typedef struct {
    const char *url;
    char bodybuf[4096];
    volatile int done;
    volatile int cancel;
    int try_tls;
    int dns_ok;
    uint32_t resolved_ip;
    int got;
} wm_http_job_t;

typedef struct {
    int active;
    int app_id;
    int x, y, w, h;
    char title[48];

    /* Calculator */
    char calc_expr[64];
    int  calc_len;

    /* Text editor */
    char ed_text[512];
    int  ed_len;

    /* Browser / JSON */
    wm_http_job_t http;
    int http_rendered;

    /* Grocery */
    int gr_qty[8];
    int gr_sel;
    char gr_status[48];

    /* SDL2 Studio */
    int ox[4], oy[4], vx[4], vy[4];
    int sdl_paused;
    int sdl_grid;
    int sdl_frame;
} wm_win_t;

static wm_win_t wm_wins[WM_MAX_WINDOWS];
static int wm_z[WM_MAX_WINDOWS];
static int wm_count = 0;
static int drag_idx = -1;
static int drag_ox = 0, drag_oy = 0;
static int wm_tick_cnt = 0;

/* URL helper duplicated locally so wm.c is self-contained */
static int wm_parse_url(const char *url, char *host, int host_cap, const char **path_out, uint16_t *port_out) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7; *port_out = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8; *port_out = 443;
    } else {
        *port_out = 80;
    }
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < host_cap - 1) {
        host[i++] = *p++;
    }
    host[i] = '\0';
    if (*p == ':') {
        p++;
        int port = 0;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (*p - '0'); p++; }
        if (port > 0) *port_out = (uint16_t)port;
    }
    *path_out = (*p == '/') ? p : "/";
    return (i > 0);
}

static void wm_http_worker(void *arg) {
    wm_http_job_t *job = (wm_http_job_t *)arg;
    char host[128];
    const char *path;
    uint16_t port;
    int parsed = wm_parse_url(job->url, host, sizeof(host), &path, &port);

    job->resolved_ip = 0;
    job->dns_ok = 0;
    job->got = 0;

    for (int w = 0; w < 4000 && !net_has_ipv4() && !job->cancel; w++) {
        net_poll();
    }

    if (parsed && !job->cancel) {
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
                                       job->bodybuf, (int)sizeof(job->bodybuf));
    }

    if (job->try_tls && job->got <= 0 && job->resolved_ip != 0 && !job->cancel) {
        int tgot = tls_http_get_by_ip(job->resolved_ip, host, path,
                                      job->bodybuf, (int)sizeof(job->bodybuf));
        if (tgot > 0) job->got = tgot;
    }

    job->done = 1;
}

static void wm_sample_job(void *arg) {
    int n = (int)(uintptr_t)arg;
    volatile u32 acc = 0;
    for (int i = 0; i < n; i++) {
        acc += (u32)(i * 7 + 3);
        if ((i & 0x3FF) == 0) sched_yield();
    }
}

void wm_init(void) {
    memset(wm_wins, 0, sizeof(wm_wins));
    wm_count = 0;
    drag_idx = -1;
}

static void wm_bring_to_front(int idx) {
    int pos = -1;
    for (int i = 0; i < wm_count; i++) {
        if (wm_z[i] == idx) { pos = i; break; }
    }
    if (pos < 0) return;
    for (int i = pos; i < wm_count - 1; i++) {
        wm_z[i] = wm_z[i + 1];
    }
    wm_z[wm_count - 1] = idx;
}

static void wm_close(int idx) {
    if (idx < 0 || idx >= WM_MAX_WINDOWS || !wm_wins[idx].active) return;
    wm_wins[idx].http.cancel = 1;
    wm_wins[idx].active = 0;
    int pos = -1;
    for (int i = 0; i < wm_count; i++) {
        if (wm_z[i] == idx) { pos = i; break; }
    }
    if (pos >= 0) {
        for (int i = pos; i < wm_count - 1; i++) {
            wm_z[i] = wm_z[i + 1];
        }
        wm_count--;
    }
    if (drag_idx == idx) drag_idx = -1;
}

static void wm_start_http(wm_win_t *w, const char *url) {
    w->http.url = url;
    w->http.done = 0;
    w->http.cancel = 0;
    w->http.try_tls = 1;
    w->http.dns_ok = 0;
    w->http.resolved_ip = 0;
    w->http.got = 0;
    w->http.bodybuf[0] = '\0';
    w->http_rendered = 0;
    worker_submit("http-api", wm_http_worker, &w->http);
}

int wm_open(int app_id) {
    /* Full-screen arcade games stay in their own loop */
    if (app_id == 5 || app_id == 6 || app_id == 7 || app_id == 8) {
        return 0;
    }

    /* If already open, bring to front */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (wm_wins[i].active && wm_wins[i].app_id == app_id) {
            wm_bring_to_front(i);
            return 1;
        }
    }

    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!wm_wins[i].active) { slot = i; break; }
    }
    if (slot < 0) return 1;

    wm_win_t *w = &wm_wins[slot];
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->app_id = app_id;

    int cascade = wm_count * 38;
    w->x = 380 + (cascade % 220);
    w->y = 58 + (cascade % 140);

    if (app_id == 1) {
        w->w = 320; w->h = 390;
        snprintf(w->title, sizeof(w->title), "Calculator");
    } else if (app_id == 2) {
        w->w = 600; w->h = 410;
        snprintf(w->title, sizeof(w->title), "MyOS Browser (API)");
        wm_start_http(w, "http://jsonplaceholder.typicode.com/posts/2");
    } else if (app_id == 3) {
        w->w = 580; w->h = 410;
        snprintf(w->title, sizeof(w->title), "JSON Viewer (API)");
        wm_start_http(w, "http://jsonplaceholder.typicode.com/users/1");
    } else if (app_id == 4) {
        w->w = 540; w->h = 370;
        snprintf(w->title, sizeof(w->title), "Text Editor");
        snprintf(w->ed_text, sizeof(w->ed_text), "Type notes here...\n");
        w->ed_len = (int)strlen(w->ed_text);
    } else if (app_id == 9) {
        w->w = 580; w->h = 400;
        w->gr_sel = -1;
        snprintf(w->gr_status, sizeof(w->gr_status), "Click item to add");
        snprintf(w->title, sizeof(w->title), "Grocery Shop");
    } else if (app_id == 10) {
        w->w = 520; w->h = 310;
        snprintf(w->title, sizeof(w->title), "System Info");
    } else if (app_id == 11) {
        w->w = 560; w->h = 380;
        snprintf(w->title, sizeof(w->title), "SDL2 Studio");
        w->ox[0] = 90;  w->oy[0] = 120; w->vx[0] = 3;  w->vy[0] = 2;
        w->ox[1] = 200; w->oy[1] = 210; w->vx[1] = -2; w->vy[1] = 3;
        w->ox[2] = 340; w->oy[2] = 150; w->vx[2] = 2;  w->vy[2] = -2;
        w->ox[3] = 440; w->oy[3] = 240; w->vx[3] = -3; w->vy[3] = -2;
        w->sdl_grid = 1;
    } else if (app_id == 12) {
        w->w = 680; w->h = 410;
        snprintf(w->title, sizeof(w->title), "Task Manager (Scheduler)");
    } else if (app_id == 13) {
        w->w = 760; w->h = 480;
        snprintf(w->title, sizeof(w->title), "MaaDurga Billing & POS");
        billing_init();
    } else {
        w->active = 0;
        return 0;
    }

    if (w->x + w->w > (int)framebuffer_width - 12) {
        w->x = (int)framebuffer_width - w->w - 16;
    }
    if (w->x < 12) w->x = 12;

    wm_z[wm_count++] = slot;
    return 1;
}

static void calc_eval_win(wm_win_t *w) {
    int res = 0; char op = '+';
    for (int i = 0; i < w->calc_len; i++) {
        int v = 0;
        while (i < w->calc_len && w->calc_expr[i] >= '0' && w->calc_expr[i] <= '9') {
            v = v * 10 + (w->calc_expr[i] - '0');
            i++;
        }
        if (op == '+') res += v;
        else if (op == '-') res -= v;
        else if (op == '*') res *= v;
        else if (op == '/') res /= (v ? v : 1);
        if (i < w->calc_len) op = w->calc_expr[i];
    }
    snprintf(w->calc_expr, sizeof(w->calc_expr), "%d", res);
    w->calc_len = (int)strlen(w->calc_expr);
}

static void wm_draw_calc(wm_win_t *w) {
    int x = w->x + 16, y = w->y + 44;
    draw_rounded_rect(x, y, w->w - 32, 46, 8, 0xFF0F172A);
    draw_string(x + 12, y + 18, w->calc_len ? w->calc_expr : "0", 0x38BDF8);

    const char *keys[16] = {
        "7","8","9","/",
        "4","5","6","*",
        "1","2","3","-",
        "C","0","=","+"
    };
    int bw = 64, bh = 54, gap = 8;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int bx = x + c * (bw + gap);
            int by = y + 60 + r * (bh + gap);
            const char *lbl = keys[r * 4 + c];
            u32 col = (lbl[0] == '=') ? 0xFF0284C7 :
                      (lbl[0] == 'C') ? 0xFFDC2626 :
                      (c == 3)        ? 0xFF334155 : 0xFFE2E8F0;
            u32 tcol = (c == 3 || lbl[0] == '=' || lbl[0] == 'C') ? 0xFFFFFF : 0x0F172A;
            draw_rounded_rect(bx, by, bw, bh, 8, col);
            draw_string(bx + 28, by + 22, lbl, tcol);
        }
    }
}

static void wm_draw_http_app(wm_win_t *w, int is_json) {
    int wx = w->x, wy = w->y, ww = w->w, wh = w->h;
    int ab_x = wx + 12, ab_y = wy + 38, ab_w = ww - 110, ab_h = 26;
    draw_rounded_rect(ab_x, ab_y, ab_w, ab_h, 6, 0xFFE2E8F0);
    draw_string(ab_x + 8, ab_y + 9, w->http.url, 0x0F172A);

    int rx = ab_x + ab_w + 8, ry = ab_y, rw = 78, rh = 26;
    draw_rounded_rect(rx, ry, rw, rh, 6, 0xFF0284C7);
    draw_string(rx + 12, ry + 9, "Refresh", 0xFFFFFF);

    int ct_x = wx + 12, ct_y = ab_y + ab_h + 8, ct_w = ww - 24, ct_h = wh - (38 + ab_h + 8 + 14);
    draw_rect(ct_x, ct_y, ct_w, ct_h, 0xFFFFFF);

    if (!w->http.done) {
        draw_string(ct_x + 10, ct_y + 12, "Fetching API in worker thread...", 0x0284C7);
        return;
    }

    if (w->http.got > 0) {
        if (is_json) {
            jsmn_parser p;
            jsmntok_t t[128];
            jsmn_init(&p);
            int r = jsmn_parse(&p, w->http.bodybuf, strlen(w->http.bodybuf), t, 128);
            if (r < 0) {
                draw_string(ct_x + 10, ct_y + 12, "JSON parse error (raw body):", 0xDC2626);
                draw_string(ct_x + 10, ct_y + 32, w->http.bodybuf, 0x0F172A);
            } else {
                int line = 0, max_lines = (ct_h - 16) / 16;
                for (int i = 1; i < r && line < max_lines; i++) {
                    if (t[i].type == JSMN_STRING && t[i - 1].type == JSMN_STRING) {
                        char k[48], v[64], lb[120];
                        int kl = t[i - 1].end - t[i - 1].start;
                        int vl = t[i].end - t[i].start;
                        if (kl > 47) kl = 47;
                        if (vl > 63) vl = 63;
                        memcpy(k, w->http.bodybuf + t[i - 1].start, kl); k[kl] = 0;
                        memcpy(v, w->http.bodybuf + t[i].start, vl);     v[vl] = 0;
                        snprintf(lb, sizeof(lb), "%s: %s", k, v);
                        draw_string(ct_x + 10, ct_y + 10 + line * 16, lb, 0x0F172A);
                        line++;
                    }
                }
            }
        } else {
            int max_lines = (ct_h - 16) / 16;
            int line = 0;
            char *p = w->http.bodybuf;
            while (line < max_lines && *p) {
                char tmp[72]; int ti = 0;
                while (*p && *p != '\n' && ti < (int)sizeof(tmp) - 1) tmp[ti++] = *p++;
                tmp[ti] = '\0';
                if (*p == '\n') p++;
                draw_string(ct_x + 10, ct_y + 10 + line * 16, tmp, 0x0F172A);
                line++;
            }
        }
    } else {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "Fetch failed: dns=%d ip=%u got=%d http_ret=%d",
                 w->http.dns_ok, w->http.resolved_ip, w->http.got, http_last_ret);
        draw_string(ct_x + 10, ct_y + 12, dbg, 0xDC2626);
        draw_string(ct_x + 10, ct_y + 32, "Check network / USB tether & click Refresh.", 0x475569);
    }
}

static void wm_draw_editor(wm_win_t *w) {
    int ct_x = w->x + 12, ct_y = w->y + 40, ct_w = w->w - 24, ct_h = w->h - 54;
    draw_rect(ct_x, ct_y, ct_w, ct_h, 0xFFFFFF);
    int row = 0, col = 0;
    int max_cols = (ct_w - 20) / 8;
    for (int i = 0; i < w->ed_len; i++) {
        char ch = w->ed_text[i];
        if (ch == '\n') { row++; col = 0; continue; }
        if (col >= max_cols) { row++; col = 0; }
        if (ct_y + 12 + row * 16 > ct_y + ct_h - 16) break;
        draw_char(ct_x + 10 + col * 8, ct_y + 12 + row * 16, ch, 0x0F172A);
        col++;
    }
    draw_rect(ct_x + 10 + col * 8, ct_y + 10 + row * 16, 2, 14, 0x0284C7);
}

static void wm_draw_grocery(wm_win_t *w) {
    static const char *names[6] = { "Rice 1kg", "Milk 1L", "Bread", "Eggs 6pc", "Apple 1kg", "Tea 250g" };
    static const int prices[6] = { 65, 55, 40, 48, 120, 95 };
    int lx = w->x + 18, ly = w->y + 46, total = 0;
    for (int i = 0; i < 6; i++) {
        total += prices[i] * w->gr_qty[i];
        int ry = ly + i * 38;
        u32 bg = (i == w->gr_sel) ? 0xFFBAE6FD : 0xFFE2E8F0;
        draw_rounded_rect(lx, ry, 310, 32, 6, bg);
        char line[64];
        snprintf(line, sizeof(line), "%d. %s  -  Rs %d   x%d", i + 1, names[i], prices[i], w->gr_qty[i]);
        draw_string(lx + 10, ry + 12, line, 0x0F172A);
    }
    int sx = w->x + 350;
    draw_rounded_rect(sx, ly, 206, 220, 10, 0xFF0F172A);
    char tot[48];
    snprintf(tot, sizeof(tot), "Total: Rs %d", total);
    draw_string(sx + 18, ly + 24, tot, 0x4ADE80);
    draw_string(sx + 18, ly + 56, w->gr_status, 0xCBD5E1);
    draw_rounded_rect(sx + 18, ly + 158, 80, 30, 6, 0xFF0284C7);
    draw_string(sx + 30, ly + 169, "Pay", 0xFFFFFF);
    draw_rounded_rect(sx + 108, ly + 158, 80, 30, 6, 0xFFDC2626);
    draw_string(sx + 126, ly + 169, "Clear", 0xFFFFFF);
}

static void wm_draw_sysinfo(wm_win_t *w) {
    int x = w->x + 20, y = w->y + 50;
    char buf[80];
    draw_string(x, y, "MyOS 2.0  |  Multi-Window + SDL2 + Scheduler", 0x0284C7);
    snprintf(buf, sizeof(buf), "Display:     %ux%u @ %ubpp", framebuffer_width, framebuffer_height, framebuffer_bpp);
    draw_string(x, y + 32, buf, 0x0F172A);
    snprintf(buf, sizeof(buf), "USB Devices: %d connected", usb_get_device_count());
    draw_string(x, y + 58, buf, 0x0F172A);
    if (g_active_netif && g_active_netif->ip != 0) {
        u32 ip = g_active_netif->ip;
        snprintf(buf, sizeof(buf), "Network IP:  %u.%u.%u.%u",
                 (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
        draw_string(x, y + 84, buf, 0x16A34A);
    } else {
        draw_string(x, y + 84, "Network IP:  Not bound", 0xDC2626);
    }
    draw_string(x, y + 120, "Tip: Drag any window by its title bar!", 0x475569);
    draw_string(x, y + 144, "Multiple windows can run side by side.", 0x475569);
}

static void wm_draw_sdl2_studio(wm_win_t *w) {
    int cx = w->x + 12, cy = w->y + 38, cw = w->w - 24, ch = w->h - 92;
    draw_gradient_v(cx, cy, cw, ch, 0x09111E, 0x163254);
    if (w->sdl_grid) {
        for (int gx = cx + 24; gx < cx + cw; gx += 32)
            draw_rect_alpha(gx, cy, 1, ch, gfx_rgba(148, 163, 184, 35));
        for (int gy = cy + 24; gy < cy + ch; gy += 32)
            draw_rect_alpha(cx, gy, cw, 1, gfx_rgba(148, 163, 184, 35));
    }
    u32 cols[4] = {
        gfx_rgba(56, 189, 248, 175),
        gfx_rgba(244, 114, 182, 175),
        gfx_rgba(74, 222, 128, 175),
        gfx_rgba(250, 204, 21, 175)
    };
    for (int i = 0; i < 4; i++) {
        draw_circle_filled(cx + w->ox[i], cy + w->oy[i], 26, cols[i]);
        draw_circle_outline(cx + w->ox[i], cy + w->oy[i], 26, gfx_rgba(255, 255, 255, 110));
    }
    int by = w->y + w->h - 42;
    draw_rounded_rect(w->x + 14, by, 96, 28, 6, 0xFF0284C7);
    draw_string(w->x + 32, by + 10, w->sdl_paused ? "Resume" : "Pause", 0xFFFFFF);
    draw_rounded_rect(w->x + 120, by, 96, 28, 6, 0xFF334155);
    draw_string(w->x + 134, by + 10, w->sdl_grid ? "Grid: ON" : "Grid: OFF", 0xFFFFFF);
    char fb[48];
    snprintf(fb, sizeof(fb), "Frame: %d", w->sdl_frame);
    draw_string(w->x + 234, by + 10, fb, 0x334155);
}

static void wm_draw_taskmgr(wm_win_t *w) {
    sched_stats_t st;
    sched_get_stats(&st);
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "Tasks:%d  Workers:%d  Switches:%d  Done:%d",
             st.active_tasks, st.workers_count, st.total_switches, st.jobs_completed);
    draw_rounded_rect(w->x + 14, w->y + 40, w->w - 28, 28, 6, 0xFFE2E8F0);
    draw_string(w->x + 22, w->y + 50, hdr, 0x0F172A);

    draw_rect(w->x + 14, w->y + 76, w->w - 28, 22, 0x1E293B);
    draw_string(w->x + 22,  w->y + 83, "PID",      0xF8FAFC);
    draw_string(w->x + 70,  w->y + 83, "NAME",     0xF8FAFC);
    draw_string(w->x + 250, w->y + 83, "STATE",    0xF8FAFC);
    draw_string(w->x + 360, w->y + 83, "SWITCHES", 0xF8FAFC);
    draw_string(w->x + 480, w->y + 83, "TICKS",    0xF8FAFC);

    int row = 0;
    for (int i = 0; i < SCHED_MAX_TASKS && row < 10; i++) {
        sched_task_info_t ti;
        if (!sched_get_task_info(i, &ti)) continue;
        int ry = w->y + 100 + row * 24;
        draw_rect(w->x + 14, ry, w->w - 28, 22, (row % 2) ? 0xF1F5F9 : 0xFFFFFF);
        const char *sn = (ti.state == TASK_RUNNING) ? "RUNNING" :
                         (ti.state == TASK_READY) ? "READY" :
                         (ti.state == TASK_SLEEPING) ? "SLEEP" : "DONE";
        u32 sc = (ti.state == TASK_RUNNING) ? 0x16A34A :
                 (ti.state == TASK_READY) ? 0x0284C7 : 0x64748B;
        char b0[12], b1[20], b2[20];
        snprintf(b0, sizeof(b0), "%d", ti.id);
        snprintf(b1, sizeof(b1), "%u", ti.switches);
        snprintf(b2, sizeof(b2), "%u", ti.ticks);
        draw_string(w->x + 22,  ry + 7, b0, 0x0F172A);
        draw_string(w->x + 70,  ry + 7, ti.name, 0x0F172A);
        draw_string(w->x + 250, ry + 7, sn, sc);
        draw_string(w->x + 360, ry + 7, b1, 0x334155);
        draw_string(w->x + 480, ry + 7, b2, 0x334155);
        row++;
    }

    int by = w->y + w->h - 42;
    draw_rounded_rect(w->x + 14, by, 156, 28, 6, 0xFF0284C7);
    draw_string(w->x + 24, by + 10, "Spawn Worker Jobs", 0xFFFFFF);
}

void wm_draw_all(void) {
    for (int z = 0; z < wm_count; z++) {
        int idx = wm_z[z];
        wm_win_t *w = &wm_wins[idx];
        if (!w->active) continue;
        int focused = (z == wm_count - 1);

        draw_shadow(w->x, w->y, w->w, w->h, 12, 8, focused ? 105 : 65);
        draw_rounded_rect(w->x, w->y, w->w, w->h, 12, focused ? 0xFF38BDF8 : 0xFF334155);
        draw_rect(w->x + 2, w->y + 2, w->w - 4, w->h - 4, 0xF8FAFC);
        draw_gradient_v(w->x + 2, w->y + 2, w->w - 4, 30,
                        focused ? 0x1E3A5F : 0x1E293B, 0x0F172A);
        draw_string(w->x + 14, w->y + 13, w->title, focused ? 0xFFFFFF : 0x94A3B8);

        /* Close button */
        int cx = w->x + w->w - 28, cy = w->y + 6;
        draw_rounded_rect(cx, cy, 22, 22, 5, 0xFFDC2626);
        draw_string(cx + 7, cy + 7, "X", 0xFFFFFF);

        if (w->app_id == 1) wm_draw_calc(w);
        else if (w->app_id == 2) wm_draw_http_app(w, 0);
        else if (w->app_id == 3) wm_draw_http_app(w, 1);
        else if (w->app_id == 4) wm_draw_editor(w);
        else if (w->app_id == 9) wm_draw_grocery(w);
        else if (w->app_id == 10) wm_draw_sysinfo(w);
        else if (w->app_id == 11) wm_draw_sdl2_studio(w);
        else if (w->app_id == 12) wm_draw_taskmgr(w);
        else if (w->app_id == 13) billing_draw(w->x, w->y, w->w, w->h);
    }
}

void wm_draw_taskbar_tabs(int bar_y, int bar_h, int start_x) {
    int tx = start_x;
    for (int z = 0; z < wm_count; z++) {
        int idx = wm_z[z];
        wm_win_t *w = &wm_wins[idx];
        if (!w->active) continue;
        int focused = (z == wm_count - 1);
        int tw = 136;
        if (tx + tw > (int)framebuffer_width - 220) break;
        draw_rounded_rect(tx, bar_y + 8, tw, bar_h - 16, 8,
                          focused ? 0xFF1E3A5F : gfx_rgba(30, 41, 59, 210));
        draw_rounded_outline(tx, bar_y + 8, tw, bar_h - 16, 8, 1,
                             focused ? 0xFF38BDF8 : gfx_rgba(148, 163, 184, 55));
        char short_t[16];
        strncpy(short_t, w->title, 15);
        short_t[15] = '\0';
        draw_string(tx + 10, bar_y + (bar_h - 8) / 2, short_t, focused ? 0xFFFFFF : 0xCBD5E1);
        tx += tw + 8;
    }
}

int wm_is_dragging(void) {
    return (drag_idx >= 0);
}

int wm_handle_mouse(int mx, int my, int pressed, int held, int released, int bar_h) {
    int changed = 0;

    if (released && drag_idx >= 0) {
        drag_idx = -1;
        changed = 1;
    }

    if (held && drag_idx >= 0 && wm_wins[drag_idx].active) {
        wm_win_t *w = &wm_wins[drag_idx];
        int nx = mx - drag_ox;
        int ny = my - drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 35) ny = 35;
        if (nx + w->w > (int)framebuffer_width) nx = (int)framebuffer_width - w->w;
        if (ny + 36 > (int)framebuffer_height - bar_h) ny = (int)framebuffer_height - bar_h - 36;
        if (nx != w->x || ny != w->y) {
            w->x = nx;
            w->y = ny;
            changed = 1;
        }
        return changed;
    }

    if (!pressed) return 0;

    /* Check taskbar window tabs first */
    int bar_y = (int)framebuffer_height - bar_h;
    if (my >= bar_y) {
        int tx = 138;
        for (int z = 0; z < wm_count; z++) {
            int idx = wm_z[z];
            int tw = 136;
            if (mx >= tx && mx <= tx + tw && my >= bar_y + 8 && my <= bar_y + bar_h - 8) {
                wm_bring_to_front(idx);
                return 1;
            }
            tx += tw + 8;
        }
        return 0;
    }

    /* Hit-test windows top-to-bottom */
    for (int z = wm_count - 1; z >= 0; z--) {
        int idx = wm_z[z];
        wm_win_t *w = &wm_wins[idx];
        if (!w->active) continue;
        if (mx < w->x || mx > w->x + w->w || my < w->y || my > w->y + w->h) continue;

        wm_bring_to_front(idx);

        /* Close button */
        int cx = w->x + w->w - 28, cy = w->y + 6;
        if (mx >= cx && mx <= cx + 22 && my >= cy && my <= cy + 22) {
            wm_close(idx);
            return 1;
        }

        /* Titlebar drag start */
        if (my <= w->y + 32) {
            drag_idx = idx;
            drag_ox = mx - w->x;
            drag_oy = my - w->y;
            return 1;
        }

        /* App-specific clicks */
        if (w->app_id == 1) {
            const char *keys[16] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
            int bx0 = w->x + 16, by0 = w->y + 104, bw = 64, bh = 54, gap = 8;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    int bx = bx0 + c * (bw + gap);
                    int by = by0 + r * (bh + gap);
                    if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                        char ch = keys[r * 4 + c][0];
                        if (ch == 'C') { w->calc_len = 0; w->calc_expr[0] = 0; }
                        else if (ch == '=') calc_eval_win(w);
                        else if (w->calc_len < 62) { w->calc_expr[w->calc_len++] = ch; w->calc_expr[w->calc_len] = 0; }
                    }
                }
            }
        } else if (w->app_id == 2 || w->app_id == 3) {
            int rx = w->x + w->w - 90, ry = w->y + 38;
            if (mx >= rx && mx <= rx + 78 && my >= ry && my <= ry + 26 && w->http.done) {
                wm_start_http(w, w->http.url);
            }
        } else if (w->app_id == 9) {
            int lx = w->x + 18, ly = w->y + 46;
            for (int i = 0; i < 6; i++) {
                int ry = ly + i * 38;
                if (mx >= lx && mx <= lx + 310 && my >= ry && my <= ry + 32) {
                    if (w->gr_qty[i] < 9) w->gr_qty[i]++;
                    w->gr_sel = i;
                    snprintf(w->gr_status, sizeof(w->gr_status), "Added item %d", i + 1);
                }
            }
            int sx = w->x + 350;
            if (mx >= sx + 18 && mx <= sx + 98 && my >= ly + 158 && my <= ly + 188) {
                snprintf(w->gr_status, sizeof(w->gr_status), "Paid! Collect order");
            }
            if (mx >= sx + 108 && mx <= sx + 188 && my >= ly + 158 && my <= ly + 188) {
                for (int i = 0; i < 6; i++) w->gr_qty[i] = 0;
                w->gr_sel = -1;
                snprintf(w->gr_status, sizeof(w->gr_status), "Cart cleared");
            }
        } else if (w->app_id == 11) {
            int by = w->y + w->h - 42;
            if (mx >= w->x + 14 && mx <= w->x + 110 && my >= by && my <= by + 28) w->sdl_paused = !w->sdl_paused;
            if (mx >= w->x + 120 && mx <= w->x + 216 && my >= by && my <= by + 28) w->sdl_grid = !w->sdl_grid;
        } else if (w->app_id == 12) {
            int by = w->y + w->h - 42;
            if (mx >= w->x + 14 && mx <= w->x + 170 && my >= by && my <= by + 28) {
                worker_submit("compute-job", wm_sample_job, (void *)(uintptr_t)25000);
                worker_submit("compute-job", wm_sample_job, (void *)(uintptr_t)25000);
            }
        } else if (w->app_id == 13) {
            billing_handle_mouse(w->x, w->y, w->w, w->h, mx, my);
        }

        return 1;
    }

    return 0;
}

int wm_handle_key(char key) {
    if (wm_count <= 0) return 0;
    wm_win_t *w = &wm_wins[wm_z[wm_count - 1]];
    if (!w->active) return 0;

    if (w->app_id == 1) {
        if ((key >= '0' && key <= '9') || key == '+' || key == '-' || key == '*' || key == '/') {
            if (w->calc_len < 62) { w->calc_expr[w->calc_len++] = key; w->calc_expr[w->calc_len] = 0; }
            return 1;
        }
        if (key == '\n' || key == '=') { calc_eval_win(w); return 1; }
        if (key == 'c' || key == 'C' || key == '\b') { w->calc_len = 0; w->calc_expr[0] = 0; return 1; }
    } else if (w->app_id == 4) {
        if (key == '\b') {
            if (w->ed_len > 0) w->ed_text[--w->ed_len] = '\0';
            return 1;
        }
        if (w->ed_len < (int)sizeof(w->ed_text) - 2) {
            w->ed_text[w->ed_len++] = key;
            w->ed_text[w->ed_len] = '\0';
            return 1;
        }
    } else if (w->app_id == 11) {
        if (key == ' ') { w->sdl_paused = !w->sdl_paused; return 1; }
        if (key == 'g' || key == 'G') { w->sdl_grid = !w->sdl_grid; return 1; }
    } else if (w->app_id == 13) {
        return billing_handle_key(key);
    }
    return 0;
}

int wm_tick(void) {
    int changed = 0;
    wm_tick_cnt++;

    for (int z = 0; z < wm_count; z++) {
        wm_win_t *w = &wm_wins[wm_z[z]];
        if (!w->active) continue;
        if ((w->app_id == 2 || w->app_id == 3) && w->http.done && !w->http_rendered) {
            w->http_rendered = 1;
            changed = 1;
        }
        if (w->app_id == 11 && !w->sdl_paused && (wm_tick_cnt & 0x1F) == 0) {
            int cw = w->w - 24, ch = w->h - 92;
            w->sdl_frame++;
            for (int i = 0; i < 4; i++) {
                w->ox[i] += w->vx[i];
                w->oy[i] += w->vy[i];
                if (w->ox[i] < 28 || w->ox[i] > cw - 28) w->vx[i] = -w->vx[i];
                if (w->oy[i] < 28 || w->oy[i] > ch - 28) w->vy[i] = -w->vy[i];
            }
            changed = 1;
        }
        if (w->app_id == 12 && (wm_tick_cnt & 0x7F) == 0) {
            changed = 1;
        }
        if (w->app_id == 13 && billing_tick()) {
            changed = 1;
        }
    }
    return changed;
}
