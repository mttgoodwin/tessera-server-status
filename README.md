# Server status

A tile that shows whether your servers are up, with ping time or uptime beside each. It reads Home Assistant entities
from the **Ping** and **Glances** integrations, so it needs no internet service and no network permission.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Server status** tile from the library's Plugins group.
3. In the inspector, for each server (up to six) choose:
   - **status**: the Ping `binary_sensor` (on = up, off = down).
   - **ping time or uptime** (optional): the Ping round-trip-time sensor, or the Glances uptime sensor.
4. Optional: in a page's top bar, add **Server outage** from the plugins. It shows only while a server is down.

## Good to know

- **A single cell** is a status badge: a round block, green with a check when all is up, amber when a ping is slow,
  red with a cross when something is down, grey with a dash without data. Under it the server's name and its ping
  time or uptime, or for several servers what is down and how many are up (`4/5`).
- **A larger tile** lists the servers: a green dot is up, red is down, grey has no data. Servers that are down come
  first and say for how long (`Down 12m`), so an outage stays in view on a tile too small for all of them.
- **A tap** opens a card with every server: its state, its ping time or uptime, and since when it is up or down.
- A ping time of 100 ms or more (a detail sensor in `ms`) shows in amber.
- A detail sensor that holds a number shows it with its unit (`1.8 ms`). One that holds a moment (the Glances uptime
  sensor holds when the machine started) shows how long ago that was (`12d 4h`).
- The top bar item knows the servers of the tiles on the screen. On a board that keeps only the current page (the
  2.8-inch CYD), it knows them while their page is shown.
- The editor finds entities by their name, not their entity id.
- Ping's round-trip-time sensors are often disabled by default in Home Assistant: enable them on the integration's page.
