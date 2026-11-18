#include "billing.h"
#include "graphics.h"
#include "scheduler.h"
#include "string.h"
#include "stdio.h"
#include "net.h"
#include "dns.h"
#include "tls.h"
#define JSMN_HEADER
#include "json.h"

#define API_HOST "maadurga.itmaity.in"

typedef enum {
    TAB_AUTH = 0,
    TAB_BRANDS,
    TAB_CUSTOMERS,
    TAB_INVOICES,
    TAB_POS
} billing_tab_t;

typedef enum {
    JOB_NONE = 0,
    JOB_LOGIN,
    JOB_FETCH_BRANDS,
    JOB_CREATE_BRAND,
    JOB_FETCH_CUSTOMERS,
    JOB_CREATE_CUSTOMER,
    JOB_FETCH_INVOICES,
    JOB_FETCH_PRODUCTS,
    JOB_CREATE_INVOICE
} billing_job_kind_t;

typedef struct {
    int id;
    char name[48];
    char extra[48];
    int price;
} bill_item_t;

typedef struct {
    int product_id;
    char name[40];
    int price;
    int qty;
} pos_cart_line_t;

/* Auth & Session */
static billing_tab_t cur_tab = TAB_AUTH;
static char auth_login[64] = "";
static char auth_pass[64]  = "";
static int  auth_focus = 0; /* 0 = login, 1 = password */
static char auth_token[256] = "";
static char auth_user[64]   = "Guest";
static int  is_authed = 0;

/* Input fields for Brands / Customers / POS */
static char brand_input[48] = "";
static char cust_name_in[48] = "";
static char cust_phone_in[32] = "";
static int  cust_focus = 0; /* 0 = name, 1 = phone */

/* Cached API Lists */
#define MAX_LIST_ITEMS 12
static bill_item_t brands[MAX_LIST_ITEMS];
static int brand_count = 0;

static bill_item_t customers[MAX_LIST_ITEMS];
static int customer_count = 0;
static int selected_cust_idx = 0;

static bill_item_t invoices[MAX_LIST_ITEMS];
static int invoice_count = 0;
static int selected_inv_idx = -1;

static bill_item_t products[MAX_LIST_ITEMS];
static int product_count = 0;

/* POS Cart */
#define MAX_CART_LINES 8
static pos_cart_line_t cart[MAX_CART_LINES];
static int cart_count = 0;
static int pay_mode = 0; /* 0=Cash, 1=UPI, 2=Card */

/* Async Worker Job State */
static char status_msg[128] = "Sign in to maadurga.itmaity.in/api";
static char raw_resp[8192];
static volatile int job_busy = 0;
static volatile int job_done = 0;
static billing_job_kind_t job_kind = JOB_NONE;
static char job_method[8];
static char job_path[128];
static char job_body[1024];
static int  job_ret = 0;
static uint32_t cached_api_ip = 0;

static void json_copy_tok(const char *js, const jsmntok_t *t, char *out, int cap) {
    int len = t->end - t->start;
    if (len < 0) len = 0;
    if (len >= cap) len = cap - 1;
    memcpy(out, js + t->start, len);
    out[len] = '\0';
}

static int json_tok_eq(const char *js, const jsmntok_t *t, const char *s) {
    int len = t->end - t->start;
    return (t->type == JSMN_STRING && (int)strlen(s) == len && strncmp(js + t->start, s, len) == 0);
}

/* Extract a JSON string value by key anywhere in token stream */
static int json_find_key_str(const char *js, const jsmntok_t *t, int n, const char *key, char *out, int cap) {
    for (int i = 0; i < n - 1; i++) {
        if (json_tok_eq(js, &t[i], key) && (t[i + 1].type == JSMN_STRING || t[i + 1].type == JSMN_PRIMITIVE)) {
            json_copy_tok(js, &t[i + 1], out, cap);
            return 1;
        }
    }
    return 0;
}

/* Parse an array of objects (either root [...] or {"data":[...]}) into bill_item_t[] */
static int parse_item_list(const char *js, bill_item_t *out, int max_items, const char *extra_key, const char *price_key) {
    jsmn_parser p;
    static jsmntok_t toks[512];
    jsmn_init(&p);
    int r = jsmn_parse(&p, js, strlen(js), toks, 512);
    if (r <= 0) return 0;

    int count = 0;
    for (int i = 0; i < r && count < max_items; i++) {
        if (toks[i].type != JSMN_OBJECT) continue;
        /* Check if this object has a name/invoice_number/id field inside its direct children */
        int obj_end = toks[i].end;
        bill_item_t item;
        memset(&item, 0, sizeof(item));
        int has_label = 0;

        for (int j = i + 1; j < r && toks[j].start < obj_end; j++) {
            if (toks[j].type != JSMN_STRING) continue;
            if (j + 1 >= r) break;
            const jsmntok_t *val = &toks[j + 1];
            if (val->type == JSMN_OBJECT || val->type == JSMN_ARRAY) {
                int sub_end = val->end;
                j++;
                while (j + 1 < r && toks[j + 1].start < sub_end) j++;
                continue;
            }
            if (json_tok_eq(js, &toks[j], "id")) {
                char buf[16];
                json_copy_tok(js, val, buf, sizeof(buf));
                item.id = atoi(buf);
            } else if (json_tok_eq(js, &toks[j], "name") ||
                       json_tok_eq(js, &toks[j], "title") ||
                       json_tok_eq(js, &toks[j], "invoice_number")) {
                json_copy_tok(js, val, item.name, sizeof(item.name));
                has_label = 1;
            } else if (extra_key && json_tok_eq(js, &toks[j], extra_key)) {
                json_copy_tok(js, val, item.extra, sizeof(item.extra));
            } else if (price_key && json_tok_eq(js, &toks[j], price_key)) {
                char buf[24];
                json_copy_tok(js, val, buf, sizeof(buf));
                item.price = atoi(buf);
            }
            j++;
        }
        if (has_label) {
            out[count++] = item;
        }
    }
    return count;
}

static void billing_api_worker(void *arg) {
    (void)arg;
    for (int w = 0; w < 3000 && !net_has_ipv4(); w++) {
        net_poll();
    }
    if (cached_api_ip == 0) {
        if (!dns_resolve(API_HOST, &cached_api_ip)) {
            job_ret = -100;
            job_done = 1;
            return;
        }
    }
    raw_resp[0] = '\0';
    job_ret = tls_http_request_by_ip(cached_api_ip, job_method, API_HOST, job_path,
                                     is_authed ? auth_token : NULL,
                                     job_body[0] ? job_body : NULL,
                                     raw_resp, (int)sizeof(raw_resp));
    job_done = 1;
}

static void billing_start_job(billing_job_kind_t kind, const char *method, const char *path, const char *body, const char *msg) {
    if (job_busy) return;
    job_kind = kind;
    strncpy(job_method, method, sizeof(job_method) - 1);
    strncpy(job_path, path, sizeof(job_path) - 1);
    if (body) strncpy(job_body, body, sizeof(job_body) - 1);
    else job_body[0] = '\0';
    job_ret = 0;
    job_done = 0;
    job_busy = 1;
    snprintf(status_msg, sizeof(status_msg), "%s", msg);
    worker_submit("billing-api", billing_api_worker, NULL);
}

void billing_init(void) {
    cur_tab = TAB_AUTH;
    product_count = 0;
    brand_count = 0;
    customer_count = 0;
    invoice_count = 0;
}

static void process_job_result(void) {
    job_busy = 0;
    job_done = 0;

    if (job_ret <= 0) {
        snprintf(status_msg, sizeof(status_msg), "API error (%d) - check network/auth", job_ret);
        return;
    }

    jsmn_parser p;
    static jsmntok_t toks[512];
    jsmn_init(&p);
    int n = jsmn_parse(&p, raw_resp, strlen(raw_resp), toks, 512);

    if (job_kind == JOB_LOGIN) {
        char tok_buf[256] = {0};
        if (n > 0 && (json_find_key_str(raw_resp, toks, n, "token", tok_buf, sizeof(tok_buf)) ||
                      json_find_key_str(raw_resp, toks, n, "access_token", tok_buf, sizeof(tok_buf)))) {
            strncpy(auth_token, tok_buf, sizeof(auth_token) - 1);
            is_authed = 1;
            if (!json_find_key_str(raw_resp, toks, n, "name", auth_user, sizeof(auth_user))) {
                snprintf(auth_user, sizeof(auth_user), "%s", auth_login[0] ? auth_login : "User");
            }
            snprintf(status_msg, sizeof(status_msg), "Logged in as %s", auth_user);
            cur_tab = TAB_POS;
            billing_start_job(JOB_FETCH_PRODUCTS, "GET", "/api/products", NULL, "Loading products...");
        } else {
            char err[96] = {0};
            if (n > 0 && json_find_key_str(raw_resp, toks, n, "message", err, sizeof(err))) {
                snprintf(status_msg, sizeof(status_msg), "Auth: %s", err);
            } else {
                snprintf(status_msg, sizeof(status_msg), "Login failed (check credentials)");
            }
        }
    } else if (job_kind == JOB_FETCH_BRANDS) {
        brand_count = parse_item_list(raw_resp, brands, MAX_LIST_ITEMS, "slug", NULL);
        snprintf(status_msg, sizeof(status_msg), "Loaded %d brands", brand_count);
    } else if (job_kind == JOB_CREATE_BRAND) {
        brand_input[0] = '\0';
        billing_start_job(JOB_FETCH_BRANDS, "GET", "/api/brands", NULL, "Brand created! Refreshing...");
    } else if (job_kind == JOB_FETCH_CUSTOMERS) {
        customer_count = parse_item_list(raw_resp, customers, MAX_LIST_ITEMS, "phone", NULL);
        snprintf(status_msg, sizeof(status_msg), "Loaded %d customers", customer_count);
    } else if (job_kind == JOB_CREATE_CUSTOMER) {
        cust_name_in[0] = '\0';
        cust_phone_in[0] = '\0';
        billing_start_job(JOB_FETCH_CUSTOMERS, "GET", "/api/customers", NULL, "Customer added! Refreshing...");
    } else if (job_kind == JOB_FETCH_INVOICES) {
        invoice_count = parse_item_list(raw_resp, invoices, MAX_LIST_ITEMS, "status", "total");
        snprintf(status_msg, sizeof(status_msg), "Loaded %d invoices", invoice_count);
    } else if (job_kind == JOB_FETCH_PRODUCTS) {
        product_count = parse_item_list(raw_resp, products, MAX_LIST_ITEMS, "sku", "price");
        snprintf(status_msg, sizeof(status_msg), "Loaded %d products from API", product_count);
    } else if (job_kind == JOB_CREATE_INVOICE) {
        cart_count = 0;
        snprintf(status_msg, sizeof(status_msg), "POS Invoice submitted to API!");
        cur_tab = TAB_INVOICES;
        billing_start_job(JOB_FETCH_INVOICES, "GET", "/api/invoices", NULL, "Invoice saved! Loading list...");
    }
}

int billing_tick(void) {
    if (job_busy && job_done) {
        process_job_result();
        return 1;
    }
    return 0;
}

static void draw_field(int x, int y, int w, int h, const char *val, int focused, int masked) {
    draw_rounded_rect(x, y, w, h, 6, 0xFFFFFFFF);
    draw_rounded_outline(x, y, w, h, 6, 1, focused ? 0xFF0284C7 : 0xFF94A3B8);
    if (masked) {
        char stars[64];
        int len = (int)strlen(val);
        if (len > 63) len = 63;
        for (int i = 0; i < len; i++) stars[i] = '*';
        stars[len] = '\0';
        draw_string(x + 10, y + (h - 8) / 2, stars, 0x0F172A);
    } else {
        draw_string(x + 10, y + (h - 8) / 2, val, 0x0F172A);
    }
}

void billing_draw(int wx, int wy, int ww, int wh) {
    /* Top Navigation Tabs */
    const char *tabs[5] = { "Auth", "Brands", "Customers", "Invoices", "POS Invoice" };
    int tx = wx + 14, ty = wy + 38, tw = 112, th = 28;
    for (int i = 0; i < 5; i++) {
        int active = (cur_tab == (billing_tab_t)i);
        draw_rounded_rect(tx + i * (tw + 6), ty, tw, th, 7, active ? 0xFF0284C7 : 0xFFE2E8F0);
        draw_string(tx + i * (tw + 6) + 12, ty + 10, tabs[i], active ? 0xFFFFFF : 0x0F172A);
    }

    /* Auth badge */
    draw_rounded_rect(wx + ww - 140, ty, 126, th, 7, is_authed ? 0xFF16A34A : 0xFFDC2626);
    draw_string(wx + ww - 128, ty + 10, is_authed ? "API: Online" : "API: Locked", 0xFFFFFF);

    int cx = wx + 14, cy = wy + 74, cw = ww - 28, ch = wh - 118;
    draw_rounded_rect(cx, cy, cw, ch, 8, 0xFFFFFFFF);
    draw_rounded_outline(cx, cy, cw, ch, 8, 1, 0xFFCBD5E1);

    if (cur_tab == TAB_AUTH) {
        draw_string(cx + 24, cy + 22, "MaaDurga Billing  -  Authentication (/api/login)", 0x0F172A);
        draw_string(cx + 24, cy + 56, "Login (Email / Username):", 0x334155);
        draw_field(cx + 24, cy + 74, 320, 32, auth_login, auth_focus == 0, 0);

        draw_string(cx + 24, cy + 122, "Password:", 0x334155);
        draw_field(cx + 24, cy + 140, 320, 32, auth_pass, auth_focus == 1, 1);

        draw_rounded_rect(cx + 24, cy + 190, 120, 34, 8, 0xFF0284C7);
        draw_string(cx + 50, cy + 203, "Sign In", 0xFFFFFF);

        if (is_authed) {
            draw_rounded_rect(cx + 160, cy + 190, 110, 34, 8, 0xFFDC2626);
            draw_string(cx + 186, cy + 203, "Logout", 0xFFFFFF);
        }

        draw_rounded_rect(cx + 380, cy + 56, cw - 404, 170, 10, 0xFFF1F5F9);
        draw_string(cx + 398, cy + 76, "Endpoint: https://maadurga.itmaity.in/api", 0x0284C7);
        draw_string(cx + 398, cy + 104, is_authed ? "Session: Bearer Token Active" : "Session: Not signed in", is_authed ? 0x16A34A : 0x64748B);
        draw_string(cx + 398, cy + 132, "Click a field to type. Tab switches field.", 0x475569);
        draw_string(cx + 398, cy + 156, "Tabs: Brands, Customers, Invoices, POS", 0x475569);
    } else if (cur_tab == TAB_BRANDS) {
        draw_string(cx + 18, cy + 16, "Brands (/api/brands)", 0x0F172A);
        draw_rounded_rect(cx + cw - 110, cy + 10, 94, 28, 6, 0xFF0284C7);
        draw_string(cx + cw - 92, cy + 20, "Refresh", 0xFFFFFF);

        draw_string(cx + 18, cy + 54, "New Brand:", 0x334155);
        draw_field(cx + 110, cy + 46, 260, 30, brand_input, 1, 0);
        draw_rounded_rect(cx + 382, cy + 46, 100, 30, 6, 0xFF16A34A);
        draw_string(cx + 404, cy + 57, "Create", 0xFFFFFF);

        for (int i = 0; i < brand_count && i < 8; i++) {
            int ry = cy + 90 + i * 28;
            draw_rect(cx + 18, ry, cw - 36, 24, (i % 2) ? 0xF8FAFC : 0xE2E8F0);
            char row[96];
            snprintf(row, sizeof(row), "#%d  %s", brands[i].id, brands[i].name);
            draw_string(cx + 28, ry + 8, row, 0x0F172A);
        }
    } else if (cur_tab == TAB_CUSTOMERS) {
        draw_string(cx + 18, cy + 16, "Customers (/api/customers)", 0x0F172A);
        draw_rounded_rect(cx + cw - 110, cy + 10, 94, 28, 6, 0xFF0284C7);
        draw_string(cx + cw - 92, cy + 20, "Refresh", 0xFFFFFF);

        draw_string(cx + 18, cy + 54, "Name:", 0x334155);
        draw_field(cx + 70, cy + 46, 180, 30, cust_name_in, cust_focus == 0, 0);
        draw_string(cx + 262, cy + 54, "Phone:", 0x334155);
        draw_field(cx + 320, cy + 46, 150, 30, cust_phone_in, cust_focus == 1, 0);
        draw_rounded_rect(cx + 482, cy + 46, 90, 30, 6, 0xFF16A34A);
        draw_string(cx + 502, cy + 57, "Add", 0xFFFFFF);

        for (int i = 0; i < customer_count && i < 8; i++) {
            int ry = cy + 90 + i * 28;
            draw_rect(cx + 18, ry, cw - 36, 24, (i % 2) ? 0xF8FAFC : 0xE2E8F0);
            char row[112];
            snprintf(row, sizeof(row), "#%d  %s   (%s)", customers[i].id, customers[i].name, customers[i].extra);
            draw_string(cx + 28, ry + 8, row, 0x0F172A);
        }
    } else if (cur_tab == TAB_INVOICES) {
        draw_string(cx + 18, cy + 16, "Invoices (/api/invoices)", 0x0F172A);
        draw_rounded_rect(cx + cw - 110, cy + 10, 94, 28, 6, 0xFF0284C7);
        draw_string(cx + cw - 92, cy + 20, "Refresh", 0xFFFFFF);

        for (int i = 0; i < invoice_count && i < 9; i++) {
            int ry = cy + 50 + i * 28;
            u32 bg = (i == selected_inv_idx) ? 0xBAE6FD : ((i % 2) ? 0xF8FAFC : 0xE2E8F0);
            draw_rect(cx + 18, ry, cw - 36, 24, bg);
            char row[112];
            snprintf(row, sizeof(row), "#%d  %s   Total: Rs %d   [%s]",
                     invoices[i].id, invoices[i].name, invoices[i].price,
                     invoices[i].extra[0] ? invoices[i].extra : "OK");
            draw_string(cx + 28, ry + 8, row, 0x0F172A);
        }
    } else if (cur_tab == TAB_POS) {
        draw_string(cx + 16, cy + 14, "POS Invoice Builder (Click product to add)", 0x0F172A);
        draw_rounded_rect(cx + cw - 110, cy + 8, 94, 26, 6, 0xFF0284C7);
        draw_string(cx + cw - 92, cy + 17, "Sync", 0xFFFFFF);

        /* Left: Product Catalog (from /api/products) */
        if (product_count == 0) {
            draw_string(cx + 18, cy + 56, is_authed ? "No products loaded. Click Sync." : "Sign in first, then click Sync.", 0x64748B);
        }
        for (int i = 0; i < product_count && i < 7; i++) {
            int ry = cy + 44 + i * 36;
            draw_rounded_rect(cx + 16, ry, 340, 30, 6, 0xFFE2E8F0);
            char line[80];
            snprintf(line, sizeof(line), "%s  -  Rs %d", products[i].name, products[i].price);
            draw_string(cx + 26, ry + 11, line, 0x0F172A);
        }

        /* Right: Cart & Checkout */
        int rx = cx + 372, rw = cw - 388;
        draw_rounded_rect(rx, cy + 44, rw, ch - 56, 8, 0xFF0F172A);
        draw_string(rx + 14, cy + 58, "POS Cart", 0x38BDF8);

        int subtotal = 0;
        for (int i = 0; i < cart_count && i < 6; i++) {
            int ry = cy + 82 + i * 24;
            int lt = cart[i].price * cart[i].qty;
            subtotal += lt;
            char cl[64];
            snprintf(cl, sizeof(cl), "%s x%d = Rs %d", cart[i].name, cart[i].qty, lt);
            draw_string(rx + 14, ry, cl, 0xF8FAFC);
        }

        const char *pm[3] = { "Cash", "UPI", "Card" };
        char tot[64];
        snprintf(tot, sizeof(tot), "Grand Total: Rs %d", subtotal);
        draw_string(rx + 14, cy + ch - 84, tot, 0x4ADE80);

        draw_rounded_rect(rx + 14, cy + ch - 54, 86, 30, 6, 0xFF334155);
        draw_string(rx + 26, cy + ch - 43, pm[pay_mode], 0xFFFFFF);

        draw_rounded_rect(rx + 110, cy + ch - 54, 150, 30, 6, 0xFF16A34A);
        draw_string(rx + 124, cy + ch - 43, "Submit Invoice", 0xFFFFFF);

        draw_rounded_rect(rx + 270, cy + ch - 54, 70, 30, 6, 0xFFDC2626);
        draw_string(rx + 286, cy + ch - 43, "Clear", 0xFFFFFF);
    }

    /* Bottom Status Bar */
    draw_rounded_rect(wx + 14, wy + wh - 36, ww - 28, 24, 5, 0xFF1E293B);
    draw_string(wx + 24, wy + wh - 28, status_msg, job_busy ? 0x38BDF8 : 0xE2E8F0);
}

int billing_handle_mouse(int wx, int wy, int ww, int wh, int mx, int my) {
    /* Tab clicks */
    int tx = wx + 14, ty = wy + 38, tw = 112, th = 28;
    for (int i = 0; i < 5; i++) {
        int bx = tx + i * (tw + 6);
        if (mx >= bx && mx <= bx + tw && my >= ty && my <= ty + th) {
            cur_tab = (billing_tab_t)i;
            if (is_authed && !job_busy) {
                if (cur_tab == TAB_BRANDS && brand_count == 0)
                    billing_start_job(JOB_FETCH_BRANDS, "GET", "/api/brands", NULL, "Loading brands...");
                else if (cur_tab == TAB_CUSTOMERS && customer_count == 0)
                    billing_start_job(JOB_FETCH_CUSTOMERS, "GET", "/api/customers", NULL, "Loading customers...");
                else if (cur_tab == TAB_INVOICES && invoice_count == 0)
                    billing_start_job(JOB_FETCH_INVOICES, "GET", "/api/invoices", NULL, "Loading invoices...");
                else if (cur_tab == TAB_POS && product_count == 0)
                    billing_start_job(JOB_FETCH_PRODUCTS, "GET", "/api/products", NULL, "Loading products...");
            }
            return 1;
        }
    }

    int cx = wx + 14, cy = wy + 74, cw = ww - 28, ch = wh - 118;
    if (cur_tab == TAB_AUTH) {
        if (mx >= cx + 24 && mx <= cx + 344 && my >= cy + 74 && my <= cy + 106) { auth_focus = 0; return 1; }
        if (mx >= cx + 24 && mx <= cx + 344 && my >= cy + 140 && my <= cy + 172) { auth_focus = 1; return 1; }
        if (mx >= cx + 24 && mx <= cx + 144 && my >= cy + 190 && my <= cy + 224) {
            char body[256];
            snprintf(body, sizeof(body), "{\"login\":\"%s\",\"password\":\"%s\"}", auth_login, auth_pass);
            billing_start_job(JOB_LOGIN, "POST", "/api/login", body, "Signing in...");
            return 1;
        }
        if (is_authed && mx >= cx + 160 && mx <= cx + 270 && my >= cy + 190 && my <= cy + 224) {
            is_authed = 0;
            auth_token[0] = '\0';
            snprintf(status_msg, sizeof(status_msg), "Logged out");
            return 1;
        }
    } else if (cur_tab == TAB_BRANDS) {
        if (mx >= cx + cw - 110 && mx <= cx + cw - 16 && my >= cy + 10 && my <= cy + 38) {
            billing_start_job(JOB_FETCH_BRANDS, "GET", "/api/brands", NULL, "Loading brands...");
            return 1;
        }
        if (mx >= cx + 382 && mx <= cx + 482 && my >= cy + 46 && my <= cy + 76 && brand_input[0]) {
            char body[128];
            snprintf(body, sizeof(body), "{\"name\":\"%s\"}", brand_input);
            billing_start_job(JOB_CREATE_BRAND, "POST", "/api/brands", body, "Creating brand...");
            return 1;
        }
    } else if (cur_tab == TAB_CUSTOMERS) {
        if (mx >= cx + cw - 110 && mx <= cx + cw - 16 && my >= cy + 10 && my <= cy + 38) {
            billing_start_job(JOB_FETCH_CUSTOMERS, "GET", "/api/customers", NULL, "Loading customers...");
            return 1;
        }
        if (mx >= cx + 70 && mx <= cx + 250 && my >= cy + 46 && my <= cy + 76) { cust_focus = 0; return 1; }
        if (mx >= cx + 320 && mx <= cx + 470 && my >= cy + 46 && my <= cy + 76) { cust_focus = 1; return 1; }
        if (mx >= cx + 482 && mx <= cx + 572 && my >= cy + 46 && my <= cy + 76 && cust_name_in[0]) {
            char body[192];
            snprintf(body, sizeof(body), "{\"name\":\"%s\",\"phone\":\"%s\"}", cust_name_in, cust_phone_in);
            billing_start_job(JOB_CREATE_CUSTOMER, "POST", "/api/customers", body, "Adding customer...");
            return 1;
        }
    } else if (cur_tab == TAB_INVOICES) {
        if (mx >= cx + cw - 110 && mx <= cx + cw - 16 && my >= cy + 10 && my <= cy + 38) {
            billing_start_job(JOB_FETCH_INVOICES, "GET", "/api/invoices", NULL, "Loading invoices...");
            return 1;
        }
        for (int i = 0; i < invoice_count && i < 9; i++) {
            int ry = cy + 50 + i * 28;
            if (mx >= cx + 18 && mx <= cx + cw - 18 && my >= ry && my <= ry + 24) {
                selected_inv_idx = i;
                return 1;
            }
        }
    } else if (cur_tab == TAB_POS) {
        if (mx >= cx + cw - 110 && mx <= cx + cw - 16 && my >= cy + 8 && my <= cy + 34) {
            billing_start_job(JOB_FETCH_PRODUCTS, "GET", "/api/products", NULL, "Syncing products...");
            return 1;
        }
        for (int i = 0; i < product_count && i < 7; i++) {
            int ry = cy + 44 + i * 36;
            if (mx >= cx + 16 && mx <= cx + 356 && my >= ry && my <= ry + 30) {
                int found = -1;
                for (int c = 0; c < cart_count; c++) {
                    if (cart[c].product_id == products[i].id) { found = c; break; }
                }
                if (found >= 0) {
                    cart[found].qty++;
                } else if (cart_count < MAX_CART_LINES) {
                    cart[cart_count].product_id = products[i].id;
                    strncpy(cart[cart_count].name, products[i].name, 39);
                    cart[cart_count].price = products[i].price;
                    cart[cart_count].qty = 1;
                    cart_count++;
                }
                return 1;
            }
        }
        int rx = cx + 372;
        if (mx >= rx + 14 && mx <= rx + 100 && my >= cy + ch - 54 && my <= cy + ch - 24) {
            pay_mode = (pay_mode + 1) % 3;
            return 1;
        }
        if (mx >= rx + 270 && mx <= rx + 340 && my >= cy + ch - 54 && my <= cy + ch - 24) {
            cart_count = 0;
            snprintf(status_msg, sizeof(status_msg), "POS cart cleared");
            return 1;
        }
        if (mx >= rx + 110 && mx <= rx + 260 && my >= cy + ch - 54 && my <= cy + ch - 24 && cart_count > 0) {
            const char *pm[3] = { "cash", "upi", "card" };
            int cust_id = (customer_count > 0) ? customers[selected_cust_idx].id : 1;
            int total = 0;
            char items_json[512];
            int pos = 0;
            items_json[pos++] = '[';
            for (int i = 0; i < cart_count; i++) {
                total += cart[i].price * cart[i].qty;
                if (i > 0) items_json[pos++] = ',';
                pos += snprintf(items_json + pos, sizeof(items_json) - pos,
                                "{\"product_id\":%d,\"quantity\":%d,\"price\":%d}",
                                cart[i].product_id, cart[i].qty, cart[i].price);
            }
            items_json[pos++] = ']';
            items_json[pos] = '\0';

            char body[768];
            snprintf(body, sizeof(body),
                     "{\"customer_id\":%d,\"payment_method\":\"%s\",\"total\":%d,\"items\":%s}",
                     cust_id, pm[pay_mode], total, items_json);
            billing_start_job(JOB_CREATE_INVOICE, "POST", "/api/invoices", body, "Submitting POS invoice...");
            return 1;
        }
    }
    return 0;
}

static void append_field(char *buf, int cap, char key) {
    int len = (int)strlen(buf);
    if (key == '\b') {
        if (len > 0) buf[len - 1] = '\0';
        return;
    }
    if (key >= 32 && key <= 126 && len < cap - 1) {
        buf[len] = key;
        buf[len + 1] = '\0';
    }
}

int billing_handle_key(char key) {
    if (cur_tab == TAB_AUTH) {
        if (key == '\t') { auth_focus = !auth_focus; return 1; }
        if (key == '\n') {
            char body[256];
            snprintf(body, sizeof(body), "{\"login\":\"%s\",\"password\":\"%s\"}", auth_login, auth_pass);
            billing_start_job(JOB_LOGIN, "POST", "/api/login", body, "Signing in...");
            return 1;
        }
        append_field(auth_focus == 0 ? auth_login : auth_pass, 64, key);
        return 1;
    }
    if (cur_tab == TAB_BRANDS) {
        if (key == '\n' && brand_input[0]) {
            char body[128];
            snprintf(body, sizeof(body), "{\"name\":\"%s\"}", brand_input);
            billing_start_job(JOB_CREATE_BRAND, "POST", "/api/brands", body, "Creating brand...");
            return 1;
        }
        append_field(brand_input, sizeof(brand_input), key);
        return 1;
    }
    if (cur_tab == TAB_CUSTOMERS) {
        if (key == '\t') { cust_focus = !cust_focus; return 1; }
        append_field(cust_focus == 0 ? cust_name_in : cust_phone_in, cust_focus == 0 ? 48 : 32, key);
        return 1;
    }
    return 0;
}
