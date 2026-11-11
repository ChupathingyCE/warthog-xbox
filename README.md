<p align="center"><img src="docs/icon-160.png" width="120" alt=""></p>

<h1 align="center">Warthog</h1>

<p align="center"><b>Chupathingy on the original Xbox.</b></p>

<p align="center">
<a href="https://discord.gg/chupathingy">Get the test build (Discord, #xbox)</a> ·
<a href="https://stats.chupathingy.org">Games online now</a> ·
<a href="https://github.com/ChupathingyCE/chupathingyce">The game</a> ·
<a href="https://chupathingy.org">chupathingy.org</a>
</p>

Play Chupathingy on the console Halo was made for. A softmodded or modded
original Xbox joins the same online games as PCs, Macs, phones and other
consoles. Open **Multiplayer > Online Games** and pick a game. System Link
works too.

It runs on a stock 64 MB console. A console with 128 MB gets a little more
room, and the game plays the same either way.

**Status: test builds.** Test builds are shared in #xbox on the
[Discord](https://discord.gg/chupathingy). You can also build it yourself
(below).

You need your own copy of Halo. No game files or SDK files are included.

This repository holds the original Xbox port. The game itself is the
`chupathingyce` submodule. The other consoles have their own repositories:
[warthog-360](https://github.com/ChupathingyCE/warthog-360),
[warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu),
[warthog-switch](https://github.com/ChupathingyCE/warthog-switch) and
[warthog-ps3](https://github.com/ChupathingyCE/warthog-ps3).

## What you need

- **A console that runs your own code.** A softmodded or hardmodded
  original Xbox, or a development kit. xemu works for testing.
- **Your own Halo disc's maps.** Use the North American (NTSC) maps,
  01.10.12.2276, as every Chupathingy build and server does. The PAL disc's
  maps work too. Warthog never provides maps, disc images or other game
  data, and never links to them. Don't share yours.
- **A way to copy files to the console**, such as your dashboard's FTP.
- **To build it yourself** (below): a Mac or Linux computer, your own copy
  of the Xbox SDK, and your own Aug 2001 `d3d8ntpr.lib`.

## Install

The first time:

1. Get the newest test build from #xbox on the
   [Discord](https://discord.gg/chupathingy) and unzip it.
2. Make a folder for it on the console's hard disk, for example
   `E:\Games\Warthog\`, and copy `default.xbe` into it.
3. Copy the `maps` folder from your own Halo disc into the same folder, so
   you have `E:\Games\Warthog\maps\ui.map` and the rest.
4. Make a text file called `config.toml` in the same folder, with these two
   lines. Online Games and playing with PCs need them.

   ```toml
   [console]
   bypass_security = true
   ```

5. Start `default.xbe` from your dashboard.

To update, keep a copy of your old `default.xbe`, then replace it with the
new one. Keep `config.toml` and your maps.

If you build it yourself, `tools/xbox_package.py` makes the whole folder
for you, `config.toml` included (see "Building it yourself").

## What works

- **Online Games:** lists the games online and joins them, dedicated
  servers too. It joins OpenCE's newest servers (network version 41) and
  older ones.
- **System Link** with PCs and consoles on your network.
- **Up to 32 players** in the test builds.
- **Link your Delta profile:** on Online Games, press **Start**, then **A**
  for a code. Enter the code at stats.chupathingy.org/connect, signed in on
  your phone or computer, then press **A** on the console. The games you
  join then count on your service record.
- **Maps:** the retail maps, the Halo CE+ map pack, and Halo PC's Custom
  Edition maps ([below](#custom-edition-maps)).
- **Map Browser:** **Multiplayer > MAP BROWSER** downloads Custom Edition
  maps, and Online Games offers a missing map when you join.
- **Split screen** and the campaign, as on the disc. When you join a game
  in progress, the host waits 5 seconds for your other controllers. Press
  **A** on them right away. The host needs a current build for this.
- **Chat:** quick lines on the controller, and a USB keyboard
  ([below](#chat-and-keyboard)).
- **Sound:** the original Xbox reverb.
- **Widescreen and 480p** follow the dashboard's video settings.
  Widescreen is new in the November test builds and still being tested.
- **60 frames a second** as a setting (`display.frame_rate = 60`). It is
  new and still being tested. The game still runs 30 ticks a second, so it
  plays the same online.

## Known issues

- **No movies.** The intro and other Bink videos are skipped.
- **The loading screen doesn't animate.**
- **No hosting for PC players over the internet**, and no online co-op.
  Co-op games are left out of the console's lists.
- **Widescreen on your TV.** The console doesn't tell the TV that the
  picture is wide. If you see black bars at the sides, set the TV's picture
  size to Wide, Full, 16:9 or Stretch. With a TV that can't stretch it,
  keep Normal in the dashboard, or set `display.widescreen = "off"` in
  `config.toml`.
- **Zoom frame rate.** Sniper and pistol zoom drop to about 20 fps.
- **First load of a map** after the console starts takes longer than
  later loads.
- **Max players.** A game that grows past 32 players is left, as a full
  one is.
- **Custom Edition maps on a stock 64 MB console are experimental.** A map
  too large for its memory is refused with a message. Their HUD button
  icons are missing.
- **HaloMD maps** aren't played on the console.
- **Split screen players who join late** can't change profile or team on
  the pregame screen
  ([#83](https://github.com/ChupathingyCE/warthog-xbox/issues/83)).

## Troubleshooting

- **The game doesn't reach the menus.** Check that the `maps` folder, with
  `ui.map` in it, is next to `default.xbe`. Then send `debug.txt` (below).
- **Online Games says "Online play needs console.bypass_security."** Add
  `bypass_security = true` under `[console]` in `config.toml`.
- **"Could not reach the game list."** Check the dashboard's network
  settings. If your DNS can't find the list, set `console.game_list_server`
  ([docs/settings.md](docs/settings.md)).
- **A game says its map is missing.** A Custom Edition map is offered for
  download. A Halo CE+ map needs the pack in `maps_cex`
  ([below](#xbox-map-packs-halo-ce)).
- **A Custom Edition map is refused.** On a stock 64 MB console, large maps
  don't fit. Check that `bitmaps.map`, `sounds.map` and `loc.map` are in
  `maps_ce` too.
- **Black bars at the sides in widescreen.** See "Known issues" above.
- **Choppy at 60 frames a second.** Set `display.frame_rate = 30`.

## Reporting problems

1. Right after the problem, copy `debug.txt` from the game's folder (it's
   `D:\debug.txt` to the game) to your computer.
2. Send it at [dump.chupathingy.org](https://dump.chupathingy.org), with
   "Original Xbox" as the platform. You get a reference code.
3. Tell us in #warthog-support on the
   [Discord](https://discord.gg/chupathingy) what you were doing, and give
   the code.

For a join that fails, set `trace = true` under `[console]` in
`config.toml` first. It adds the network's steps to `debug.txt`, without
anyone's address.

## Settings on the console

The settings are in `config.toml`, next to `default.xbe`. The game calls
that folder `D:\`. If there's no `config.toml`, the first start writes one
with every setting, commented out at its default. Remove the `# ` in front
of one, change its value and restart the game. Edit it over FTP, or on your
computer before you copy the folder.

```toml
[console]
bypass_security = true

[display]
frame_rate = 60
```

The ones players use most:

| Setting | What it does |
| --- | --- |
| `console.bypass_security` | `true` is needed for Online Games and playing with PCs. |
| `display.widescreen` | `"off"` keeps the 4:3 picture even with Widescreen on in the dashboard. |
| `display.frame_rate` | `60` turns the view every frame. `30`, the default, draws as the original game did. |
| `display.flicker_filter` | From 0 to 5. Lower is sharper on a 480i TV, with more flicker. The default is 5. |
| `display.field_of_view` | From 50 to 120. The default is 70. |
| `network.report_joined_games` | `false` keeps your games off your service record. |
| `console.trace` | `true` adds a join's network steps to `debug.txt`, for a bug report. |

[docs/settings.md](docs/settings.md) lists every setting the console reads.
[port/xbox/FEATURES.md](port/xbox/FEATURES.md) shows which PC features the
console has.

## Chat and keyboard

Chat works in a game whose host runs Chupathingy.

- **Controller:** **D-pad up** picks a quick line, **D-pad down** sends it.
- **USB keyboard** (on a controller port): **T** talks to everyone, **Y** to
  your team, **H** shows the recent lines.

## Map packs and Custom Edition maps

### Xbox map packs (Halo CE+)

Warthog plays community packs of Xbox maps, such as Halo CE+ X (Original
Xbox) by VKMT, beside your retail maps. A pack's maps reuse the retail
maps' file names, so each pack has a folder of its own beside `maps`:
Halo CE+ goes in `D:\maps_cex\`. Copy the contents of the pack's `maps`
folder there, its `maplist.txt` included, and keep the retail maps in
`D:\maps\`. With the packaging script:
`python3 tools/xbox_package.py --maps /path/to/maps --map-pack cex="/path/to/Halo CE+ X/maps" --out ...`.

The pack's maps appear in the multiplayer map list after the retail ones,
marked [CE+] and named as the pack names them (Abyss, Gothra, ...). Online,
a CE+ game is joined only with the same version of the pack, and a console
without it is told which map and folder it needs. The larger CE+ maps use a
campaign map's cache file on the utility partition. Warthog never provides
the pack's files: get them from the pack's release page. The pack's own
menus (`ui.map`) stay unused unless `game.pack_menus = "cex"` is set in
`D:\config.toml`.

### Custom Edition maps

Custom Edition maps go in `D:\maps_ce\`, with no conversion. Their
`bitmaps.map`, `sounds.map` and `loc.map` go beside them. They show in the
multiplayer map list marked [CE], and Online Games lists the Custom Edition
games hosted by PCs and dedicated servers.

The easy way to get them is **Multiplayer > MAP BROWSER**. It lists the
[xboxhomebrew.store](https://xboxhomebrew.store) archive (halomaps.org's
maps) and the store's Xbox map packs, with a picture and details for each.
**A** installs a map, **Y** removes it, and **X** searches. Each download is
checked against the store's hash. When you join a game whose map you don't
have, Online Games offers to download it first.

A console with 128 MB plays them. On a stock 64 MB console they are
experimental: most maps load, and a map too large for the console's memory
is refused with a message. A map without Custom Edition's `bitmaps.map`,
`sounds.map` and `loc.map` beside it is refused with a message too.
[docs/custom-edition.md](docs/custom-edition.md) has the details.

## Building it yourself

Clone with the game:

```sh
git clone --recursive https://github.com/ChupathingyCE/warthog-xbox.git
cd warthog-xbox
```

(In a clone you already have: `git submodule update --init`.)

You need:
- Python 3, [ninja](https://ninja-build.org/) and LLVM (clang and lld-link).
  On macOS: `brew install llvm lld ninja`. On Linux, see the Linux section
  of [docs/building.md](docs/building.md).
- [OXDK](https://github.com/MrMilenko/OXDK) in `~/OXDK` (or `--oxdk DIR`),
  with its `cxbe` built, and your own 5933 Xbox SDK's libraries and headers
  in OXDK's `xbox/xdk/lib` and `xbox/xdk/include`.
- Your own Aug 2001 Direct3D library, `d3d8ntpr.lib` (the `aug01` i386
  build).

Build:

```sh
python3 chupathingyce/configure.py --console-port port/xbox \
    --xbox-d3d8 /path/to/d3d8ntpr.lib --console-no-debug-asserts --console-players 32
ninja xbox                          # build/xbox/default.xbe
port/xbox/tests/run.sh --xbox       # the host tests
```

Or let `python3 tools/easy_build.py` check your tools, ask a few questions,
build and package (`--check` only checks).

Notes:
- `--console-players 32` is the test build. Without it the game's
  configure builds 16, as the Xbox's own game had.
- The Xbox 360 port is
  [ChupathingyCE/warthog-360](https://github.com/ChupathingyCE/warthog-360).
- macOS and Linux build it. Windows: no build path yet. Help is welcome in
  #warthog-dev on the Discord.
- Nothing from Microsoft's SDKs is in this repository, and none of it may be
  committed.

[docs/building.md](docs/building.md) has every step, the options, the
versions tested and how to update the game submodule. Changes to the game
itself go to [ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce)
([CONTRIBUTING.md](CONTRIBUTING.md)).

## License

Warthog is free software: you can redistribute it and modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License or (at your option) any later
version (`GPL-3.0-or-later`), with the console SDK permission below. It comes
with no warranty. The full text is in [LICENSE](LICENSE).

- **From the GPL change onward.** Warthog follows Chupathingy, which is GPL
  from release 0.8.0. The source at every commit before the change was
  released under [CC0 1.0](LICENSES/CC0-1.0.txt) and stays that way.
- **What Warthog builds on.** The Halo CE decompilation
  ([punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1)) and OpenCE are released
  under CC0 1.0, which lets anyone build on them under any terms. Their
  repositories stay CC0.
- **The game** (the `chupathingyce` submodule) is under its own
  repository's license, with its third-party code, fonts and art under
  theirs.
- **Halo itself is not covered.** The license covers the work of Warthog's
  and Chupathingy's authors. It gives no rights in Halo, its maps or
  anything else Microsoft owns, and no SDK is included.

### Console SDK permission

You may build Warthog yourself with a console maker's SDK (see "Building it
yourself"), and share what you build:

> **Additional permission under GNU GPL version 3 section 7**
>
> If you modify this Program, or any covered work, by linking or combining
> it with the libraries of a Microsoft Xbox or Xbox 360 software development
> kit (such as the Xbox Development Kit and the Xbox 360 SDK, including
> their `xnet.lib` and Direct3D libraries), or a modified version of those
> libraries, containing parts covered by the terms of those kits' licenses,
> the licensors of this Program grant you additional permission to convey
> the resulting work. Corresponding Source for a non-source form of such a
> combination shall include the source code of the covered work, and need
> not include the source code of those libraries.

The permission only lifts the GPL's own objection. It grants no rights in
Microsoft's libraries, whose own terms still apply, and you may remove it
from your copies (section 7). Warthog's own published builds are made only
with free, legally available tools and libraries; none contains code from
Microsoft's SDKs.

## Support

Chupathingy is free and always will be. Support through
[Ko-fi](https://ko-fi.com/milenko) goes to running the site and the servers. If
there's a specific developer or port you'd like to support, ask them: they may
not be part of our community.

## Credits

- The decompilation: [punpckhdq/halo](https://github.com/punpckhdq/halo) and
  [bnunu/halo-1](https://github.com/bnunu/halo-1), of the Xbox build 2342.
- The game: [Chupathingy](https://github.com/ChupathingyCE/chupathingyce),
  and OpenCE and its contributors, up to October 9, 2026.
- Warthog: [Milenko](https://github.com/MrMilenko) and contributors. The
  toolchain: [OXDK](https://github.com/MrMilenko/OXDK).
- Libraries: KCP and Monocypher (internet play), zlib, and the libraries
  the game uses, with their licenses in the game's `port/third_party`.
  [jsmn](https://github.com/zserge/jsmn) by Serge Zaitsev (MIT), the Map
  Browser's JSON parser (`port/xbox/browser/jsmn.h`).

### Contributors

- [JizzyRivers](https://github.com/JizzyRivers): Custom Edition maps on the
  console, the Map Browser, maps offered on join, and the Linux build fixes.
- [thelinkin3000](https://github.com/thelinkin3000): the Nintendo Switch
  port, now in [ChupathingyCE/warthog-switch](https://github.com/ChupathingyCE/warthog-switch).
- CrunchBite (XLink Kai's developer): XLink Kai's addressing for consoles
  ([docs/xlink.md](docs/xlink.md)), and the idea of checking the console's
  memory and saying so on screen instead of crashing.
- vsod99: the xemu development loop and the memory probe behind the
  cross-play plan.

## Links

- The game: [ChupathingyCE/chupathingyce](https://github.com/ChupathingyCE/chupathingyce)
- Games online now: [stats.chupathingy.org](https://stats.chupathingy.org)
- Discord: [discord.gg/chupathingy](https://discord.gg/chupathingy)
  (#warthog-support for help, #warthog-dev for development)

Warthog is an unofficial fan project. It isn't affiliated with or endorsed
by Microsoft, Xbox Game Studios, Bungie or Halo Studios. Halo is a trademark
of Microsoft. No game files are included: you bring your own copy. The code is released under the GNU GPL, version 3 or later
(see [License](#license)).
