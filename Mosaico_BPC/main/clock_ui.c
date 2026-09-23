#include "clock_app.h"
#include "bsp/esp_mosaico.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>

typedef enum { PAGE_HOME, PAGE_SETTINGS, PAGE_WLAN, PAGE_PASSWORD, PAGE_GPIO, PAGE_PROTOCOL, PAGE_OUTPUT } page_t;
static page_t page;
static lv_obj_t *root, *time_label, *date_label, *output_label;
static lv_obj_t *net_label, *state_label, *tx_button_text, *ap_list, *scan_button;
static lv_obj_t *input, *keyboard, *form_status, *drive_select;
static clock_network_state_t network;
static clock_ap_t selected_ap;
static unsigned shown_revision = ~0U;
static const uint32_t BG = 0x101721, FG = 0xeaf1f8, MUTED = 0x8e9aaa, GREEN = 0x59deb3, ORANGE = 0xffb567, RED = 0xff626b;
static void show_page(page_t target);

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y); lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                        lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x263748), 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_label_set_text(l, text); lv_obj_center(l);
    return b;
}
static void navigate(lv_event_t *e) { show_page((page_t)(intptr_t)lv_event_get_user_data(e)); }
static void toggle_tx(lv_event_t *e)
{
    (void)e;
    if (!atomic_load(&tx_error)) atomic_store(&tx_requested, !atomic_load(&tx_requested));
}
static void report_error(esp_err_t err)
{
    lv_label_set_text_fmt(form_status, "Could not apply: 0x%x", err);
}
static void apply_gpio(lv_event_t *e)
{
    (void)e;
    const char *text = lv_textarea_get_text(input);
    char *end;
    long pin = strtol(text, &end, 10);
    if (!text[0] || *end || pin < 0 || pin > 63 || !clock_output_pin_valid((int)pin)) {
        lv_label_set_text(form_status, "Use one of the listed expansion GPIOs");
        return;
    }
    esp_err_t err = clock_set_output((int)pin, atomic_load(&output_drive));
    if (err == ESP_OK) show_page(PAGE_OUTPUT); else report_error(err);
}
static void apply_output(lv_event_t *e)
{
    (void)e;
    esp_err_t err = clock_set_output(atomic_load(&output_gpio), lv_dropdown_get_selected(drive_select));
    if (err == ESP_OK) lv_label_set_text(form_status, "Saved. TX will restart on a new frame.");
    else report_error(err);
}
static void scan(lv_event_t *e)
{
    (void)e;
    esp_err_t err = clock_network_scan();
    if (err != ESP_OK) lv_label_set_text_fmt(state_label, "Scan unavailable: 0x%x", err);
    else lv_label_set_text(state_label, "Scan requested...");
}
static void select_ap(lv_event_t *e)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (index >= network.count || !network.aps[index].supported) return;
    selected_ap = network.aps[index];
    show_page(PAGE_PASSWORD);
}
static void connect_ap(lv_event_t *e)
{
    (void)e;
    const char *password = selected_ap.open ? "" : lv_textarea_get_text(input);
    esp_err_t err = clock_network_connect(selected_ap.ssid, password);
    if (err == ESP_OK) { if (input) lv_textarea_set_text(input, ""); show_page(PAGE_WLAN); }
    else if (err == ESP_ERR_INVALID_ARG) lv_label_set_text(form_status, "Use 8-63 characters, or 64 hex digits");
    else report_error(err);
}
static void key_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        if (page == PAGE_GPIO) apply_gpio(e); else if (page == PAGE_PASSWORD) connect_ap(e);
    } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
        show_page(page == PAGE_GPIO ? PAGE_OUTPUT : PAGE_WLAN);
    }
}
static void header(const char *title, page_t back)
{
    label(root, title, 24, 24, 320, &lv_font_montserrat_24, FG);
    button(root, 364, 16, 92, 44, "Back", navigate, (void *)(intptr_t)back);
}
static void input_changed(lv_event_t *e)
{
    (void)e;
    if (form_status) lv_label_set_text(form_status, page == PAGE_GPIO ?
        "Antenna wire must match the selected pin" : "Saved after a successful connection");
}
static void make_input(bool password)
{
    input = lv_textarea_create(root);
    lv_obj_set_pos(input, 24, 123); lv_obj_set_size(input, 432, 52);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, password);
    lv_obj_set_style_text_font(input, &lv_font_montserrat_20, 0);
    lv_textarea_set_max_length(input, password ? 64 : 2);
    lv_obj_add_event_cb(input, input_changed, LV_EVENT_VALUE_CHANGED, NULL);
    keyboard = lv_keyboard_create(root);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 0, 265); lv_obj_set_size(keyboard, 480, 215);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_16, 0);
    lv_keyboard_set_textarea(keyboard, input);
    lv_obj_add_event_cb(keyboard, key_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, key_event, LV_EVENT_CANCEL, NULL);
}
static void fill_networks(void)
{
    lv_obj_clean(ap_list);
    for (unsigned i = 0; i < network.count; ++i) {
        clock_ap_t *ap = &network.aps[i];
        lv_obj_t *b = button(ap_list, 0, (int)i * 64, 410, 58, "", select_ap, (void *)(uintptr_t)i);
        lv_obj_clean(b);
        lv_obj_t *name = label(b, ap->ssid, 8, 0, 302, &lv_font_montserrat_20, FG);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        char detail[50];
        snprintf(detail, sizeof(detail), "%s  /  %d dBm", !ap->supported ? "Unsupported" : ap->open ? "Open" : "Secured", ap->rssi);
        label(b, detail, 8, 26, 370, &lv_font_montserrat_16, MUTED);
        if (!ap->supported) lv_obj_add_state(b, LV_STATE_DISABLED);
    }
    if (!network.count) label(ap_list, "Tap Scan to find nearby networks", 4, 8, 390, &lv_font_montserrat_16, MUTED);
    shown_revision = network.revision;
}
static void show_page(page_t target)
{
    // All navigation and refresh happen under the BSP LVGL lock.
    page = target;
    input = keyboard = form_status = NULL;
    lv_obj_clean(root);
    switch (page) {
    case PAGE_HOME: {
        label(root, "BPC CLOCK", 24, 24, 432, &lv_font_montserrat_24, FG);
        output_label = label(root, "", 24, 65, 432, &lv_font_montserrat_16, MUTED);
        time_label = label(root, "--:--:--", 24, 157, 432, &lv_font_montserrat_40, FG);
        date_label = label(root, "", 24, 212, 432, &lv_font_montserrat_20, MUTED);
        state_label = label(root, "", 24, 327, 432, &lv_font_montserrat_16, GREEN);
        lv_obj_t *b = button(root, 24, 397, 204, 52, "STOP TX", toggle_tx, NULL);
        tx_button_text = lv_obj_get_child(b, 0);
        button(root, 252, 397, 204, 52, "Setting", navigate, (void *)PAGE_SETTINGS);
        break;
    }
    case PAGE_SETTINGS:
        header("Setting", PAGE_HOME);
        button(root, 24, 88, 432, 65, "WLAN", navigate, (void *)PAGE_WLAN);
        button(root, 24, 185, 432, 65, "Protocol", navigate, (void *)PAGE_PROTOCOL);
        button(root, 24, 282, 432, 65, "Output", navigate, (void *)PAGE_OUTPUT);
        label(root, "Settings are saved on this device", 24, 425, 432, &lv_font_montserrat_16, MUTED);
        break;
    case PAGE_GPIO: {
        header("Antenna GPIO", PAGE_OUTPUT);
        label(root, "4,5,10,11,12,13,14,15,16,17,18,19,37,38,39,40,"
                    "46,47,48,49,52,53,54,55",
              24, 77, 432, &lv_font_montserrat_16, MUTED);
        make_input(false);
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_textarea_set_accepted_chars(input, "0123456789");
        char value[8]; snprintf(value, sizeof(value), "%d", atomic_load(&output_gpio));
        lv_textarea_set_text(input, value);
        form_status = label(root, "Antenna wire must match the selected pin", 24, 181, 432, &lv_font_montserrat_16, MUTED);
        button(root, 24, 214, 432, 42, "Apply GPIO", apply_gpio, NULL);
        break;
    }
    case PAGE_PROTOCOL: {
        header("Protocol", PAGE_SETTINGS);
        label(root, "Only BPC is available in this firmware", 24, 80, 432, &lv_font_montserrat_16, MUTED);
        const char *names[] = {"BPC / China / 68.5 kHz  [Selected]", "WWVB / USA / 60 kHz", "DCF77 / Europe / 77.5 kHz", "MSF / UK / 60 kHz", "JJY / Japan / 40 & 60 kHz"};
        for (unsigned i = 0; i < 5; ++i) {
            lv_obj_t *b = button(root, 24, 119 + (int)i * 62, 432, 52, names[i], NULL, NULL);
            if (i) lv_obj_add_state(b, LV_STATE_DISABLED);
            else lv_obj_set_style_bg_color(b, lv_color_hex(0x216553), 0);
        }
        break;
    }
    case PAGE_OUTPUT: {
        header("Output", PAGE_SETTINGS);
        label(root, "Antenna GPIO", 24, 89, 200, &lv_font_montserrat_20, FG);
        char gpio_text[24];
        snprintf(gpio_text, sizeof(gpio_text), "GPIO%d  >", atomic_load(&output_gpio));
        button(root, 252, 78, 204, 48, gpio_text, navigate, (void *)PAGE_GPIO);
        label(root, "Drive strength", 24, 159, 220, &lv_font_montserrat_20, FG);
        drive_select = lv_dropdown_create(root);
        lv_obj_set_pos(drive_select, 252, 148); lv_obj_set_size(drive_select, 204, 48);
        lv_dropdown_set_options(drive_select, "0 / Weak\n1 / Medium\n2 / Normal\n3 / Strong");
        lv_dropdown_set_selected(drive_select, atomic_load(&output_drive));
        label(root, "Use an unused expansion pin. Move the\nantenna wire to match your selection.", 24, 231, 432, &lv_font_montserrat_16, MUTED);
        label(root, "Drive strength is not calibrated RF power.\nOutput remains a 3.3 V logic signal.", 24, 293, 432, &lv_font_montserrat_16, MUTED);
        form_status = label(root, "", 24, 350, 432, &lv_font_montserrat_16, GREEN);
        button(root, 24, 397, 432, 52, "Save output", apply_output, NULL);
        break;
    }
    case PAGE_WLAN:
        header("WLAN", PAGE_SETTINGS);
        scan_button = button(root, 328, 75, 128, 44, "Scan", scan, NULL);
        net_label = label(root, "", 24, 81, 294, &lv_font_montserrat_16, MUTED);
        lv_label_set_long_mode(net_label, LV_LABEL_LONG_DOT);
        ap_list = lv_obj_create(root);
        lv_obj_set_pos(ap_list, 24, 134); lv_obj_set_size(ap_list, 432, 279);
        lv_obj_set_style_pad_all(ap_list, 0, 0); lv_obj_set_style_border_width(ap_list, 0, 0);
        lv_obj_set_style_bg_opa(ap_list, LV_OPA_TRANSP, 0);
        lv_obj_set_scroll_dir(ap_list, LV_DIR_VER);
        state_label = label(root, "", 24, 426, 432, &lv_font_montserrat_16, GREEN);
        clock_network_state(&network); fill_networks();
        break;
    case PAGE_PASSWORD:
        header("Connect WLAN", PAGE_WLAN);
        label(root, selected_ap.ssid, 24, 79, 432, &lv_font_montserrat_20, FG);
        if (!selected_ap.open) {
            make_input(true); lv_textarea_set_placeholder_text(input, "Password");
        } else label(root, "Open network / no password required", 24, 134, 432, &lv_font_montserrat_16, MUTED);
        form_status = label(root, "Saved after a successful connection", 24, 184, 432, &lv_font_montserrat_16, MUTED);
        button(root, 24, 214, 432, 42, "Connect & save", connect_ap, NULL);
        break;
    }
}
esp_err_t clock_ui_start(void)
{
    bsp_display_config_t cfg = BSP_DISPLAY_DEFAULT_CONFIG(); cfg.enable_touch = true;
    if (!bsp_display_start_with_config(&cfg)) return ESP_FAIL;
    esp_err_t err = bsp_display_brightness_set(75);
    if (err != ESP_OK) return err;
    if (!bsp_display_lock(-1)) return ESP_ERR_TIMEOUT;
    root = lv_obj_create(NULL);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(FG), 0);
    show_page(PAGE_HOME); lv_screen_load(root); bsp_display_unlock();
    return ESP_OK;
}
void clock_ui_refresh(void)
{
    if (!bsp_display_lock(50)) return;
    if (page == PAGE_HOME) {
        bool usable; int64_t now = clock_time_us(&usable);
        lv_label_set_text_fmt(output_label, "BPC 68.5 kHz  /  GPIO%d  /  Drive %d", atomic_load(&active_gpio), atomic_load(&active_drive));
        const bool online = atomic_load(&clock_wifi_up);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_40, 0);
        lv_obj_set_style_text_color(time_label, lv_color_hex(usable ? FG : online ? ORANGE : RED), 0);
        if (usable) {
            time_t seconds = now / 1000000 + BPC_TIMEZONE_SECONDS; struct tm t; char s[40];
            gmtime_r(&seconds, &t); strftime(s, sizeof(s), "%H:%M:%S", &t); lv_label_set_text(time_label, s);
            strftime(s, sizeof(s), "%Y-%m-%d  %a", &t); lv_label_set_text(date_label, s);
        } else {
            lv_label_set_text(time_label, online ? "Syncing..." : "No network");
            lv_label_set_text(date_label, "");
        }
        bool requested = atomic_load(&tx_requested), active = atomic_load(&tx_active);
        if (atomic_load(&tx_error)) lv_label_set_text_fmt(state_label, "TX error: 0x%x", atomic_load(&tx_error));
        else if (!requested) lv_label_set_text(state_label, active ? "Stopping TX..." : "TX stopped");
        else if (!usable) lv_label_set_text(state_label, "TX paused: time not ready");
        else if (!active) lv_label_set_text(state_label, "TX waiting for :00 / :20 / :40");
        else lv_label_set_text_fmt(state_label, "TX active  /  frame %02lld", (long long)(atomic_load(&tx_frame_utc) % 60));
        lv_label_set_text(tx_button_text, requested ? "STOP TX" : "START TX");
    } else if (page == PAGE_WLAN) {
        clock_network_state(&network);
        lv_label_set_text(net_label, network.connected ? network.ssid : "Select a network");
        lv_label_set_text(state_label, network.message);
        if (network.scanning || !network.ready) lv_obj_add_state(scan_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(scan_button, LV_STATE_DISABLED);
        if (shown_revision != network.revision) fill_networks();
    }
    bsp_display_unlock();
}

