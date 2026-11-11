#include "server_status.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace esphome::server_status {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr int MAX_SERVERS = 6;

// Days since 1970-01-01 for a date (Howard Hinnant's days_from_civil).
static long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// A moment as Home Assistant writes it ("2026-10-01T08:00:00+02:00", with or without a fraction and an offset) in
// seconds since 1970. False for anything else.
static bool parse_moment(const std::string &text, long &out) {
  int y, mo, d, h, mi, se, used = 0;
  if (sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d, &h, &mi, &se, &used) != 6) return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  long t = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400L + h * 3600L + mi * 60L + se;
  const char *p = text.c_str() + used;
  if (*p == '.')
    for (++p; *p >= '0' && *p <= '9';) ++p;
  if (*p == '+' || *p == '-') {
    int oh = 0, om = 0;
    sscanf(p + 1, "%d:%d", &oh, &om);
    const long offset = oh * 3600L + om * 60L;
    t += *p == '+' ? -offset : offset;
  }
  out = t;
  return true;
}

// Slower than this, a ping time (a detail sensor in ms) shows in amber.
static constexpr double SLOW_MS = 100;

struct Server {
  int index = 0;       // 1 to 6: the options status<index> and info<index>
  int up = -1;         // 1 up, 0 down, -1 not known
  bool has_info = false;
  uint32_t since = 0;  // when Home Assistant last heard a change of the status
  std::string fallback, name, info, unit;
};

// The servers of one tile, and what there is to say about them. The tile and the card each keep one.
class Servers {
 public:
  explicit Servers(const tessera::Plugin *plugin) : plugin_(plugin) {}

  Server list[MAX_SERVERS];
  int count = 0, up = 0, down = 0;
  int order[MAX_SERVERS] = {};  // down first, then not known, then up; within each the order of the settings

  void add(int index, const std::string &entity, bool has_info) {
    if (count >= MAX_SERVERS) return;
    Server &s = list[count++];
    s = Server();
    s.index = index;
    const size_t dot = entity.find('.');
    s.fallback = dot == std::string::npos ? entity : entity.substr(dot + 1);
    s.has_info = has_info;
    sort();
  }

  // The tile's data: {"entities": {"status1": {"state", "name", "updated"}, "info1": {"state", "attributes"}}}.
  // `discover`: the servers are the ones the data has (a card knows no options); else the ones add() made.
  void load(JsonObjectConst data, bool discover) {
    JsonObjectConst entities = data["entities"];
    char key[16];
    if (discover) {
      count = 0;
      for (int i = 1; i <= MAX_SERVERS; i++) {
        snprintf(key, sizeof key, "status%d", i);
        if (entities[key].isNull()) continue;
        snprintf(key, sizeof key, "info%d", i);
        add(i, "", !entities[key].isNull());
      }
    }
    for (int i = 0; i < count; i++) {
      Server &s = list[i];
      snprintf(key, sizeof key, "status%d", s.index);
      JsonObjectConst status = entities[key];
      const std::string state = status["state"] | "";
      s.up = state == "on" ? 1 : state == "off" ? 0 : -1;  // unavailable, unknown or not there: -1
      s.name = status["name"] | "";
      if (s.name.empty()) s.name = s.fallback;
      s.since = status["updated"] | 0u;
      snprintf(key, sizeof key, "info%d", s.index);
      JsonObjectConst info = entities[key];
      s.info = info["state"] | "";
      s.unit = info["attributes"]["unit_of_measurement"] | "";
    }
    sort();
  }

  // All of them together: red when one is down, green when all are up.
  Tone all() const { return down ? Tone::ALERT : up == count ? Tone::GOOD : Tone::NORMAL; }

  // One of them: green up, amber up but slow, red down, plain when not known.
  Tone tone(const Server &s) const {
    return s.up == 1 ? (slow(s) ? Tone::BUSY : Tone::GOOD) : s.up == 0 ? Tone::ALERT : Tone::NORMAL;
  }

  // A ping time over SLOW_MS.
  bool slow(const Server &s) const {
    if (s.up != 1 || s.unit != "ms" || s.info.empty()) return false;
    char *end = nullptr;
    const double value = strtod(s.info.c_str(), &end);
    return end != s.info.c_str() && !*end && value >= SLOW_MS;
  }

  // What is the matter, in a few words: all is well, the one that is down, or how many are.
  std::string summary() const {
    if (!count) return "";
    if (down == 1) return tessera::fill(plugin_->text("one_down"), "name", list[order[0]].name);
    if (down > 1) return tessera::format(plugin_->text("some_down"), static_cast<long>(down));
    return plugin_->text(up == count ? "all_up" : "no_data");
  }

  // "Up", "Down 12m", "No data".
  std::string state_text(const Server &s, uint32_t epoch) const {
    if (s.up == 1) return plugin_->text("up");
    if (s.up != 0) return plugin_->text("no_data");
    const std::string how_long = age(s.since, epoch);
    return how_long.empty() ? std::string(plugin_->text("down")) : tessera::fill(plugin_->text("down_for"), "time", how_long);
  }

  // How long ago a moment was, in the two largest parts that say something: "12d 4h", "3h 20m", "8m". Nothing while
  // the screen's clock is not set, or for a moment that is not known or still to come.
  std::string age(long since, uint32_t epoch) const {
    if (!since || !epoch || static_cast<long>(epoch) < since) return "";
    const long s = static_cast<long>(epoch) - since;
    const std::string d = std::to_string(s / 86400), h = std::to_string(s % 86400 / 3600), m = std::to_string(s % 3600 / 60);
    if (s >= 86400) return tessera::fill(tessera::fill(plugin_->text("age_days"), "d", d), "h", h);
    if (s >= 3600) return tessera::fill(tessera::fill(plugin_->text("age_hours"), "h", h), "m", m);
    return tessera::fill(plugin_->text("age_minutes"), "m", m);
  }

  // The detail sensor in words. A moment (the Glances uptime sensor holds when the machine started) is how long ago
  // that was; a number gets its unit ("1.8 ms"); anything else is shown as it is.
  std::string detail(const Server &s, uint32_t epoch) const {
    if (s.info.empty() || s.info == "unknown" || s.info == "unavailable") return "";
    long moment = 0;
    if (parse_moment(s.info, moment)) return age(moment, epoch);
    char *end = nullptr;
    const double value = strtod(s.info.c_str(), &end);
    if (end == s.info.c_str() || *end) return s.info;
    std::string number = s.info;
    if (number.find('.') != std::string::npos) {  // a whole number stays as it is; a fraction keeps one decimal
      char text[24];
      snprintf(text, sizeof text, value >= 100 || value <= -100 ? "%.0f" : "%.1f", value);
      number = text;
    }
    return s.unit.empty() ? number : number + " " + s.unit;
  }

 private:
  void sort() {
    int n = 0;
    up = down = 0;
    for (int rank : {0, -1, 1})
      for (int i = 0; i < count; i++)
        if (list[i].up == rank) order[n++] = i;
    for (int i = 0; i < count; i++) {
      up += list[i].up == 1;
      down += list[i].up == 0;
    }
  }

  const tessera::Plugin *plugin_;
};

// The tile. A single cell says it in one glance: how many servers are up, big, in green or red, and under it which
// one is down. A larger tile has a header and a row per server: a dot, its name, and its ping time or uptime. The
// servers that are down come first, so an outage is in view on a tile too small for all of them. A tap opens the card.
class ServersTile : public tessera::Tile {
 public:
  explicit ServersTile(const tessera::Plugin *plugin) : plugin_(plugin), servers_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    char key[16];
    for (int i = 1; i <= MAX_SERVERS; i++) {
      snprintf(key, sizeof key, "status%d", i);
      const std::string id = c.options[key] | "";
      if (id.empty()) continue;  // nothing chosen for this server
      snprintf(key, sizeof key, "info%d", i);
      servers_.add(i, id, (c.options[key] | "")[0] != 0);
    }
    if (!servers_.count) {
      note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(note_, width_);
      lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(note_, 0, (height_ - ui::line_height(Font::BODY)) / 2);
      ui::set_text(note_, plugin_->text("choose"));
      return;
    }
    single_ = c.columns < 2 && c.rows < 2;
    if (single_) {
      big_ = ui::label(c.parent, Font::VALUE, theme::INK);
      lv_obj_set_width(big_, width_);
      lv_obj_set_style_text_align(big_, LV_TEXT_ALIGN_CENTER, 0);
      sub_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(sub_, width_);
      lv_obj_set_style_text_align(sub_, LV_TEXT_ALIGN_CENTER, 0);
      return;
    }
    const int line = ui::line_height(Font::BODY);
    row_h_ = line + ui::px(ui::large() ? 8 : 5);
    dot_ = ui::px(ui::large() ? 10 : 8);
    // A header only with a second server, and room for it above two rows.
    const int head_h = ui::line_height(Font::TITLE) + ui::px(ui::large() ? 10 : 6);
    if (servers_.count > 1 && height_ >= head_h + 2 * row_h_) {
      top_ = head_h;
      head_ = ui::label(c.parent, Font::TITLE, theme::INK);
      lv_obj_set_pos(head_, 0, 0);
      tally_ = ui::label(c.parent, Font::TITLE, theme::MUTED);
      lv_obj_set_style_text_align(tally_, LV_TEXT_ALIGN_RIGHT, 0);
    }
    slots_ = std::max(1, std::min(servers_.count, (height_ - top_) / row_h_));
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const int y = top_ + i * row_h_;
      r.dot = ui::block(c.parent, theme::MUTED);
      lv_obj_set_size(r.dot, dot_, dot_);
      lv_obj_set_style_radius(r.dot, dot_ / 2, 0);
      lv_obj_set_pos(r.dot, 0, y + (row_h_ - dot_) / 2);
      r.name = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_pos(r.name, dot_ + ui::px(8), y + (row_h_ - line) / 2);
      r.info = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.info, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_y(r.info, y + (row_h_ - line) / 2);
    }
  }

  void on_state(JsonObjectConst data) override { servers_.load(data, false); }

  void on_tick(uint32_t epoch) override { paint(epoch); }

  // Light and dark: the plain colours here, the ones that mean something in paint().
  void on_theme() override {
    if (note_) ui::set_color(note_, theme::MUTED);
    if (big_) ui::set_color(big_, theme::INK);
    if (sub_) ui::set_color(sub_, theme::MUTED);
    if (head_) ui::set_color(head_, theme::INK);
    if (tally_) ui::set_color(tally_, theme::MUTED);
    for (int i = 0; i < slots_; i++) {
      ui::set_fill(slot_[i].dot, Tone::NORMAL, theme::MUTED);
      ui::set_color(slot_[i].name, theme::INK);
      ui::set_color(slot_[i].info, theme::MUTED);
    }
    paint(tessera::epoch());
  }

  // The card with every server and what is known of it.
  void on_tap() override {
    if (servers_.count) tessera::open_card(plugin_->plugin_id(), "servers", "", tile_);
  }

 private:
  struct Slot {
    lv_obj_t *dot{}, *name{}, *info{};
  };

  void paint(uint32_t epoch) {
    if (!servers_.count) return;
    char tally[12];
    snprintf(tally, sizeof tally, "%d/%d", servers_.up, servers_.count);
    if (single_) paint_single(tally, epoch);
    else paint_list(tally, epoch);
  }

  void paint_single(const char *tally, uint32_t epoch) {
    const Server &first = servers_.list[servers_.order[0]];
    std::string big, sub;
    Tone tone = servers_.all();
    if (servers_.count == 1) {
      // One server: its state big, its name and its ping time or uptime under it.
      big = plugin_->text(first.up == 1 ? "up" : first.up == 0 ? "down" : "no_data");
      const std::string more = first.up == 0 ? servers_.age(first.since, epoch) : first.up == 1 ? servers_.detail(first, epoch) : "";
      sub = more.empty() ? first.name : first.name + " · " + more;
      tone = servers_.tone(first);
    } else {
      big = tally;
      sub = servers_.summary();
    }
    Font face = Font::TITLE;
    for (Font f : {Font::VALUE, Font::HEADLINE, Font::TITLE}) {
      face = f;
      if (ui::text_width(big, f) <= width_) break;
    }
    ui::set_font(big_, face);
    ui::set_text(big_, big);
    ui::set_tone(big_, tone, theme::INK);
    ui::set_text(sub_, sub);
    const int big_h = ui::line_height(face), sub_h = ui::line_height(Font::BODY);
    const int y = std::max(0, (height_ - big_h - sub_h) / 2);
    lv_obj_set_pos(big_, 0, y);
    lv_obj_set_pos(sub_, 0, y + big_h);
  }

  void paint_list(const char *tally, uint32_t epoch) {
    if (head_) {
      const int tally_w = ui::text_width(tally, Font::TITLE) + ui::px(2);
      ui::set_text(tally_, tally);
      lv_obj_set_width(tally_, tally_w);
      lv_obj_set_pos(tally_, width_ - tally_w, 0);
      ui::set_text(head_, servers_.summary());
      ui::set_tone(head_, servers_.all(), theme::INK);
      lv_obj_set_width(head_, std::max(1, width_ - tally_w - ui::px(6)));
    }
    const int name_x = dot_ + ui::px(8);
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const Server &s = servers_.list[servers_.order[i]];
      // Up with a detail sensor: its ping time or uptime. Else the state in words.
      const std::string info = s.up == 1 && s.has_info ? servers_.detail(s, epoch) : servers_.state_text(s, epoch);
      const Tone tone = servers_.tone(s);
      ui::set_fill(r.dot, tone, theme::MUTED);
      ui::set_text(r.name, s.name);
      ui::set_text(r.info, info);
      ui::set_tone(r.info, tone == Tone::GOOD ? Tone::NORMAL : tone, theme::MUTED);  // only what is wrong takes a colour
      // The detail is as wide as its words; the name takes the room it leaves, and ends in dots when that is too little.
      const int info_w = info.empty() ? 0 : std::min(width_ / 2, ui::text_width(info, Font::BODY) + ui::px(2));
      lv_obj_set_width(r.info, std::max(1, info_w));
      lv_obj_set_x(r.info, width_ - info_w);
      lv_obj_set_width(r.name, std::max(1, width_ - name_x - info_w - (info_w ? ui::px(6) : 0)));
    }
  }

  const tessera::Plugin *plugin_;
  Servers servers_;
  int width_ = 0, height_ = 0, tile_ = -1, slots_ = 0, row_h_ = 0, dot_ = 0, top_ = 0;
  bool single_ = false;
  Slot slot_[MAX_SERVERS];
  lv_obj_t *note_{}, *big_{}, *sub_{}, *head_{}, *tally_{};
};

// The card a tap on the tile opens: every server on two lines. Its name with its state in its colour, and under it
// what its detail sensor says and since when it has been up or down.
class ServersCard : public tessera::Card {
 public:
  explicit ServersCard(const tessera::Plugin *plugin) : plugin_(plugin), servers_(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    const int name_h = ui::line_height(Font::BODY_LARGE), more_h = ui::line_height(Font::BODY);
    row_h_ = name_h + more_h + ui::px(ui::large() ? 14 : 8);
    dot_ = ui::px(ui::large() ? 12 : 9);
    slots_ = std::max(1, std::min(MAX_SERVERS, c.height / row_h_));
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const int y = i * row_h_;
      r.dot = ui::block(c.parent, theme::MUTED);
      lv_obj_set_size(r.dot, dot_, dot_);
      lv_obj_set_style_radius(r.dot, dot_ / 2, 0);
      lv_obj_set_pos(r.dot, 0, y + (name_h - dot_) / 2);
      r.name = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
      lv_obj_set_pos(r.name, dot_ + ui::px(10), y);
      r.state = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
      lv_obj_set_style_text_align(r.state, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_y(r.state, y);
      r.more = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(r.more, std::max(1, width_ - dot_ - ui::px(10)));
      lv_obj_set_pos(r.more, dot_ + ui::px(10), y + name_h);
    }
    paint(tessera::epoch());
  }

  void on_state(JsonObjectConst data) override { servers_.load(data, true); }

  void on_tick(uint32_t epoch) override { paint(epoch); }

  void on_theme() override {
    for (int i = 0; i < slots_; i++) {
      ui::set_fill(slot_[i].dot, Tone::NORMAL, theme::MUTED);
      ui::set_color(slot_[i].name, theme::INK);
      ui::set_color(slot_[i].state, theme::MUTED);
      ui::set_color(slot_[i].more, theme::MUTED);
    }
    paint(tessera::epoch());
  }

 private:
  struct Slot {
    lv_obj_t *dot{}, *name{}, *state{}, *more{};
  };

  // A moment as the screen writes it: the time today, the day with it before.
  static std::string moment_text(uint32_t at) {
    const std::string time = tessera::clock_text(at);
    return tessera::days_from_today(at) == 0 ? time : tessera::date_text(at) + " " + time;
  }

  void paint(uint32_t epoch) {
    const int name_x = dot_ + ui::px(10);
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const bool shown = i < servers_.count;
      for (lv_obj_t *part : {r.dot, r.name, r.state, r.more}) {
        if (shown) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
      }
      if (!shown) continue;
      const Server &s = servers_.list[servers_.order[i]];
      const Tone tone = servers_.tone(s);
      const std::string state = servers_.state_text(s, epoch);
      ui::set_fill(r.dot, tone, theme::MUTED);
      ui::set_text(r.name, s.name);
      ui::set_text(r.state, state);
      ui::set_tone(r.state, tone, theme::MUTED);
      const int state_w = ui::text_width(state, Font::BODY_LARGE) + ui::px(2);
      lv_obj_set_width(r.state, std::max(1, state_w));
      lv_obj_set_x(r.state, width_ - state_w);
      lv_obj_set_width(r.name, std::max(1, width_ - name_x - state_w - ui::px(8)));
      // The second line: the detail sensor (while it is up), and since when it is as it is.
      std::string more = s.up == 1 ? servers_.detail(s, epoch) : "";
      if (s.since && epoch && s.up != -1) {
        if (!more.empty()) more += " · ";
        more += tessera::fill(plugin_->text("since"), "when", moment_text(s.since));
      }
      ui::set_text(r.more, more);
    }
  }

  const tessera::Plugin *plugin_;
  Servers servers_;
  int width_ = 0, slots_ = 0, row_h_ = 0, dot_ = 0;
  Slot slot_[MAX_SERVERS];
};

void ServerStatus::setup() {
  // The ids are the tile's and the card's in the manifest.
  add_tile("servers", [this]() { return new ServersTile(this); });
  add_card("servers", [this]() { return new ServersCard(this); });
}

}  // namespace esphome::server_status
