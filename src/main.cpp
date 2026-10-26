#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <esp32_smartdisplay.h>

static lv_style_t style_screen_dark;
static lv_style_t style_card_cyan;
static lv_style_t style_button_cyan;
static lv_style_t style_button_muted;
static lv_style_t style_button_text;
static bool styles_ready = false;

// The remote is intentionally independent from the radio's audio stack. It only
// sends HTTP commands to the ESP32 radio and does not stream audio itself.
static const char *RADIO_IP = "192.168.50.52";
static const char *RADIO_BASE = "http://192.168.50.52";

static lv_obj_t *home_screen;
static lv_obj_t *radio_screen;
static lv_obj_t *radio_station;
static lv_obj_t *radio_play_icon;
static lv_obj_t *radio_sleep_icon;
static lv_obj_t *radio_equalizer;
static lv_obj_t *radio_bars[10];
static lv_timer_t *equalizer_timer;
static lv_timer_t *radio_poll_timer;
static bool radio_online = false;
static bool radio_playing = false;
static bool radio_sleeping = false;

static void init_styles() {
    if (styles_ready) return;
    styles_ready = true;
    lv_style_init(&style_screen_dark);
    lv_style_set_bg_color(&style_screen_dark, lv_color_hex(0x101820));
    lv_style_set_bg_opa(&style_screen_dark, LV_OPA_COVER);

    lv_style_init(&style_card_cyan);
    lv_style_set_bg_color(&style_card_cyan, lv_color_hex(0x162632));
    lv_style_set_bg_opa(&style_card_cyan, LV_OPA_COVER);
    lv_style_set_border_color(&style_card_cyan, lv_color_hex(0x00D9FF));
    lv_style_set_border_width(&style_card_cyan, 1);
    lv_style_set_border_opa(&style_card_cyan, LV_OPA_70);
    lv_style_set_radius(&style_card_cyan, 12);

    lv_style_init(&style_button_cyan);
    lv_style_set_bg_color(&style_button_cyan, lv_color_hex(0x063E56));
    lv_style_set_bg_opa(&style_button_cyan, LV_OPA_COVER);
    lv_style_set_border_color(&style_button_cyan, lv_color_hex(0x00D9FF));
    lv_style_set_border_width(&style_button_cyan, 2);
    lv_style_set_radius(&style_button_cyan, 12);
    lv_style_set_text_color(&style_button_cyan, lv_color_hex(0xE8FBFF));

    lv_style_init(&style_button_muted);
    lv_style_set_bg_color(&style_button_muted, lv_color_hex(0x1D2C35));
    lv_style_set_bg_opa(&style_button_muted, LV_OPA_COVER);
    lv_style_set_border_color(&style_button_muted, lv_color_hex(0x52717B));
    lv_style_set_border_width(&style_button_muted, 1);
    lv_style_set_radius(&style_button_muted, 12);
    lv_style_set_text_color(&style_button_muted, lv_color_hex(0xAFC7CC));

    lv_style_init(&style_button_text);
    lv_style_set_text_color(&style_button_text, lv_color_hex(0xDDFBFF));
}

static void set_label(lv_obj_t *label, const char *text) {
    if (label) lv_label_set_text(label, text);
}

static void set_label_fmt(lv_obj_t *label, const char *format, const char *value, int number) {
    if (!label) return;
    char buffer[128];
    snprintf(buffer, sizeof(buffer), format, value, number);
    lv_label_set_text(label, buffer);
}

static bool radio_request(const char *path, String *response = nullptr) {
    HTTPClient http;
    String url = String(RADIO_BASE) + path;
    http.setConnectTimeout(1200);
    http.setTimeout(1800);
    if (!http.begin(url)) return false;
    int code = http.GET();
    if (response && code > 0) *response = http.getString();
    http.end();
    return code >= 200 && code < 300;
}

static void radio_set_online(bool online) {
    radio_online = online;
    if (radio_station && !online) lv_label_set_text(radio_station, "Radio offline");
}

static void update_radio_icons() {
    if (radio_play_icon) lv_label_set_text(radio_play_icon, radio_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (radio_sleep_icon) lv_label_set_text(radio_sleep_icon, radio_sleeping ? "ON" : "OFF");
}

static void animate_equalizer(lv_timer_t *) {
    static uint32_t seed = 0xA341316Cu;
    if (!radio_playing || radio_sleeping || !radio_online) { for (uint8_t i = 0; i < 10; i++) if (radio_bars[i]) lv_bar_set_value(radio_bars[i], 3, LV_ANIM_OFF); return; }
    for (uint8_t i = 0; i < 10; i++) { seed = seed * 1664525UL + 1013904223UL + i * 97UL; uint8_t value = 12 + ((seed >> 24) % 89); if (radio_bars[i]) lv_bar_set_value(radio_bars[i], value, LV_ANIM_OFF); }
}

static void radio_update_from_json(const String &payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        radio_set_online(false);
        return;
    }

    radio_set_online(true);
    const char *station = doc["station"] | "Unknown station";
    radio_playing = doc["playing"] | false;
    radio_sleeping = doc["sleeping"] | false;
    set_label(radio_station, station);
    update_radio_icons();
    animate_equalizer(nullptr);


}

static void radio_poll(lv_timer_t *) {
    if (!radio_screen || lv_scr_act() != radio_screen) return;
    String payload;
    if (radio_request("/api/status", &payload)) radio_update_from_json(payload);
    else radio_set_online(false);
}

static void radio_command(const char *path) {
    if (radio_request(path)) radio_poll(nullptr);
    else radio_set_online(false);
}

static void radio_back_cb(lv_event_t *) {
    if (radio_poll_timer) {
        lv_timer_del(radio_poll_timer);
        radio_poll_timer = nullptr;
    }
    lv_scr_load(home_screen);
}

static void radio_prev_cb(lv_event_t *) { radio_command("/api/prev"); }
static void radio_next_cb(lv_event_t *) { radio_command("/api/next"); }
static void radio_toggle_cb(lv_event_t *) { radio_command("/api/toggle"); }
static void radio_sleep_cb(lv_event_t *) { radio_command("/api/sleep"); }
static void radio_wake_cb(lv_event_t *) { radio_command("/api/wake"); }
static void radio_power_cb(lv_event_t *) { radio_command(radio_sleeping ? "/api/wake" : "/api/sleep"); }
static void radio_volume_down_cb(lv_event_t *) { radio_command("/api/vdown"); }
static void radio_volume_up_cb(lv_event_t *) { radio_command("/api/vup"); }

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_coord_t width, lv_coord_t height,
                             lv_event_cb_t callback, bool muted = false) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_add_style(button, muted ? &style_button_muted : &style_button_cyan, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(button, muted ? &style_button_muted : &style_button_cyan, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_style(label, &style_button_text, 0);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

static void create_header(lv_obj_t *screen, const char *title, lv_event_cb_t back_callback) {
    lv_obj_t *back = make_button(screen, LV_SYMBOL_LEFT, 48, 38, back_callback);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, title);
    lv_obj_add_style(label, &style_button_text, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x00D9FF), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
}

static void open_radio_cb(lv_event_t *) {
    lv_scr_load(radio_screen);
    radio_poll_timer = lv_timer_create(radio_poll, 2500, nullptr);
    radio_poll(nullptr);
}

static void build_radio_screen() {
    radio_screen = lv_obj_create(nullptr); lv_obj_add_style(radio_screen, &style_screen_dark, 0); lv_obj_set_style_pad_all(radio_screen, 8, 0); lv_obj_clear_flag(radio_screen, LV_OBJ_FLAG_SCROLLABLE);
    create_header(radio_screen, "Radio", radio_back_cb);
    radio_station = lv_label_create(radio_screen); lv_label_set_long_mode(radio_station, LV_LABEL_LONG_DOT); lv_obj_set_width(radio_station, 224); lv_obj_align(radio_station, LV_ALIGN_TOP_MID, 0, 48); lv_obj_set_style_text_align(radio_station, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_color(radio_station, lv_color_hex(0x00D9FF), 0); lv_obj_set_style_text_font(radio_station, &lv_font_montserrat_20, 0); set_label(radio_station, "...");
    radio_equalizer = lv_obj_create(radio_screen); lv_obj_remove_style_all(radio_equalizer); lv_obj_set_size(radio_equalizer, 220, 82); lv_obj_align(radio_equalizer, LV_ALIGN_TOP_MID, 0, 82);
    for (uint8_t i = 0; i < 10; i++) { radio_bars[i] = lv_bar_create(radio_equalizer); lv_obj_set_size(radio_bars[i], 14, 64); lv_bar_set_range(radio_bars[i], 0, 100); lv_bar_set_value(radio_bars[i], 4, LV_ANIM_OFF); lv_obj_set_style_bg_color(radio_bars[i], lv_color_hex(0x1C3540), LV_PART_MAIN); lv_obj_set_style_bg_color(radio_bars[i], lv_color_hex(0x00D9FF), LV_PART_INDICATOR); lv_obj_set_style_radius(radio_bars[i], 2, LV_PART_MAIN); lv_obj_set_style_radius(radio_bars[i], 2, LV_PART_INDICATOR); lv_obj_align(radio_bars[i], LV_ALIGN_LEFT_MID, i * 22, 0); }
    constexpr lv_coord_t button_size = 64;
    constexpr lv_coord_t button_gap = 8;
    constexpr lv_coord_t controls_width = button_size * 3 + button_gap * 2;
    lv_obj_t *row1 = lv_obj_create(radio_screen); lv_obj_remove_style_all(row1); lv_obj_set_size(row1, controls_width, button_size); lv_obj_align(row1, LV_ALIGN_TOP_MID, 0, 174);
    lv_obj_t *prev = make_button(row1, LV_SYMBOL_PREV, button_size, button_size, radio_prev_cb); lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *play = make_button(row1, LV_SYMBOL_PLAY, button_size, button_size, radio_toggle_cb); lv_obj_align(play, LV_ALIGN_LEFT_MID, button_size + button_gap, 0); radio_play_icon = lv_obj_get_child(play, 0);
    lv_obj_t *next = make_button(row1, LV_SYMBOL_NEXT, button_size, button_size, radio_next_cb); lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *row2 = lv_obj_create(radio_screen); lv_obj_remove_style_all(row2); lv_obj_set_size(row2, controls_width, button_size); lv_obj_align(row2, LV_ALIGN_TOP_MID, 0, 246);
    lv_obj_t *volume_down = make_button(row2, "-", button_size, button_size, radio_volume_down_cb); lv_obj_align(volume_down, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *power = make_button(row2, "OFF", button_size, button_size, radio_power_cb); lv_obj_align(power, LV_ALIGN_LEFT_MID, button_size + button_gap, 0); radio_sleep_icon = lv_obj_get_child(power, 0);
    lv_label_set_text(radio_sleep_icon, "OFF");
    lv_obj_t *volume_up = make_button(row2, "+", button_size, button_size, radio_volume_up_cb); lv_obj_align(volume_up, LV_ALIGN_RIGHT_MID, 0, 0);
    equalizer_timer = lv_timer_create(animate_equalizer, 144, nullptr);
}

static void placeholder_cb(lv_event_t *) {
    // Reserved for future devices.
}

static void build_home_screen() {
    home_screen = lv_obj_create(nullptr); lv_obj_add_style(home_screen, &style_screen_dark, 0); lv_obj_set_style_pad_all(home_screen, 6, 0); lv_obj_clear_flag(home_screen, LV_OBJ_FLAG_SCROLLABLE);
    constexpr lv_coord_t card_w = 96, card_h = 78, gap_x = 16, gap_y = 14, left_x = 12, right_x = left_x + card_w + gap_x, top_y = 34, bottom_y = top_y + card_h + gap_y;
    lv_obj_t *radio = make_button(home_screen, LV_SYMBOL_AUDIO "\nRadio", card_w, card_h, open_radio_cb); lv_obj_align(radio, LV_ALIGN_TOP_LEFT, left_x, top_y);
    lv_obj_t *music = make_button(home_screen, LV_SYMBOL_PLAY "\nMusic", card_w, card_h, placeholder_cb, true); lv_obj_align(music, LV_ALIGN_TOP_LEFT, right_x, top_y);
    lv_obj_t *lights = make_button(home_screen, LV_SYMBOL_SETTINGS "\nLights", card_w, card_h, placeholder_cb, true); lv_obj_align(lights, LV_ALIGN_TOP_LEFT, left_x, bottom_y);
    lv_obj_t *other = make_button(home_screen, LV_SYMBOL_HOME "\nOther", card_w, card_h, placeholder_cb, true); lv_obj_align(other, LV_ALIGN_TOP_LEFT, right_x, bottom_y);
}

static void connect_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(20);
    wm.setCaptivePortalEnable(true);
    Serial.println("Connecting to Wi-Fi; AP fallback: CYD-Remote-Setup");
    if (!wm.autoConnect("CYD-Remote-Setup")) {
        Serial.println("Wi-Fi setup timed out; continuing offline");
        radio_set_online(false);
        return;
    }
    Serial.print("CYD Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("CYD multi-device remote starting");
    init_styles();
    smartdisplay_init();
    connect_wifi();
    build_home_screen();
    build_radio_screen();
    lv_scr_load(home_screen);
}

void loop() {
    uint32_t now = millis();
    static uint32_t last_tick = 0;
    if (now - last_tick >= 10) {
        lv_tick_inc(now - last_tick);
        last_tick = now;
        lv_timer_handler();
    }
}
