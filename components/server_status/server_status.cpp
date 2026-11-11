#include "server_status.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::server_status {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr int kMaxRows = 6;

// Days since 1970-01-01 for a date (Howard Hinnant's days_from_civil).
static long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// A header (how many are up) and one row per server: a dot (green up, red down, grey unknown), its name, and ping
// time or uptime on the right. Servers that are down come first, so an outage stays in view on a small tile.
class ServersTile : public tessera::Tile {
 public:
  explicit ServersTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    row_h_ = ui::line_height(Font::BODY) + ui::px(6);
    head_h_ = ui::line_height(Font::TITLE) + ui::px(6);
    dot_ = ui::px(10);
    gap_ = ui::px(8);
    char key[16];
    for (int i = 0; i < kMaxRows; i++) {
      snprintf(key, sizeof key, "status%d", i + 1);
      if (!(c.options[key] | "")[0]) continue;  // no entity chosen for this server
      Row &r = row_[rows_++];
      r.index = i + 1;
      r.dot = ui::block(c.parent, theme::MUTED);
      lv_obj_set_size(r.dot, dot_, dot_);
      r.name = ui::label(c.parent, Font::BODY, theme::INK);
      r.info = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.info, LV_TEXT_ALIGN_RIGHT, 0);
    }
    // A header only when there is a second server and room for it plus two rows.
    has_header_ = rows_ > 1 && height_ >= head_h_ + 2 * row_h_;
    if (has_header_) {
      head_ = ui::label(c.parent, Font::TITLE, theme::INK);
      count_ = ui::label(c.parent, Font::TITLE, theme::MUTED);
      lv_obj_set_style_text_align(count_, LV_TEXT_ALIGN_RIGHT, 0);
    }
    empty_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(empty_, width_);
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(empty_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
    ui::set_text(empty_, rows_ ? "" : plugin_->text("choose"));
  }

  void on_state(JsonObjectConst data) override {
    JsonObjectConst ents = data["entities"];
    char key[16];
    for (int n = 0; n < rows_; n++) {
      Row &r = row_[n];
      snprintf(key, sizeof key, "status%d", r.index);
      JsonObjectConst st = ents[key];
      const std::string state = st["state"] | "";
      r.name_text = st["name"] | "";
      r.up = state == "on" ? 1 : state == "off" ? 0 : -1;  // unavailable/unknown stay -1
      snprintf(key, sizeof key, "info%d", r.index);
      JsonObjectConst in = ents[key];
      r.info_state = in["state"] | "";
      r.info_unit = in["attributes"]["unit_of_measurement"] | "";
    }
  }

  void on_tick(uint32_t epoch) override {
    int up = 0, down = 0;
    for (int n = 0; n < rows_; n++) {
      Row &r = row_[n];
      up += r.up == 1;
      down += r.up == 0;
      ui::set_fill(r.dot, r.up == 1 ? Tone::GOOD : r.up == 0 ? Tone::ALERT : Tone::NORMAL, theme::MUTED);
      ui::set_text(r.name, r.name_text);
      if (r.up == 0) {
        ui::set_text(r.info, plugin_->text("down"));
        ui::set_tone(r.info, Tone::ALERT, theme::MUTED);
      } else {
        ui::set_text(r.info, info_text(r, epoch));
        ui::set_tone(r.info, Tone::NORMAL, theme::MUTED);
      }
    }
    if (has_header_) {
      char count[16];
      snprintf(count, sizeof count, "%d/%d", up, rows_);
      ui::set_text(count_, count);
      if (down > 0) {
        ui::set_text(head_, tessera::format(plugin_->text("some_down"), down));
        ui::set_tone(head_, Tone::ALERT, theme::INK);
      } else if (up == rows_) {
        ui::set_text(head_, plugin_->text("all_up"));
        ui::set_tone(head_, Tone::GOOD, theme::INK);
      } else {
        ui::set_text(head_, plugin_->text("some_unknown"));
        ui::set_tone(head_, Tone::NORMAL, theme::INK);
      }
    }
    layout();
  }

  void on_theme() override {
    ui::set_color(empty_, theme::MUTED);
    for (int n = 0; n < rows_; n++) ui::set_color(row_[n].name, theme::INK);
    // The header and the details take their colours from on_tick, which runs right after.
  }

 private:
  struct Row {
    int index = 0;
    int up = -1;
    std::string name_text, info_state, info_unit;
    lv_obj_t *dot{}, *name{}, *info{};
  };

  // The detail entity as words. A number gets its unit ("12.4 ms"); a timestamp (the Glances uptime sensor is the
  // moment the machine booted) becomes an age ("12d 4h"). Anything else is shown as it is.
  std::string info_text(const Row &r, uint32_t epoch) const {
    const std::string &s = r.info_state;
    if (s.empty() || s == "unknown" || s == "unavailable") return "";
    int y, mo, d, h, mi, se;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6) {
      if (!epoch) return "";  // the screen's clock is not set yet
      // Assumes the timestamp is UTC, as Home Assistant states it.
      const long boot = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400L + h * 3600L + mi * 60L + se;
      const long up = static_cast<long>(epoch) - boot;
      if (up < 0) return "";
      const long days = up / 86400, hours = (up % 86400) / 3600, mins = (up % 3600) / 60;
      char buf[24];
      if (days > 0) snprintf(buf, sizeof buf, "%ldd %ldh", days, hours);
      else if (hours > 0) snprintf(buf, sizeof buf, "%ldh %ldm", hours, mins);
      else snprintf(buf, sizeof buf, "%ldm", mins);
      return buf;
    }
    char *end = nullptr;
    const double v = strtod(s.c_str(), &end);
    if (end != s.c_str() && *end == '\0') {
      char buf[24];
      snprintf(buf, sizeof buf, std::abs(v) >= 100 ? "%.0f" : "%.1f", v);
      return r.info_unit.empty() ? std::string(buf) : std::string(buf) + " " + r.info_unit;
    }
    return s;
  }

  // Down first, then unknown, then up; the order of the settings within each. Rows past the room are hidden.
  void layout() {
    int order[kMaxRows], n = 0;
    for (int rank : {0, -1, 1})
      for (int i = 0; i < rows_; i++)
        if (row_[i].up == rank) order[n++] = i;
    const int top = has_header_ ? head_h_ : 0;
    int slots = (height_ - top) / row_h_;
    if (slots < 1) slots = 1;
    if (has_header_) {
      const int cw = ui::text_width(lv_label_get_text(count_), Font::TITLE) + ui::px(2);
      lv_obj_set_width(count_, cw);
      lv_obj_set_pos(count_, width_ - cw, ui::px(3));
      lv_obj_set_width(head_, width_ - cw - ui::px(6));
      lv_obj_set_pos(head_, 0, ui::px(3));
    }
    for (int p = 0; p < n; p++) {
      Row &r = row_[order[p]];
      const bool shown = p < slots;
      for (lv_obj_t *o : {r.dot, r.name, r.info}) {
        if (shown) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      }
      if (!shown) continue;
      const int y = top + p * row_h_;
      const int iw = ui::text_width(lv_label_get_text(r.info), Font::BODY) + ui::px(2);
      const int name_x = dot_ + gap_;
      lv_obj_set_pos(r.dot, 0, y + (row_h_ - dot_) / 2);
      lv_obj_set_width(r.info, iw > 0 ? iw : 1);
      lv_obj_set_pos(r.info, width_ - iw, y + ui::px(3));
      const int nw = width_ - name_x - iw - ui::px(6);
      lv_obj_set_width(r.name, nw > 1 ? nw : 1);
      lv_obj_set_pos(r.name, name_x, y + ui::px(3));
    }
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0, row_h_ = 0, head_h_ = 0, dot_ = 0, gap_ = 0, rows_ = 0;
  bool has_header_ = false;
  Row row_[kMaxRows];
  lv_obj_t *empty_{}, *head_{}, *count_{};
};

void ServerStatus::setup() {
  // The id is the tile's id in the manifest.
  add_tile("servers", [this]() { return new ServersTile(this); });
}

}  // namespace esphome::server_status
