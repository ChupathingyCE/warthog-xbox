# XLink Kai

[XLink Kai](https://www.teamxlink.co.uk/) carries consoles' System Link
over the internet. CrunchBite (XLink Kai's developer, on the ChupathingyCE
Discord) described the addressing that lets Kai pass a console's traffic:

- take the console's MAC address, `AA:BB:CC:DD:EE:FF`;
- give the console the address `10.252.EE.FF` with the mask `255.255.0.0`
  (its last two bytes, in decimal: `10.252.238.255` here);
- Kai then carries it across its tunnel. The rule is the same for the Xbox,
  360, One and Series; XLink's [IP calculator](https://teamxlink.co.uk/ipcalculator/)
  works it out.

## Using it today

1. Set `trace = true` under `[console]` in `config.toml` and start the game
   once. `debug.txt` then gives the address Kai expects for your console.
2. In the dashboard's network settings, set that address by hand, with the
   mask `255.255.0.0`. Keep your network's gateway and DNS.
3. Start the game. `debug.txt` says whether the console's address is Kai's.

Kai is for System Link between consoles. Online Games and play with PCs
use the internet play tunnel instead, with `console.bypass_security = true`.

Warthog speaks the PC builds' network version, so it doesn't find retail
Halo's System Link games.

## What is built

Only the rule and a check:

- `port/xbox/src/xbox_xlink.c`: `xbox_xlink_address` gives the address and
  mask for a MAC, `xbox_xlink_matches` says whether an address is it, and
  `xbox_xlink_report` writes the log's line. Tested on the host
  (`port/xbox/tests/run.sh`, `xlink_test`).
- At the start, `transport_initialize` (in
  `source/bungie_net/network/transport_endpoint_set_winsock.c`) writes one
  line to `debug.txt`: whether the console's address is Kai's for its MAC,
  yes or no. What Kai's would be is in the line only with `console.trace`,
  since it carries the MAC's last two bytes. The MAC itself is never
  logged.
- The edge MACs (last two bytes `00 00` or `FF FF`) give the /16's network
  or broadcast address, so `xbox_xlink_address` refuses them.

The game doesn't set the address itself. You set it in the dashboard.
