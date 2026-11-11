#include "server_status.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::server_status {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr int kMaxRows = 4;

// Days since 1970-01-01 for a date (Howard Hinnant's days_from_civil).
static long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// One server per row: a dot (green up, red down, grey unknown), its name, and ping time or uptime on the right.
class ServersTile : public tessera::Tile {
 public:
  explicit ServersTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    row_h_ = ui::line_height(Font::BODY) + ui::px(6);
    const int fit = row_h_ > 0 ? height_ / row_h_ : 1;
    const int dot = ui::px(10);
    char key[16];
    for (int i = 0; i < kMaxRows && rows_ < fit; i++) {
      snprintf(key, sizeof key, "status%d", i + 1);
      if (!(c.options[key] | "")[0]) continue;  // no entity chosen for this server
      Row &r = row_[rows_];
      r.index = i + 1;
      r.dot = ui::block(c.parent, theme::MUTED);
      lv_obj_set_size(r.dot, dot, dot);
      lv_obj_set_pos(r.dot, 0, rows_ * row_h_ + (row_h_ - dot) / 2);
      r.name = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_pos(r.name, dot + ui::px(8), rows_ * row_h_ + ui::px(3));
      r.info = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.info, LV_TEXT_ALIGN_RIGHT, 0);
      rows_++;
    }
    empty_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(empty_, width_);
    lv_obj_set_style_text_align(empty_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(empty_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
    ui::set_text(empty_, rows_ ? "" : plugin_->text("choose"));
    layout_rows();
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
    for (int n = 0; n < rows_; n++) {
      Row &r = row_[n];
      ui::set_fill(r.dot, r.up == 1 ? Tone::GOOD : r.up == 0 ? Tone::ALERT : Tone::NORMAL, theme::MUTED);
      ui::set_text(r.name, r.name_text);
      ui::set_text(r.info, r.up == 0 ? plugin_->text("down") : info_text(r, epoch));
    }
    layout_rows();
  }

  void on_theme() override {
    ui::set_color(empty_, theme::MUTED);
    for (int n = 0; n < rows_; n++) {
      ui::set_color(row_[n].name, theme::INK);
      ui::set_color(row_[n].info, theme::MUTED);
    }
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
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6 && epoch) {
      // Assumes the timestamp is UTC, as Home Assistant states it.
      const long boot = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400L + h * 3600L + mi * 60L + se;
      long up = static_cast<long>(epoch) - boot;
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

  // The name takes the room the detail leaves it.
  void layout_rows() {
    const int dot = ui::px(10);
    for (int n = 0; n < rows_; n++) {
      Row &r = row_[n];
      const int y = n * row_h_ + ui::px(3);
      const int iw = ui::text_width(lv_label_get_text(r.info), Font::BODY) + ui::px(2);
      const int name_x = dot + ui::px(8);
      lv_obj_set_width(r.info, iw > 0 ? iw : 1);
      lv_obj_set_pos(r.info, width_ - iw, y);
      const int nw = width_ - name_x - iw - ui::px(6);
      lv_obj_set_width(r.name, nw > 1 ? nw : 1);
    }
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0, row_h_ = 0, rows_ = 0;
  Row row_[kMaxRows];
  lv_obj_t *empty_{};
};

void ServerStatus::setup() {
  // The id is the tile's id in the manifest.
  add_tile("servers", [this]() { return new ServersTile(this); });
}

}  // namespace esphome::server_status
