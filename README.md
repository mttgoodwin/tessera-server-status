# Server status

A tile that shows whether your servers are up, with ping time or uptime beside each. It reads Home Assistant entities
from the **Ping** and **Glances** integrations, so it needs no internet service and no network permission.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Server status** tile from the library's Plugins group.
3. In the inspector, for each server (up to four) choose:
   - **status**: the Ping `binary_sensor` (on = up, off = down).
   - **ping time or uptime** (optional): the Ping round-trip-time sensor, or the Glances uptime sensor.

## Good to know

- A green dot is up, red is down, grey is unknown or unavailable. A server that is down says "Down" in place of its detail.
- A detail sensor that holds a number shows it with its unit (`1.8 ms`). One that holds a boot time (the Glances
  uptime sensor) shows how long the machine has been up (`12d 4h`).
- The tile shows as many servers as fit its size.
- Ping's round-trip-time sensors are often disabled by default in Home Assistant: enable them on the integration's page.
