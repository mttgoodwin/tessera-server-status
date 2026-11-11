# Server status

A [Tessera](https://github.com/MaxGramser/homeassistant_espscreen) plugin that shows at a glance whether your servers
and services are up, how long they have been up, and their uptime percentage. It reads Home Assistant entities from
**Uptime Kuma** (or the **Ping** integration), so it needs no internet service and no network permission.

- **Glanceable:** a single cell is a status badge; a larger tile lists up to four servers, worst first.
- **Quiet when all is well:** colour only appears where something is wrong.
- **Detail on tap:** a card with every server, its uptime and since when it is up or down.
- **Outage in the top bar:** an optional item that appears only while a server is down.

## Install

In the Tessera app, open **Plugins**, choose **Add with a link** and paste:

```
https://github.com/mttgoodwin/tessera-server-status
```

Then add **Server status** to a screen. The screen builds once.

## Set up

1. Add the [Uptime Kuma integration](https://www.home-assistant.io/integrations/uptime_kuma/) to Home Assistant. Each
   monitor gets a **Status** sensor. Its **Uptime** sensors (1, 30 and 365 days) are disabled by default: enable the
   ones you want on the integration's page.
2. In Layout, place the **Server status** tile from the library's Plugins group.
3. In the inspector, for each server (up to four per tile; add another tile for more) choose:

   | Setting | What to pick | Shows |
   |---|---|---|
   | **Server *n*** | The monitor's Status sensor, such as `nas Status`, or a Ping `binary_sensor` | The colour and the icon |
   | **uptime (time)** *(optional)* | Any sensor with when the machine started or how long it has been up: Glances' Uptime, System Monitor, a template | `11d 6h` |
   | **uptime %** *(optional)* | One of the monitor's Uptime sensors, such as `nas Uptime (30 days)` | `99.99%` |

   The editor finds entities by their name, not their entity id.
4. Optional: in a page's top bar, add **Server outage** from the plugins.

## What it shows

| State | Colour | Icon |
|---|---|---|
| Up | green | check |
| Up, but uptime below 99.9% (or a ping of 100 ms or more) | amber text | check |
| Pending (Uptime Kuma is retrying) | amber | dots |
| Down | red, with how long (`Down 12m`) | cross |
| Maintenance, or no data | grey | dash |

- **A single cell** is a tinted round badge with the state's icon. One server: its name, how long it is up and its
  uptime under it. Several: what is the matter ("All up", "nas down") and how many are up.
- **A larger tile** says what is the matter and "3 of 4 up", then a row per server: a dot, its name, and how long it is
  up and its uptime in two aligned columns. Servers that are down come first, so an outage stays in view on a tile too
  small for all of them.
- **The card** (tap the tile) shows each server on a block of its own with a small status badge, and under its name
  `11d 6h · 99.99% over 365 days`, or `Down 12m · since 14:02`.
- **The top bar item** shows the server that is down, in red, and nothing while all are up. It knows the servers of
  the tiles on that screen; on a board that keeps only the current page (the 2.8-inch CYD), while their page is shown.

## Develop

The plugin is an ESPHome component built into the screen's firmware; [MAKING_A_PLUGIN.md](https://github.com/MaxGramser/tessera-plugins/blob/main/docs/MAKING_A_PLUGIN.md)
explains the parts.

| File | What |
|---|---|
| `tessera-plugin.yaml` | The manifest: the tile and its twelve entity options, the card, the top bar item |
| `components/server_status/server_status.cpp` | The tile, the card and the top bar item |
| `translations/en.json` | Every word on the screen and in the editor |
| `preview.yaml` | Sample data for the plugins repository's `tools/preview.py` |

For a quick round without GitHub, copy the folder into Home Assistant's config as `tessera-plugins/server_status/` and
press **Build again** on the screen's Plugins tab after each change. In a Docker install of Tessera whose ESPHome
config is mounted at `/config`, the app looks for that folder at `/tessera-plugins` in its container: mount it there.

## License

MIT
