#pragma once

// Internal implementation detail for button_grid.h. Include button_grid.h from device YAML.

// ── Home Assistant actions ────────────────────────────────────────────

inline bool is_button_entity(const std::string &entity_id) {
  return entity_id.size() > 7 && entity_id.compare(0, 7, "button.") == 0;
}

// Press HA button entities; toggle other bound entities.
inline void send_toggle_action(const std::string &entity_id) {
  ha_send_entity_action(entity_id,
    is_button_entity(entity_id) ? "button.press" : "homeassistant.toggle");
}

inline void send_turn_off_action(const std::string &entity_id) {
  ha_send_entity_action(entity_id, "homeassistant.turn_off");
}

inline void send_turn_on_action(const std::string &entity_id) {
  ha_send_entity_action(entity_id, "homeassistant.turn_on");
}

inline bool action_card_requires_value(const std::string &action) {
  return action == "input_number.set_value";
}

inline const char *action_card_value_key(const std::string &action) {
  if (action == "input_number.set_value") return "value";
  return nullptr;
}

inline bool action_card_action_allowed(const std::string &action) {
  return action == "scene.turn_on" ||
         action == "script.turn_on" ||
         action == "automation.trigger" ||
         action == "button.press" ||
         action == "vacuum.start" ||
         action == "vacuum.return_to_base" ||
         action == "input_button.press" ||
         action == "input_boolean.toggle" ||
         action == "input_boolean.turn_on" ||
         action == "input_boolean.turn_off" ||
         action_card_option_select_action(action) ||
         action_card_requires_value(action);
}

inline void send_action_card_action(const ParsedCfg &p) {
  if (p.entity.empty() || p.sensor.empty() || !action_card_action_allowed(p.sensor)) return;
  if (action_card_option_select(p)) return;
  const char *value_key = action_card_value_key(p.sensor);
  if (value_key && p.unit.empty()) return;

  esphome::api::HomeassistantActionRequest req;
  if (!ha_action_begin(req, p.sensor.c_str(), false, value_key ? 2 : 1)) return;
  ha_action_add_entity(req, p.entity);
  if (value_key) {
    ha_action_add_data(req, value_key, p.unit.c_str());
  }
  ha_action_send(req);
}

using WebhookHeaders = std::vector<esphome::http_request::Header>;
using WebhookSender = std::function<bool(const std::string &, const std::string &,
                                         const std::string &, const WebhookHeaders &)>;

inline WebhookSender &webhook_sender() {
  static WebhookSender sender;
  return sender;
}

inline void register_webhook_sender(WebhookSender sender) {
  webhook_sender() = sender;
}

inline std::string trim_webhook_text(const std::string &value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    start++;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    end--;
  }
  return value.substr(start, end - start);
}

inline bool webhook_header_name_valid(const std::string &name) {
  if (name.empty() || name.size() > 64) return false;
  for (char ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c <= 32 || c >= 127 || ch == ':') return false;
  }
  return true;
}

inline void webhook_add_header(WebhookHeaders &headers,
                               const std::string &name,
                               const std::string &value) {
  std::string trimmed_name = trim_webhook_text(name);
  std::string trimmed_value = trim_webhook_text(value);
  if (!webhook_header_name_valid(trimmed_name) || trimmed_value.size() > 256) return;
  esphome::http_request::Header header;
  header.name = trimmed_name;
  header.value = trimmed_value;
  headers.push_back(header);
}

inline bool webhook_has_header(const WebhookHeaders &headers, const char *name) {
  if (!name) return false;
  std::string wanted = name;
  for (char &ch : wanted) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  for (const auto &header : headers) {
    std::string actual = header.name;
    for (char &ch : actual) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (actual == wanted) return true;
  }
  return false;
}

inline bool webhook_body_looks_json(const std::string &body) {
  for (char ch : body) {
    if (std::isspace(static_cast<unsigned char>(ch))) continue;
    return ch == '{' || ch == '[';
  }
  return false;
}

inline WebhookHeaders parse_webhook_headers(const std::string &value,
                                            const std::string &body) {
  WebhookHeaders headers;
  size_t start = 0;
  while (start <= value.size() && headers.size() < 8) {
    size_t end = value.find(';', start);
    if (end == std::string::npos) end = value.size();
    std::string part = trim_webhook_text(value.substr(start, end - start));
    size_t colon = part.find(':');
    if (colon != std::string::npos) {
      webhook_add_header(headers, part.substr(0, colon), part.substr(colon + 1));
    }
    start = end + 1;
  }
  if (!body.empty() && !webhook_has_header(headers, "Content-Type")) {
    webhook_add_header(headers, "Content-Type",
                       webhook_body_looks_json(body) ? "application/json" : "text/plain");
  }
  return headers;
}

inline void send_webhook_action(const ParsedCfg &p) {
  std::string url = trim_webhook_text(p.entity);
  if (url.empty()) {
    ESP_LOGW("webhook", "Webhook card has no URL");
    return;
  }
  std::string method = normalize_webhook_method(p.sensor);
  std::string body = (method == "GET" || method == "DELETE") ? "" : p.unit;
  WebhookHeaders headers = parse_webhook_headers(webhook_card_headers(p), body);
  WebhookSender &sender = webhook_sender();
  if (!sender) {
    ESP_LOGW("webhook", "Webhook sender is not registered");
    return;
  }
  ESP_LOGI("webhook", "Calling webhook with method %s", method.c_str());
  sender(url, method, body, headers);
}

inline void send_lock_action(const std::string &entity_id, const std::string &state) {
  ha_send_entity_action(entity_id, card_runtime_lock_toggle_service(state));
}

inline void send_lock_action(LockCardCtx *ctx) {
  if (!ctx) return;
  send_lock_action(ctx->entity_id, ctx->state);
}

inline void send_lock_command_action(const ParsedCfg &p) {
  if (p.entity.empty()) return;
  const char *service = card_runtime_lock_command_service(p.sensor);
  if (service == nullptr) return;

  ha_send_entity_action(p.entity, service);
}

// ── Slider card helpers ────────────────────────────────────────────────

inline bool is_cover_entity(const std::string &entity_id) {
  return entity_id.size() > 6 && entity_id.compare(0, 6, "cover.") == 0;
}

inline bool is_light_entity(const std::string &entity_id) {
  return entity_id.size() > 6 && entity_id.compare(0, 6, "light.") == 0;
}

inline bool is_fan_entity(const std::string &entity_id) {
  return entity_id.size() > 4 && entity_id.compare(0, 4, "fan.") == 0;
}

inline bool cover_toggle_mode(const std::string &sensor) {
  return card_runtime_cover_toggle_mode(sensor);
}

inline bool cover_tilt_mode(const std::string &sensor) {
  return card_runtime_cover_tilt_mode(sensor);
}

inline bool cover_modal_mode(const std::string &sensor) {
  return card_runtime_cover_modal_mode(sensor);
}

inline bool cover_command_mode(const std::string &sensor) {
  return card_runtime_cover_command_mode(sensor);
}

inline const char *cover_command_service(const std::string &sensor) {
  return card_runtime_cover_command_service(sensor);
}

inline int cover_position_value(const std::string &value) {
  char *end = nullptr;
  long pos = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str()) pos = 50;
  if (pos < 0) pos = 0;
  if (pos > 100) pos = 100;
  return static_cast<int>(pos);
}

inline uint32_t next_cover_stop_call_id() {
  static uint32_t call_id = 200000;
  return call_id++;
}

inline uint32_t &cover_stop_pending_call_id() {
  static uint32_t call_id = 0;
  return call_id;
}

inline void cover_stop_clear_pending(uint32_t call_id) {
  if (cover_stop_pending_call_id() == call_id) cover_stop_pending_call_id() = 0;
}

inline bool cover_stop_track_pending(uint32_t call_id) {
  if (call_id == 0 || cover_stop_pending_call_id() != 0) return false;
  cover_stop_pending_call_id() = call_id;
  return true;
}

inline void cover_stop_cancel_pending_request() {
  uint32_t call_id = cover_stop_pending_call_id();
  if (call_id == 0) return;
  cover_stop_pending_call_id() = 0;
  ha_cancel_action_response_callback(call_id, "api disconnected");
}

inline void send_cover_command_action(const ParsedCfg &p) {
  const char *service = cover_command_service(p.sensor);
  if (p.entity.empty() || service == nullptr) return;

  bool has_position = p.sensor == "set_position";
  bool wants_stop_response = p.sensor == "stop";
  esphome::api::HomeassistantActionRequest req;
  uint32_t call_id = wants_stop_response ? next_cover_stop_call_id() : 0;
  if (!ha_action_begin(req, service, false, has_position ? 2 : 1, call_id)) return;
  if (wants_stop_response) {
    req.call_id = call_id;
  }
  ha_action_add_entity(req, p.entity);
  if (has_position) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", cover_position_value(p.unit));
    ha_action_add_data(req, "position", buf);
  }
  bool cover_stop_tracked = false;
  if (wants_stop_response) {
    std::string entity_id = p.entity;
    cover_stop_tracked = cover_stop_track_pending(req.call_id);
    if (cover_stop_tracked) {
      if (!ha_register_action_response_callback(
        req.call_id,
        [entity_id, call_id = req.call_id](const esphome::api::ActionResponse &response) {
          cover_stop_clear_pending(call_id);
          if (response.is_success()) return;
          ESP_LOGW("cover", "cover.stop_cover failed for %s: %s; falling back to cover toggle",
                   entity_id.c_str(), response.get_error_message().c_str());
          send_toggle_action(entity_id);
        })) {
        cover_stop_clear_pending(req.call_id);
        cover_stop_tracked = false;
      }
    }
  }
  if (!ha_action_send(req) && cover_stop_tracked) {
    cover_stop_clear_pending(req.call_id);
    ha_cancel_action_response_callback(req.call_id, "send failed");
  }
}

inline void send_cover_command_action(const std::string &entity_id,
                                      const std::string &mode) {
  ParsedCfg p;
  p.entity = entity_id;
  p.sensor = mode;
  send_cover_command_action(p);
}

// Send HA action for a slider change: toggle (value<0), brightness, or cover position/tilt
inline void send_slider_action(const std::string &entity_id, int value, bool cover_tilt = false) {
  esphome::api::HomeassistantActionRequest req;
  if (value < 0) {
    if (!ha_action_begin(req, "homeassistant.toggle", false, 1)) return;
    ha_action_add_entity(req, entity_id);
  } else if (is_cover_entity(entity_id)) {
    if (!ha_action_begin(req,
      cover_tilt ? "cover.set_cover_tilt_position" : "cover.set_cover_position",
      false, 2)) return;
    ha_action_add_entity(req, entity_id);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    ha_action_add_data(req, cover_tilt ? "tilt_position" : "position", buf);
  } else if (is_fan_entity(entity_id)) {
    if (value == 0) {
      if (!ha_action_begin(req, "fan.turn_off", false, 1)) return;
      ha_action_add_entity(req, entity_id);
    } else {
      if (!ha_action_begin(req, "fan.turn_on", false, 2)) return;
      ha_action_add_entity(req, entity_id);
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", value);
      ha_action_add_data(req, "percentage", buf);
    }
  } else if (value == 0) {
    if (!ha_action_begin(req, "light.turn_off", false, 1)) return;
    ha_action_add_entity(req, entity_id);
  } else {
    if (!ha_action_begin(req, "light.turn_on", false, 2)) return;
    ha_action_add_entity(req, entity_id);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    ha_action_add_data(req, "brightness_pct", buf);
  }
  ha_action_send(req);
}

// Parse "min-max" kelvin range from the unit config field (e.g. "2000-6500").
inline void parse_kelvin_range(const std::string &unit, int &min_k, int &max_k) {
  min_k = 2000; max_k = 6500;
  if (unit.empty()) return;
  auto dash = unit.find('-');
  if (dash == std::string::npos || dash == 0) return;
  int a = atoi(unit.substr(0, dash).c_str());
  int b = atoi(unit.substr(dash + 1).c_str());
  if (a >= 1000 && b > a) { min_k = a; max_k = b; }
}

// Map a kelvin value to an lv_color_t by lerping between warm amber (low K) and
// cool blue-white (high K). Used when "Show light colour on card" is enabled.
//
// The interpolation is anchored to an absolute reference range
// (KELVIN_REF_MIN..KELVIN_REF_MAX) rather than the user's configured slider
// range. That way a narrow range (e.g. 5000-6000K) shows the actual subtle
// shift between two cool-white shades — instead of stretching the full
// amber-to-blue gradient across two values that should both look cool.
inline lv_color_t kelvin_to_fill_color(int k, int /*min_k*/, int /*max_k*/) {
  constexpr int KELVIN_REF_MIN = 2000;
  constexpr int KELVIN_REF_MAX = 6500;
  float t = (float)(k - KELVIN_REF_MIN) /
            (float)(KELVIN_REF_MAX - KELVIN_REF_MIN);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  // warm = 0xFF8012, cool = 0xB8CCFF
  uint8_t r = (uint8_t)(0xFF + t * (float)(0xB8 - 0xFF) + 0.5f);
  uint8_t g = (uint8_t)(0x80 + t * (float)(0xCC - 0x80) + 0.5f);
  uint8_t b = (uint8_t)(0x12 + t * (float)(0xFF - 0x12) + 0.5f);
  return lv_color_make(r, g, b);
}

// Send light.turn_on with color_temp_kelvin mapped from 0-100 pct over [min_k, max_k].
inline void send_light_temp_action(const std::string &entity_id, int pct, int min_k, int max_k) {
  esphome::api::HomeassistantActionRequest req;
  if (!ha_action_begin(req, "light.turn_on", false, 2)) return;
  if (entity_id.empty()) return;
  ha_action_add_entity(req, entity_id);
  int kelvin = min_k + (pct * (max_k - min_k)) / 100;
  if (kelvin < min_k) kelvin = min_k;
  if (kelvin > max_k) kelvin = max_k;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", kelvin);
  ha_action_add_data(req, "color_temp_kelvin", buf);
  ha_action_send(req);
}

inline void send_light_color_name_action(const std::string &entity_id, const char *color_name) {
  esphome::api::HomeassistantActionRequest req;
  if (!ha_action_begin(req, "light.turn_on", false, 2)) return;
  if (entity_id.empty() || !color_name || color_name[0] == '\0') return;
  ha_action_add_entity(req, entity_id);
  ha_action_add_data(req, "color_name", color_name);
  ha_action_send(req);
}

inline void send_light_rgb_action(const std::string &entity_id, uint32_t color) {
  esphome::api::HomeassistantActionRequest req;
  if (!ha_action_begin(req, "light.turn_on", false, 1)) return;
  if (entity_id.empty()) return;
  req.data_template.init(1);
  req.variables.init(3);
  ha_action_add_entity(req, entity_id);
  ha_action_add_data_template(req, "rgb_color", "{{ [red | int, green | int, blue | int] }}");
  char red[4];
  char green[4];
  char blue[4];
  snprintf(red, sizeof(red), "%u", static_cast<unsigned>((color >> 16) & 0xFF));
  snprintf(green, sizeof(green), "%u", static_cast<unsigned>((color >> 8) & 0xFF));
  snprintf(blue, sizeof(blue), "%u", static_cast<unsigned>(color & 0xFF));
  ha_action_add_variable(req, "red", red);
  ha_action_add_variable(req, "green", green);
  ha_action_add_variable(req, "blue", blue);
  ha_action_send(req);
}

inline const char *light_temp_icon(const std::string &icon) {
  return (!icon.empty() && icon != "Auto") ? find_icon(icon.c_str()) : find_icon("Lightbulb");
}

inline std::string media_card_mode(const std::string &sensor) {
  return card_runtime_media_mode(sensor);
}

inline bool media_playback_button_mode(const std::string &mode) {
  return card_runtime_media_playback_button_mode(mode);
}

inline const char *media_service_for_mode(const std::string &mode) {
  return card_runtime_media_playback_service(mode);
}

inline void send_media_player_action(const std::string &entity_id,
                                     const char *service,
                                     const char *value_key = nullptr,
                                     const char *value = nullptr) {
  if (entity_id.empty() || service == nullptr || service[0] == '\0') return;
  esphome::api::HomeassistantActionRequest req;
  if (!ha_action_begin(req, service, false, value_key && value ? 2 : 1)) return;
  ha_action_add_entity(req, entity_id);
  if (value_key && value) {
    ha_action_add_data(req, value_key, value);
  }
  ha_action_send(req);
}

inline void send_media_volume_action(const std::string &entity_id, int value) {
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  char buf[12];
  snprintf(buf, sizeof(buf), "%d.%02d", value / 100, value % 100);
  send_media_player_action(entity_id, "media_player.volume_set", "volume_level", buf);
}

inline void send_media_seek_action(const std::string &entity_id, int value, float duration) {
  if (duration <= 0.0f) return;
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  int seconds = (int)((duration * value / 100.0f) + 0.5f);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", seconds);
  send_media_player_action(entity_id, "media_player.media_seek", "seek_position", buf);
}

inline void send_media_playback_action(const std::string &entity_id,
                                       const std::string &mode) {
  if (entity_id.empty()) return;
  send_media_player_action(entity_id, media_service_for_mode(mode));
}

inline bool media_fast_press_mode(const std::string &mode) {
  return mode == "previous" || mode == "next";
}

inline bool *media_fast_press_slots() {
  static bool slots[MAX_GRID_SLOTS + 1] = {};
  return slots;
}

inline bool media_fast_press_consume(int slot_num) {
  if (slot_num <= 0 || slot_num > MAX_GRID_SLOTS) return false;
  bool *slots = media_fast_press_slots();
  bool sent = slots[slot_num];
  slots[slot_num] = false;
  return sent;
}

inline void handle_button_press(const std::string &cfg, int slot_num,
                                lv_obj_t *btn_obj) {
  if (slot_num <= 0 || slot_num > MAX_GRID_SLOTS) return;
  if (btn_obj && lv_obj_has_state(btn_obj, LV_STATE_DISABLED)) return;
  ParsedCfg p = parse_cfg(cfg);
  if (p.type != "media") return;
  std::string mode = media_card_mode(p.sensor);
  if (!media_fast_press_mode(mode) || p.entity.empty()) return;
  media_fast_press_slots()[slot_num] = true;
  send_media_playback_action(p.entity, mode);
}

inline bool button_long_press_indicator_supported(const ParsedCfg &p) {
  return card_long_press_action(p) == "modal" &&
    ((p.type == "cover" && cover_toggle_mode(p.sensor)) ||
     p.type == "light_control" ||
     p.type == "light_switch" ||
     p.type == "light_brightness" ||
     p.type == "light_temperature" ||
     (p.type.empty() && is_light_entity(p.entity)));
}

inline void button_long_press_arc_set_value(void *obj, int32_t value) {
  if (obj) lv_arc_set_value(static_cast<lv_obj_t *>(obj), value);
}

inline void button_long_press_indicator_hide_timer(lv_timer_t *timer) {
  if (!timer) return;
  lv_obj_t *arc = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
  if (arc) lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
  lv_timer_del(timer);
}

inline void button_long_press_indicator_stop(lv_obj_t *arc) {
  if (!arc) return;
  lv_anim_del(arc, button_long_press_arc_set_value);
  lv_arc_set_value(arc, 0);
  lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

inline void button_long_press_indicator_start(const std::string &cfg, lv_obj_t *arc,
                                              float seconds) {
  if (!arc) return;
  ParsedCfg p = parse_cfg(cfg);
  if (!button_long_press_indicator_supported(p)) {
    button_long_press_indicator_stop(arc);
    return;
  }
  if (std::isnan(seconds) || seconds < 1.0f) seconds = 2.0f;
  if (seconds > 5.0f) seconds = 5.0f;
  lv_anim_del(arc, button_long_press_arc_set_value);
  lv_arc_set_value(arc, 0);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(arc);
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, arc);
  lv_anim_set_exec_cb(&anim, button_long_press_arc_set_value);
  lv_anim_set_values(&anim, 0, 100);
  lv_anim_set_time(&anim, static_cast<uint32_t>(seconds * 1000.0f));
  lv_anim_start(&anim);
}

inline void button_long_press_indicator_complete(lv_obj_t *arc) {
  if (!arc) return;
  lv_anim_del(arc, button_long_press_arc_set_value);
  lv_arc_set_value(arc, 100);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(arc);
  lv_timer_create(button_long_press_indicator_hide_timer, 250, arc);
}

// ── Button click dispatch ─────────────────────────────────────────────

struct MediaVolumeCtx;
inline void media_volume_open_modal(MediaVolumeCtx *ctx);
struct ClimateControlCtx;
inline void climate_control_open_modal(ClimateControlCtx *ctx);
struct ImageCardCtx;
inline void image_card_open_modal(ImageCardCtx *ctx);
inline void switch_confirmation_open_modal(const ParsedCfg &p, lv_obj_t *btn_obj, bool turn_on);
struct OptionSelectCtx;
inline void option_select_open_modal(OptionSelectCtx *ctx);
struct TodoCardCtx;
inline bool todo_card_context_valid(TodoCardCtx *ctx);
inline void todo_card_open_modal(TodoCardCtx *ctx);
struct AlarmCardCtx;
inline void alarm_card_open_page(AlarmCardCtx *ctx);
inline bool alarm_card_context_valid(AlarmCardCtx *ctx);
struct AlarmActionCtx;
inline void alarm_action_activate(AlarmActionCtx *action);
inline bool alarm_action_context_valid(AlarmActionCtx *action);
inline void alarm_action_activate_mode(void *user_data, const std::string &mode);
struct FanCardCtx;
inline bool fan_non_speed_card_type(const std::string &type);
inline void fan_card_handle_click(FanCardCtx *ctx);
inline void fan_card_handle_action(FanCardCtx *ctx, const std::string &type);
struct CoverControlCtx;
inline void cover_control_open_modal(CoverControlCtx *ctx);
struct LightControlCtx;
inline void light_control_open_modal(LightControlCtx *ctx);
inline void handle_button_click(const std::string &cfg, int slot_num,
                                lv_obj_t *btn_obj);

inline void handle_button_action_cfg(const ParsedCfg &p, int slot_num,
                                     lv_obj_t *btn_obj,
                                     const std::string &action);

inline bool handle_button_secondary_action(const ParsedCfg &p, int slot_num,
                                           lv_obj_t *btn_obj) {
  std::string action = card_long_press_action(p);
  if (action == "__none") return true;
  if (action == "modal" && p.type == "cover") {
    CoverControlCtx *ctx = (CoverControlCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) {
      cover_control_open_modal(ctx);
      return true;
    }
  } else if (action == "modal" &&
             (p.type == "light_control" || p.type == "light_switch" ||
              p.type == "light_brightness" || p.type == "light_temperature" ||
              (p.type.empty() && is_light_entity(p.entity)))) {
    LightControlCtx *ctx = (LightControlCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) {
      light_control_open_modal(ctx);
      return true;
    }
  }
  handle_button_action_cfg(p, slot_num, btn_obj, action);
  return true;
}

inline void handle_button_long_press(const std::string &cfg, int slot_num,
                                     lv_obj_t *btn_obj) {
  if (media_fast_press_consume(slot_num)) return;
  if (btn_obj && lv_obj_has_state(btn_obj, LV_STATE_DISABLED)) return;
  ParsedCfg p = parse_cfg(cfg);
  if (handle_button_secondary_action(p, btn_obj)) return;
  ESP_LOGD("button", "Treating grid card long press as normal tap for slot %d", slot_num);
  handle_button_click(cfg, slot_num, btn_obj);
}

inline ParsedCfg cfg_for_action(ParsedCfg p, const std::string &action) {
  if (p.type == "alarm") {
    if (action != "control_panel") {
      p.type = "alarm_action";
      p.sensor = action;
    }
  } else if (p.type == "alarm_action") {
    if (action == "control_panel") {
      p.type = "alarm";
      p.sensor.clear();
    } else {
      p.sensor = action;
    }
  } else if (p.type == "cover" || p.type == "garage" || p.type == "lock" ||
             p.type == "media" || p.type == "vacuum" || p.type == "action") {
    p.sensor = action;
  } else if (fan_card_type(p.type)) {
    p.type = action;
  } else if (p.type == "light_control" || p.type == "light_switch" ||
             p.type == "light_brightness" || p.type == "light_temperature") {
    p.type = action;
  }
  return normalize_parsed_cfg(p);
}

inline void handle_button_action_cfg(const ParsedCfg &p, int slot_num,
                                     lv_obj_t *btn_obj,
                                     const std::string &action) {
  if (action.empty()) return;
  ParsedCfg effective = cfg_for_action(p, action);
  if (effective.type == "sensor" || effective.type == "text_sensor" ||
      effective.type == "door_window" || effective.type == "presence" ||
      effective.type == "calendar" || effective.type == "clock" ||
      effective.type == "timezone" || effective.type == "weather_forecast") return;
  if (effective.type == "screen_lock") {
    screen_lock_toggle();
  } else if (effective.type == "push") {
    std::string label = effective.label;
    if (label.empty()) {
      char buf[16];
      snprintf(buf, sizeof(buf), "Push %d", slot_num);
      label = buf;
    }
    esphome::api::HomeassistantActionRequest req;
    if (!ha_action_begin(req, "esphome.push_button_pressed", true, 2)) return;
    ha_action_add_data(req, "label", label.c_str());
    char slot_buf[8];
    snprintf(slot_buf, sizeof(slot_buf), "%d", slot_num);
    ha_action_add_data(req, "slot", slot_buf);
    ha_action_send(req);
  } else if (effective.type == "subpage") {
    lv_obj_t *sub_scr = (lv_obj_t *)lv_obj_get_user_data(btn_obj);
    if (sub_scr) lv_scr_load_anim(sub_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
  } else if (effective.type == "alarm") {
    AlarmCardCtx *ctx = (AlarmCardCtx *)lv_obj_get_user_data(btn_obj);
    if (alarm_card_context_valid(ctx)) alarm_card_open_page(ctx);
  } else if (effective.type == "alarm_action") {
    alarm_action_activate_mode(btn_obj ? lv_obj_get_user_data(btn_obj) : nullptr, effective.sensor);
  } else if (fan_card_type(effective.type)) {
    FanCardCtx *ctx = (FanCardCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) fan_card_handle_action(ctx, effective.type);
  } else if (effective.type == "cover" && cover_modal_mode(effective.sensor)) {
    CoverControlCtx *ctx = (CoverControlCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) cover_control_open_modal(ctx);
  } else if (effective.type == "light_control") {
    if (!effective.entity.empty()) send_toggle_action(effective.entity);
  } else if (effective.type == "garage") {
    if (garage_command_mode(effective.sensor)) {
      send_cover_command_action(effective);
    } else if (!effective.entity.empty()) {
      set_card_checked_state(btn_obj, true);
      send_toggle_action(effective.entity);
    }
  } else if (effective.type == "lock") {
    if (lock_command_mode(effective.sensor)) {
      send_lock_command_action(effective);
    } else {
      LockCardCtx *ctx = (LockCardCtx *)lv_obj_get_user_data(btn_obj);
      if (ctx) send_lock_action(ctx);
      else send_lock_action(effective.entity, "");
    }
  } else if (effective.type == "cover" && cover_command_mode(effective.sensor)) {
    send_cover_command_action(effective);
  } else if (effective.type == "cover" && cover_toggle_mode(effective.sensor)) {
    if (!effective.entity.empty()) {
      if (cover_stop_on_move_enabled(effective) && btn_obj && lv_obj_has_state(btn_obj, LV_STATE_USER_1)) {
        send_cover_command_action(effective.entity, "stop");
        return;
      }
      set_card_checked_state(btn_obj, true);
      send_toggle_action(effective.entity);
    }
  } else if (effective.type == "internal") {
    if (!effective.entity.empty()) send_internal_relay_action(effective);
  } else if (effective.type == "action") {
    if (action_card_option_select(effective)) {
      OptionSelectCtx *ctx = (OptionSelectCtx *)lv_obj_get_user_data(btn_obj);
      if (ctx) option_select_open_modal(ctx);
    } else if (action_script_confirmation_enabled(effective) && btn_obj) {
      switch_confirmation_open_modal(effective, btn_obj, false);
    } else {
      send_action_card_action(effective);
    }
  } else if (effective.type == "vacuum") {
    VacuumCardCtx *ctx = (VacuumCardCtx *)lv_obj_get_user_data(btn_obj);
    VacuumCardCtx fallback;
    if (ctx) {
      fallback = *ctx;
      fallback.mode = vacuum_card_mode(effective.sensor);
      fallback.area_id = effective.unit;
      fallback.status_card = fallback.mode == "status";
      send_vacuum_card_action(&fallback);
    } else if (!vacuum_card_read_only(effective)) {
      fallback.entity_id = effective.entity;
      fallback.mode = vacuum_card_mode(effective.sensor);
      fallback.area_id = effective.unit;
      fallback.status_card = fallback.mode == "status";
      send_vacuum_card_action(&fallback);
    }
  } else if (effective.type == "webhook") {
    send_webhook_action(effective);
  } else if (effective.type == "todo") {
    TodoCardCtx *ctx = (TodoCardCtx *)lv_obj_get_user_data(btn_obj);
    if (todo_card_context_valid(ctx)) todo_card_open_modal(ctx);
  } else if (effective.type == "media") {
    std::string mode = media_card_mode(effective.sensor);
    if (mode == "volume") {
      MediaVolumeCtx *ctx = (MediaVolumeCtx *)lv_obj_get_user_data(btn_obj);
      if (ctx) media_volume_open_modal(ctx);
    } else if (mode == "now_playing" && effective.precision == "play_pause") {
      send_media_playback_action(effective.entity, "play_pause");
    } else if (media_playback_button_mode(mode)) {
      send_media_playback_action(effective.entity, mode);
    }
  } else if (effective.type == "climate") {
    ClimateControlCtx *ctx = (ClimateControlCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) climate_control_open_modal(ctx);
  } else if (effective.type == "image") {
    ImageCardCtx *ctx = (ImageCardCtx *)lv_obj_get_user_data(btn_obj);
    if (ctx) image_card_open_modal(ctx);
  } else if (effective.type == "light_temperature") {
    // Tap does nothing; only dragging the slider sends commands.
  } else if (brightness_slider_type(effective.type) || effective.type == "cover") {
    if (!effective.entity.empty()) send_slider_action(effective.entity, -1, cover_tilt_mode(effective.sensor));
  } else {
    if (!effective.entity.empty()) {
      bool currently_on = btn_obj && lv_obj_has_state(btn_obj, LV_STATE_CHECKED);
      if (switch_confirmation_required(effective, currently_on) && btn_obj &&
          !is_button_entity(effective.entity)) {
        switch_confirmation_open_modal(effective, btn_obj, !currently_on);
      } else {
        send_toggle_action(effective.entity);
      }
    }
  }
}

// Handle a main-grid button press: dispatch push event, subpage nav,
// slider toggle, or entity toggle based on the config string.
inline void handle_button_click(const std::string &cfg, int slot_num,
                                lv_obj_t *btn_obj) {
  if (media_fast_press_consume(slot_num)) return;
  if (btn_obj && lv_obj_has_state(btn_obj, LV_STATE_DISABLED)) return;
  ParsedCfg p = parse_cfg(cfg);
  handle_button_action_cfg(p, slot_num, btn_obj, card_short_press_action(p));
}
