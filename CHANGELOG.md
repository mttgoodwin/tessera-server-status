# Changelog

## 0.3.0 - 2026-11-11
- Uptime Kuma: its Status sensors (up, down, pending, maintenance) work as a server's status, beside Ping.
- Pending shows in amber, maintenance in grey; a response time of 0 ms shows as <1 ms.
- Four servers per tile, each with a status, how long it is up (any sensor with a start time or a duration, such as
  Glances' Uptime: `12d 4h`) and its uptime percentage (Uptime Kuma, 1, 30 or 365 days; below 99.9% in amber).
- The 1x1 tile of one server shows both, where there is room.
- A top bar item: the server that is down, in red, only while one is.
- A single cell is a status badge: a round block in green, amber, red or grey with a check, a cross or a dash in it,
  and the name (or the summary) and the ping time or uptime under it.
- An icon with the status in the header of a larger tile.
- Thin lines between the rows, a little more room on large screens, and a server without data in a softer colour.
- The card shows each server on a raised block of its own.
- The plugin's icon is now one of Tessera's (lan-connect).

## 0.2.0 - 2026-11-11
- A single cell shows how many servers are up, big, in green or red, and which one is down.
- Larger tiles have a header, and list the servers that are down first.
- A server that is down says for how long.
- A tap opens a card with every server: its state, its ping time or uptime, and since when.
- A ping time of 100 ms or more shows in amber.
- Up to six servers, with a hint under each choice in the editor (four from 0.3.0).

## 0.1.0 - 2026-11-11
- First version: a tile with up to four servers, each with a status dot and ping time or uptime.
