#include "server_status.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace esphome::server_status {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr int MAX_SERVERS = 4;

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
// Less than this, an uptime (a percentage) shows in amber.
static constexpr double LOW_UPTIME = 99.9;

// A number in a state, or false for anything else ("unavailable", "").
static bool number_of(const std::string &text, double &out) {
  char *end = nullptr;
  out = strtod(text.c_str(), &end);
  return end != text.c_str() && !*end;
}

// What a name says in its last brackets: Uptime Kuma's "nas Uptime (30 days)" says "30 days".
static std::string bracketed(const std::string &name) {
  const size_t close = name.rfind(')'), open = name.rfind('(');
  if (close == std::string::npos || open == std::string::npos || open > close) return "";
  return name.substr(open + 1, close - open - 1);
}

// A server's state. Uptime Kuma's status sensor says up, down, pending (it failed and tries again) or maintenance;
// a Ping binary sensor says on or off.
enum : int { STATE_UNKNOWN = -1, STATE_DOWN = 0, STATE_UP = 1, STATE_PENDING = 2, STATE_PAUSED = 3 };

static int state_of(const std::string &state) {
  if (state == "up" || state == "on") return STATE_UP;
  if (state == "down" || state == "off") return STATE_DOWN;
  if (state == "pending") return STATE_PENDING;
  if (state == "maintenance") return STATE_PAUSED;
  return STATE_UNKNOWN;  // unavailable, unknown, or nothing chosen
}

// A name as the row shows it: Uptime Kuma's "nas Status" (sometimes with a space in front) is "nas".
static std::string tidy(std::string name) {
  static const std::string suffix = " Status";
  if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    name.erase(name.size() - suffix.size());
  const size_t first = name.find_first_not_of(' '), last = name.find_last_not_of(' ');
  return first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
}

struct Server {
  int index = 0;           // 1 to 4: the options status<index>, info<index> (response time) and uptime<index>
  int up = STATE_UNKNOWN;  // one of the states above
  bool has_info = false, has_uptime = false;
  uint32_t since = 0;      // when Home Assistant last heard a change of the status
  std::string fallback, name, info, unit, uptime, window;
};

// The servers of one tile, and what there is to say about them. The tile and the card each keep one.
class Servers {
 public:
  explicit Servers(const tessera::Plugin *plugin) : plugin_(plugin) {}

  Server list[MAX_SERVERS];
  int count = 0, up = 0, down = 0, pending = 0, paused = 0;
  int order[MAX_SERVERS] = {};  // down, pending, not known, maintenance, up; within each the order of the settings

  void add(int index, const std::string &entity, bool has_info, bool has_uptime) {
    if (count >= MAX_SERVERS) return;
    Server &s = list[count++];
    s = Server();
    s.index = index;
    const size_t dot = entity.find('.');
    s.fallback = dot == std::string::npos ? entity : entity.substr(dot + 1);
    s.has_info = has_info;
    s.has_uptime = has_uptime;
    sort();
  }

  // The tile's data: {"entities": {"status1": {"state", "name", "updated"}, "info1": {"state", "attributes"}},
  // "uptime1": {"state", "name"}}.
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
        const bool has_info = !entities[key].isNull();
        snprintf(key, sizeof key, "uptime%d", i);
        add(i, "", has_info, !entities[key].isNull());
      }
    }
    for (int i = 0; i < count; i++) {
      Server &s = list[i];
      snprintf(key, sizeof key, "status%d", s.index);
      JsonObjectConst status = entities[key];
      const std::string state = status["state"] | "";
      s.up = state_of(state);
      s.name = tidy(status["name"] | "");
      if (s.name.empty()) s.name = s.fallback;
      s.since = status["updated"] | 0u;
      snprintf(key, sizeof key, "info%d", s.index);
      JsonObjectConst info = entities[key];
      s.info = info["state"] | "";
      s.unit = info["attributes"]["unit_of_measurement"] | "";
      snprintf(key, sizeof key, "uptime%d", s.index);
      JsonObjectConst uptime = entities[key];
      s.uptime = uptime["state"] | "";
      s.window = bracketed(uptime["name"] | "");
    }
    sort();
  }

  // All up, apart from the ones in maintenance.
  bool all_well() const { return count && up + paused == count && up; }

  // All of them together: red when one is down, amber when one is pending, green when all are up.
  Tone all() const { return down ? Tone::ALERT : pending ? Tone::BUSY : all_well() ? Tone::GOOD : Tone::NORMAL; }

  // One of them: green up, amber up but slow or pending, red down, plain in maintenance or not known.
  Tone tone(const Server &s) const {
    if (s.up == STATE_UP) return slow(s) ? Tone::BUSY : Tone::GOOD;
    return s.up == STATE_DOWN ? Tone::ALERT : s.up == STATE_PENDING ? Tone::BUSY : Tone::NORMAL;
  }

  // A ping time over SLOW_MS.
  bool slow(const Server &s) const {
    double value;
    return s.up == STATE_UP && s.unit == "ms" && number_of(s.info, value) && value >= SLOW_MS;
  }

  // An uptime under LOW_UPTIME.
  bool low(const Server &s) const {
    double value;
    return number_of(s.uptime, value) && value < LOW_UPTIME;
  }

  // The uptime as a percentage with what it says: "100%", "99.998%", "99.5%". Nothing without one.
  std::string uptime_text(const Server &s) const {
    double value;
    if (!number_of(s.uptime, value)) return "";
    char text[16];
    snprintf(text, sizeof text, "%.3f", value >= 100 ? 100.0 : value);
    std::string number = text;
    number.erase(number.find_last_not_of('0') + 1);
    if (!number.empty() && number.back() == '.') number.pop_back();
    return number + "%";
  }

  // A row's words on the right. Up: its response time and its uptime ("<1 ms · 99.998%"), or "Up" without either.
  // Else the state in words ("Down 12m").
  std::string line(const Server &s, uint32_t epoch) const {
    if (s.up != STATE_UP) return state_text(s, epoch);
    std::string text = s.has_info ? detail(s, epoch) : "";
    const std::string pct = uptime_text(s);
    if (!pct.empty()) text += (text.empty() ? "" : " \u00B7 ") + pct;
    return text.empty() ? std::string(plugin_->text("up")) : text;
  }

  // The colour of a row's words: what is wrong only. Down red, pending, slow or a low uptime amber.
  Tone line_tone(const Server &s) const {
    if (s.up != STATE_UP) return tone(s);
    return slow(s) || low(s) ? Tone::BUSY : Tone::NORMAL;
  }

  // What is the matter, in a few words: all is well, the one that is down, or how many are.
  std::string summary() const {
    if (!count) return "";
    if (down == 1) return tessera::fill(plugin_->text("one_down"), "name", list[order[0]].name);
    if (down > 1) return tessera::format(plugin_->text("some_down"), static_cast<long>(down));
    if (pending == 1)
      for (int i = 0; i < count; i++)
        if (list[i].up == STATE_PENDING) return tessera::fill(plugin_->text("one_pending"), "name", list[i].name);
    if (pending > 1) return tessera::format(plugin_->text("some_pending"), static_cast<long>(pending));
    return plugin_->text(all_well() ? "all_up" : "no_data");
  }

  // "Up", "Down 12m", "Pending", "Maintenance", "No data".
  std::string state_text(const Server &s, uint32_t epoch) const {
    if (s.up == STATE_UP) return plugin_->text("up");
    if (s.up == STATE_PENDING) return plugin_->text("pending");
    if (s.up == STATE_PAUSED) return plugin_->text("maintenance");
    if (s.up != STATE_DOWN) return plugin_->text("no_data");
    const std::string how_long = age(s.since, epoch);
    return how_long.empty() ? std::string(plugin_->text("down")) : tessera::fill(plugin_->text("down_for"), "time", how_long);
  }

  // How long ago a moment was, in the two largest parts that say something: "12d 4h", "3h 20m", "8m". Nothing while
  // the screen's clock is not set, or for a moment that is not known or still to come.
  std::string age(long since, uint32_t epoch) const {
    if (!since || !epoch || static_cast<long>(epoch) < since) return "";
    const long s = static_cast<long>(epoch) - since;
    const std::string d = std::to_string(s / 86400), h = std::to_string(s % 86400 / 3600), m = std::to_string(s % 3600 / 60);
    if (s >= 86400) return tessera::fill(tessera::fill(plugin_->text("age_days"), "d", d).c_str(), "h", h);
    if (s >= 3600) return tessera::fill(tessera::fill(plugin_->text("age_hours"), "h", h).c_str(), "m", m);
    return tessera::fill(plugin_->text("age_minutes"), "m", m);
  }

  // The detail sensor in words. A number gets its unit ("152 ms", "<1 ms"); a moment (the Glances uptime sensor holds
  // when the machine started) is how long ago that was; anything else is shown as it is.
  std::string detail(const Server &s, uint32_t epoch) const {
    if (s.info.empty() || s.info == "unknown" || s.info == "unavailable") return "";
    long moment = 0;
    if (parse_moment(s.info, moment)) return age(moment, epoch);
    char *end = nullptr;
    const double value = strtod(s.info.c_str(), &end);
    if (end == s.info.c_str() || *end) return s.info;
    // Uptime Kuma rounds a ping on the home network to 0 ms.
    if (s.unit == "ms" && value < 1) return plugin_->text("under_ms");
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
    up = down = pending = paused = 0;
    for (int rank : {STATE_DOWN, STATE_PENDING, STATE_UNKNOWN, STATE_PAUSED, STATE_UP})
      for (int i = 0; i < count; i++)
        if (list[i].up == rank) order[n++] = i;
    for (int i = 0; i < count; i++) {
      up += list[i].up == STATE_UP;
      down += list[i].up == STATE_DOWN;
      pending += list[i].up == STATE_PENDING;
      paused += list[i].up == STATE_PAUSED;
    }
  }

  const tessera::Plugin *plugin_;
};

// Icons of Tessera's set (screen_manager/app/tile_icons.py).
static constexpr uint32_t ICON_ALL_UP = 0xF0C54;    // check-network-outline
static constexpr uint32_t ICON_DOWN = 0xF0C5F;      // close-network-outline
static constexpr uint32_t ICON_UNKNOWN = 0xF05D6;   // alert-circle-outline
static constexpr uint32_t ICON_CHECK = 0xF012C;     // check: in the badge of a single cell
static constexpr uint32_t ICON_CROSS = 0xF0156;     // close
static constexpr uint32_t ICON_DASH = 0xF0374;      // minus
static constexpr uint32_t ICON_DOTS = 0xF01D8;      // dots-horizontal: pending

static uint32_t icon_of(const Servers &s) { return s.down ? ICON_DOWN : s.all_well() ? ICON_ALL_UP : ICON_UNKNOWN; }

// A thin line between rows, in the theme's colour for lines.
static lv_obj_t *divider(lv_obj_t *parent, int width, int y) {
  lv_obj_t *line = ui::block(parent, theme::LINE);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_size(line, width, std::max(1, ui::px(1)));
  lv_obj_set_pos(line, 0, y);
  return line;
}

// The tile. A single cell says it in one glance: an icon and how many servers are up, big, in green or red, and under
// it which one is down. A larger tile has a header and a row per server: a dot, its name, and its ping time or uptime.
// The servers that are down come first, so an outage is in view on a tile too small for all of them. A tap opens the
// card. Every change is reported to the plugin, for the top bar.
class ServersTile : public tessera::Tile {
 public:
  explicit ServersTile(ServerStatus *plugin) : plugin_(plugin), servers_(plugin) {}
  ~ServersTile() override { plugin_->forget(tile_); }

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
      const bool has_info = (c.options[key] | "")[0] != 0;
      snprintf(key, sizeof key, "uptime%d", i);
      servers_.add(i, id, has_info, (c.options[key] | "")[0] != 0);
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
      // A badge: a round block as large as the cell allows, filled in the colour of the state, with a check, a cross
      // or a dash in it. Under it the name (or the summary), a line with the response time, and for one server with an
      // uptime sensor a line with its uptime, as far as there is room: the badge keeps at least an icon's size.
      const int title_h = ui::line_height(Font::BODY_LARGE), sub_h = ui::line_height(Font::BODY);
      const int gap = ui::px(ui::large() ? 8 : 5), least = ui::line_height(Font::ICON) + ui::px(12);
      with_third_ = servers_.count == 1 && servers_.list[0].has_uptime;
      int room = height_ - gap - title_h - sub_h - (with_third_ ? sub_h : 0);
      if (with_third_ && room < least) {
        with_third_ = false;
        room += sub_h;
      }
      with_sub_ = room >= least;
      if (!with_sub_) room += sub_h;
      const int d = std::max(least, std::min(width_, room));
      const int total = d + gap + title_h + (with_sub_ ? sub_h : 0) + (with_third_ ? sub_h : 0);
      const int y = std::max(0, (height_ - total) / 2);
      badge_ = ui::block(c.parent, theme::TRACK);
      lv_obj_set_size(badge_, d, d);
      lv_obj_set_style_radius(badge_, d / 2, 0);
      lv_obj_set_pos(badge_, (width_ - d) / 2, y);
      badge_icon_ = ui::label(badge_, Font::ICON, theme::ON_ACCENT);
      lv_label_set_long_mode(badge_icon_, LV_LABEL_LONG_CLIP);
      lv_obj_center(badge_icon_);  // as the line badge of ov_departures centres its number
      big_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
      lv_obj_set_width(big_, width_);
      lv_obj_set_style_text_align(big_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(big_, 0, y + d + gap);
      sub_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(sub_, width_);
      lv_obj_set_style_text_align(sub_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(sub_, 0, y + d + gap + title_h);
      if (with_third_) {
        third_ = ui::label(c.parent, Font::BODY, theme::MUTED);
        lv_obj_set_width(third_, width_);
        lv_obj_set_style_text_align(third_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(third_, 0, y + d + gap + title_h + sub_h);
      }
      return;
    }
    const int line = ui::line_height(Font::BODY);
    row_h_ = line + ui::px(ui::large() ? 12 : 7);
    dot_ = ui::px(ui::large() ? 10 : 8);
    // A header only with a second server, and room for it above two rows.
    const int head_line = std::max(ui::line_height(Font::TITLE), ui::line_height(Font::ICON_SMALL));
    const int head_h = head_line + ui::px(ui::large() ? 10 : 6);
    if (servers_.count > 1 && height_ >= head_h + 2 * row_h_) {
      top_ = head_h;
      head_icon_ = ui::label(c.parent, Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_pos(head_icon_, 0, (head_line - ui::line_height(Font::ICON_SMALL)) / 2);
      head_ = ui::label(c.parent, Font::TITLE, theme::INK);
      lv_obj_set_pos(head_, ui::line_height(Font::ICON_SMALL) + ui::px(6), (head_line - ui::line_height(Font::TITLE)) / 2);
      tally_ = ui::label(c.parent, Font::TITLE, theme::MUTED);
      lv_obj_set_style_text_align(tally_, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_y(tally_, (head_line - ui::line_height(Font::TITLE)) / 2);
      lines_[lines_n_++] = divider(c.parent, width_, top_ - ui::px(ui::large() ? 4 : 3));
    }
    slots_ = std::max(1, std::min(servers_.count, (height_ - top_) / row_h_));
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const int y = top_ + i * row_h_;
      if (i > 0) lines_[lines_n_++] = divider(c.parent, width_, y);
      r.dot = ui::block(c.parent, theme::MUTED);
      lv_obj_set_size(r.dot, dot_, dot_);
      lv_obj_set_style_radius(r.dot, dot_ / 2, 0);
      lv_obj_set_pos(r.dot, 0, y + (row_h_ - dot_) / 2);
      r.name = ui::label(c.parent, Font::BODY, theme::INK);
      lv_obj_set_pos(r.name, dot_ + ui::px(10), y + (row_h_ - line) / 2);
      r.info = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(r.info, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_y(r.info, y + (row_h_ - line) / 2);
    }
  }

  void on_state(JsonObjectConst data) override {
    servers_.load(data, false);
    if (servers_.count) plugin_->report(tile_, servers_.down, servers_.list[servers_.order[0]].name);
  }

  void on_tick(uint32_t epoch) override { paint(epoch); }

  // Light and dark: the plain colours here, the ones that mean something in paint().
  void on_theme() override {
    if (note_) ui::set_color(note_, theme::MUTED);
    if (big_) ui::set_color(big_, theme::INK);
    if (sub_) ui::set_color(sub_, theme::MUTED);
    if (third_) ui::set_color(third_, theme::MUTED);
    if (head_) ui::set_color(head_, theme::INK);
    if (tally_) ui::set_color(tally_, theme::MUTED);
    for (int i = 0; i < lines_n_; i++) ui::set_fill(lines_[i], Tone::NORMAL, theme::LINE);
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
    std::string title, sub;
    Tone tone, sub_tone = Tone::NORMAL;
    int state;  // as a server's: for several, the worst of them
    if (servers_.count == 1) {
      // One server: its name, its response time (or its state in words) under it, and its uptime under that.
      tone = servers_.tone(first);
      state = first.up;
      title = first.name;
      sub = first.up == STATE_UP && first.has_info ? servers_.detail(first, epoch) : servers_.state_text(first, epoch);
      sub_tone = first.up == STATE_UP ? (servers_.slow(first) ? Tone::BUSY : Tone::NORMAL) : tone;
      if (third_) {
        ui::set_text(third_, servers_.uptime_text(first));
        ui::set_tone(third_, servers_.low(first) ? Tone::BUSY : Tone::NORMAL, theme::MUTED);
      }
    } else {
      // Several: what is the matter (or that all is well), and how many are up.
      tone = servers_.all();
      state = servers_.down ? STATE_DOWN : servers_.pending ? STATE_PENDING : servers_.all_well() ? STATE_UP : STATE_UNKNOWN;
      title = servers_.summary();
      sub = tally;
    }
    const uint32_t icon = state == STATE_UP ? ICON_CHECK : state == STATE_DOWN ? ICON_CROSS : state == STATE_PENDING ? ICON_DOTS : ICON_DASH;
    ui::set_fill(badge_, tone, theme::TRACK);
    ui::set_text(badge_icon_, ui::icon(icon));
    ui::set_color(badge_icon_, tone == Tone::NORMAL ? theme::MUTED : theme::ON_ACCENT);
    ui::set_text(big_, title);
    ui::set_color(big_, state == STATE_UNKNOWN || state == STATE_PAUSED ? theme::MUTED : theme::INK);
    ui::set_text(sub_, with_sub_ ? sub : std::string());
    ui::set_tone(sub_, sub_tone, theme::MUTED);
  }

  void paint_list(const char *tally, uint32_t epoch) {
    if (head_) {
      const int tally_w = ui::text_width(tally, Font::TITLE) + ui::px(2);
      ui::set_text(tally_, tally);
      lv_obj_set_width(tally_, tally_w);
      lv_obj_set_x(tally_, width_ - tally_w);
      ui::set_text(head_icon_, ui::icon(icon_of(servers_)));
      ui::set_tone(head_icon_, servers_.all(), theme::MUTED);
      ui::set_text(head_, servers_.summary());
      ui::set_tone(head_, servers_.all(), theme::INK);
      const int head_x = ui::line_height(Font::ICON_SMALL) + ui::px(6);
      lv_obj_set_width(head_, std::max(1, width_ - head_x - tally_w - ui::px(6)));
    }
    const int name_x = dot_ + ui::px(10);
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      const Server &s = servers_.list[servers_.order[i]];
      const std::string info = servers_.line(s, epoch);
      const Tone tone = servers_.tone(s);
      ui::set_fill(r.dot, tone, theme::MUTED);
      ui::set_text(r.name, s.name);
      ui::set_color(r.name, s.up == STATE_UNKNOWN || s.up == STATE_PAUSED ? theme::MUTED : theme::INK);  // no data or paused: steps back
      ui::set_text(r.info, info);
      ui::set_tone(r.info, servers_.line_tone(s), theme::MUTED);  // only what is wrong takes a colour
      // The detail is as wide as its words; the name takes the room it leaves, and ends in dots when that is too little.
      const int info_w = info.empty() ? 0 : std::min(width_ * 3 / 5, ui::text_width(info, Font::BODY) + ui::px(2));
      lv_obj_set_width(r.info, std::max(1, info_w));
      lv_obj_set_x(r.info, width_ - info_w);
      lv_obj_set_width(r.name, std::max(1, width_ - name_x - info_w - (info_w ? ui::px(6) : 0)));
    }
  }

  ServerStatus *plugin_;
  Servers servers_;
  int width_ = 0, height_ = 0, tile_ = -1, slots_ = 0, row_h_ = 0, dot_ = 0, top_ = 0, lines_n_ = 0;
  bool single_ = false, with_sub_ = true, with_third_ = false;
  Slot slot_[MAX_SERVERS];
  lv_obj_t *lines_[MAX_SERVERS + 1] = {};
  lv_obj_t *note_{}, *badge_{}, *badge_icon_{}, *big_{}, *sub_{}, *third_{}, *head_icon_{}, *head_{}, *tally_{};
};

// The card a tap on the tile opens: every server on a raised block of its own. Its name with its state in its colour,
// and under it its response time and its uptime over its window, or since when it is down.
class ServersCard : public tessera::Card {
 public:
  explicit ServersCard(const tessera::Plugin *plugin) : plugin_(plugin), servers_(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    const int name_h = ui::line_height(Font::BODY_LARGE), more_h = ui::line_height(Font::BODY);
    pad_ = ui::px(ui::large() ? 12 : 8);
    const int gap = ui::px(ui::large() ? 8 : 5);
    const int block_h = pad_ + name_h + more_h + pad_;
    row_h_ = block_h + gap;
    dot_ = ui::px(ui::large() ? 12 : 9);
    slots_ = std::max(1, std::min(MAX_SERVERS, (c.height + gap) / row_h_));
    inner_w_ = width_ - 2 * pad_;
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      r.block = ui::block(c.parent, theme::RAISED);
      lv_obj_set_size(r.block, width_, block_h);
      lv_obj_set_pos(r.block, 0, i * row_h_);
      r.dot = ui::block(r.block, theme::MUTED);
      lv_obj_set_size(r.dot, dot_, dot_);
      lv_obj_set_style_radius(r.dot, dot_ / 2, 0);
      lv_obj_set_pos(r.dot, pad_, pad_ + (name_h - dot_) / 2);
      r.name = ui::label(r.block, Font::BODY_LARGE, theme::INK);
      lv_obj_set_pos(r.name, pad_ + dot_ + ui::px(10), pad_);
      r.state = ui::label(r.block, Font::BODY_LARGE, theme::MUTED);
      lv_obj_set_style_text_align(r.state, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_y(r.state, pad_);
      r.more = ui::label(r.block, Font::BODY, theme::MUTED);
      lv_obj_set_width(r.more, std::max(1, inner_w_ - dot_ - ui::px(10)));
      lv_obj_set_pos(r.more, pad_ + dot_ + ui::px(10), pad_ + name_h);
    }
    paint(tessera::epoch());
  }

  void on_state(JsonObjectConst data) override { servers_.load(data, true); }

  void on_tick(uint32_t epoch) override { paint(epoch); }

  void on_theme() override {
    for (int i = 0; i < slots_; i++) {
      ui::set_fill(slot_[i].block, Tone::NORMAL, theme::RAISED);
      ui::set_fill(slot_[i].dot, Tone::NORMAL, theme::MUTED);
      ui::set_color(slot_[i].name, theme::INK);
      ui::set_color(slot_[i].state, theme::MUTED);
      ui::set_color(slot_[i].more, theme::MUTED);
    }
    paint(tessera::epoch());
  }

 private:
  struct Slot {
    lv_obj_t *block{}, *dot{}, *name{}, *state{}, *more{};
  };

  // A moment as the screen writes it: the time today, the day with it before.
  static std::string moment_text(uint32_t at) {
    const std::string time = tessera::clock_text(at);
    return tessera::days_from_today(at) == 0 ? time : tessera::date_text(at) + " " + time;
  }

  void paint(uint32_t epoch) {
    const int name_x = pad_ + dot_ + ui::px(10);
    for (int i = 0; i < slots_; i++) {
      Slot &r = slot_[i];
      // The block holds the rest of the row: hiding it hides them all.
      if (i >= servers_.count) {
        lv_obj_add_flag(r.block, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(r.block, LV_OBJ_FLAG_HIDDEN);
      const Server &s = servers_.list[servers_.order[i]];
      const Tone tone = servers_.tone(s);
      const std::string state = servers_.state_text(s, epoch);
      ui::set_fill(r.dot, tone, theme::MUTED);
      ui::set_text(r.name, s.name);
      ui::set_color(r.name, s.up == STATE_UNKNOWN || s.up == STATE_PAUSED ? theme::MUTED : theme::INK);
      ui::set_text(r.state, state);
      ui::set_tone(r.state, tone, theme::MUTED);
      const int state_w = ui::text_width(state, Font::BODY_LARGE) + ui::px(2);
      lv_obj_set_width(r.state, std::max(1, state_w));
      lv_obj_set_x(r.state, width_ - pad_ - state_w);
      lv_obj_set_width(r.name, std::max(1, width_ - pad_ - name_x - state_w - ui::px(8)));
      // The second line. Up: its response time and its uptime ("152 ms · 99.998% over 30 days"). Else since when it
      // is as it is.
      std::string more;
      if (s.up == STATE_UP) {
        more = s.has_info ? servers_.detail(s, epoch) : "";
        std::string pct = servers_.uptime_text(s);
        if (!pct.empty() && !s.window.empty())
          pct = tessera::fill(tessera::fill(plugin_->text("uptime_over"), "pct", pct).c_str(), "window", s.window);
        if (!pct.empty()) more += (more.empty() ? "" : " \u00B7 ") + pct;
      } else if (s.since && epoch && s.up != STATE_UNKNOWN) {
        more = tessera::fill(plugin_->text("since"), "when", moment_text(s.since));
      }
      ui::set_text(r.more, more);
    }
  }

  const tessera::Plugin *plugin_;
  Servers servers_;
  int width_ = 0, inner_w_ = 0, slots_ = 0, row_h_ = 0, dot_ = 0, pad_ = 0;
  Slot slot_[MAX_SERVERS];
};

void ServerStatus::report(int tile, int down, const std::string &first) {
  Report &r = reports_[tile];
  r.down = down;
  r.first = first;
}

// "nas down" in red in the top bar while a tile on the screen has a server down; nothing while all is well.
tessera::BarItem ServerStatus::bar() const {
  tessera::BarItem item;
  int down = 0;
  std::string first;
  for (const auto &entry : reports_) {
    if (entry.second.down && first.empty()) first = entry.second.first;
    down += entry.second.down;
  }
  if (!down) return item;
  item.shown = true;
  item.icon = ICON_DOWN;
  item.tone = Tone::ALERT;
  item.text = down == 1 ? tessera::fill(text("one_down"), "name", first) : tessera::format(text("bar_some"), static_cast<long>(down));
  return item;
}

void ServerStatus::setup() {
  // The ids are the tile's, the card's and the top bar item's in the manifest.
  add_tile("servers", [this]() { return new ServersTile(this); });
  add_card("servers", [this]() { return new ServersCard(this); });
  add_bar_item("outage", [this]() { return bar(); });
}

}  // namespace esphome::server_status
