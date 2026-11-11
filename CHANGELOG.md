# Changelog

## 0.3.0 - 2026-11-11
- **Uptime Kuma.** Its Status sensors (up, down, pending, maintenance) work as a server's status, beside Ping.
- **Uptime.** Each server has how long it is up (any sensor with a start time or a duration, such as Glances' Uptime:
  `11d 6h`) and its uptime percentage (Uptime Kuma's 1, 30 or 365 days; below 99.9% in amber). Four servers per tile.
- **A new look,** as Tessera's own tiles: bold names, muted details, colour only where something is wrong.
  - A single cell is a tinted round badge with a check, a cross, dots or a dash.
  - A larger tile: what is the matter and "3 of 4 up", then rows with two aligned columns that share the tile's height.
  - The card shows each server on a block of its own with a small status badge.
- **Top bar item:** the server that is down, in red, only while one is.
- The plugin's icon is now one of Tessera's (`lan-connect`).

## 0.2.0 - 2026-11-11
- A single cell shows how many servers are up, big, in green or red, and which one is down.
- Larger tiles have a header, and list the servers that are down first.
- A server that is down says for how long.
- A tap opens a card with every server: its state, its ping time or uptime, and since when.
- A ping time of 100 ms or more shows in amber.
- Up to six servers, with a hint under each choice in the editor (four from 0.3.0).

## 0.1.0 - 2026-11-11
- First version: a tile with up to four servers, each with a status dot and ping time or uptime.
