# Server status

A tile that shows whether your servers and services are up, with their response time. It reads Home Assistant
entities from **Uptime Kuma** (or the **Ping** integration), so it needs no internet service and no network permission.

## Set up

1. Add the Uptime Kuma integration to Home Assistant. Each monitor gets a **Status** sensor and a **Response time**
   sensor.
2. Add the plugin to a screen in Tessera. The screen builds once.
3. In Layout, place the **Server status** tile from the library's Plugins group.
4. In the inspector, for each server (up to four per tile; add another tile for more) choose:
   - **status**: the monitor's Status sensor (or a Ping `binary_sensor`). Search by its name, such as `nas Status`.
   - **response time** (optional): the monitor's Response time sensor.
   - **uptime** (optional): one of the monitor's Uptime sensors, such as `nas Uptime (30 days)`. Uptime Kuma
     leaves these disabled: enable them on the integration's page in Home Assistant.
5. Optional: in a page's top bar, add **Server outage** from the plugins. It shows only while a server is down.

## Good to know

- **A single cell** is a status badge: a round block, green with a check when all is up, amber when a ping is slow,
  red with a cross when something is down, grey with a dash without data. Under it the server's name and its ping
  time or uptime, or for several servers what is down and how many are up (`4/5`).
- **A larger tile** lists the servers: a green dot is up, amber is slow (100 ms or more) or pending (Uptime Kuma
  retrying), red is down, grey is in maintenance or has no data. Servers that are down come
  first and say for how long (`Down 12m`), so an outage stays in view on a tile too small for all of them.
- **A tap** opens a card with every server: its state, its ping time or uptime, and since when it is up or down.
- A ping time of 100 ms or more (a detail sensor in `ms`) shows in amber.
- A row shows the response time and the uptime: `<1 ms · 99.998%`. A ping on the home network that Uptime Kuma
  rounds to 0 shows as `<1 ms`. An uptime below 99.9% shows in amber.
- The card says over which window the uptime is: `152 ms · 99.998% over 30 days`.
- The top bar item knows the servers of the tiles on the screen. On a board that keeps only the current page (the
  2.8-inch CYD), it knows them while their page is shown.
- The editor finds entities by their name, not their entity id.
- Ping's round-trip-time sensors are often disabled by default in Home Assistant: enable them on the integration's page.
