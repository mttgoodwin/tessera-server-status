#pragma once
#include <map>
#include <string>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::server_status {

// The plugin: an ESPHome component and a tessera::Plugin. It registers the tile, its card and the top bar item.
class ServerStatus : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // What each tile on the screen last saw, for the top bar: how many of its servers are down, and the first one's name.
  // A tile reports on every change and forgets its report when it goes.
  void report(int tile, int down, const std::string &first);
  void forget(int tile) { reports_.erase(tile); }

 protected:
  tessera::BarItem bar() const;

  struct Report {
    int down = 0;
    std::string first;
  };
  std::map<int, Report> reports_;
};

}  // namespace esphome::server_status
