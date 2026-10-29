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
original Xbox joins the same online games as PCs, Macs and phones. Open
**Multiplayer > Online Games** and pick a game. System Link works too.

It runs on a stock 64 MB console. A console with 128 MB gets a little more
room, and the game plays the same either way.

**Status: test builds.** Online play, up to 32 players and Halo CE+ maps
work today. Hosting PC players, online co-op and movies aren't in yet.

Test builds are shared in #xbox on the [Discord](https://discord.gg/chupathingy).
You can also build it yourself (below).

You need your own copy of Halo. No game files or SDK files are included.

This repository holds the original Xbox port. The game itself is the
`chupathingyce` submodule. The other consoles have their own repositories:
[warthog-360](https://github.com/ChupathingyCE/warthog-360),
[warthog-wiiu](https://github.com/ChupathingyCE/warthog-wiiu) and
[warthog-switch](https://github.com/ChupathingyCE/warthog-switch).

## Details

- Network version: 32 (wire `chupa-32a`). Joins hosts of network versions
  11 through 33.
- Online: **Multiplayer > ONLINE GAMES** lists the games online and joins
  them through the PCs' own tunnel, dedicated servers too.
- System Link: joins PC hosts and consoles on your network.
- Memory: 64 MB is the baseline. A console with 128 MB or more (a devkit,
  or the common upgrade) gets larger texture and sound caches; the game
  plays the same either way.
- Maps: the retail maps, and community packs of Xbox maps beside them, Halo CE+ in `D:\maps_cex` ([below](#xbox-map-packs-halo-ce)).
- Players: the test build holds 32 players. Games past that are left, as a
  full game is.
- Chat: quick lines on the controller, and a USB keyboard.
- Not yet: hosting PC players, network co-op, Bink movies, reverb.

## What you need

- **A console that runs your own code.** A softmodded or hardmodded
  original Xbox, or a development kit. xemu works for testing.
- **Your own Halo disc's maps.** The North American (NTSC) release,
  01.10.12.2276, as every Chupathingy build and server uses. The PAL disc's
  maps work too. Warthog never provides maps, disc images or other game
  data, and never links to them. Don't share yours.
- **To build it** (below): a Mac or Linux computer, your own copy of the
  Xbox SDK, and your own Aug 2001 `d3d8ntpr.lib`.

## Install

1. Build `default.xbe` (see "Building it yourself").
2. Copy the `maps` folder from your own disc (about 2 GB) to your computer.
3. Lay the game out for the console's hard disk:
   `python3 tools/xbox_package.py --maps /path/to/maps --out ~/Downloads/Warthog-xbox`.
   This puts `default.xbe`, your maps and a `config.toml` in one folder.
4. Copy that folder to the console, for example to `E:\Games\Warthog\`
   (FTP from your dashboard, or a devkit's tools).
5. Start `default.xbe` from the dashboard.

`tools/easy_build.py` does steps 1 and 3 for you, with questions.

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

## Settings on the console

The settings are in `D:\config.toml` (`D:\` is the game's folder, beside
`default.xbe`), the same kind of file as the PC builds'. The first start
writes every setting, commented out at its default. Remove the `# ` in
front of one, change its value and restart the game. Edit it over FTP, or
on the computer before copying the folder.

```toml
[console]
bypass_security = true
join = "192.168.1.20"
```

| Setting (`[console]`) | What it does |
| --- | --- |
| `bypass_security` | `true` starts the network in its open mode, which the internet and the PC builds need. The packaging script writes `true`; `--secure` writes `false` for console-only System Link. |
| `game_list_server` | A host or address (`:port` optional): where to get the game list if `warthog.milenko.org` doesn't resolve. |
| `join` | A host's LAN address, or an invite (`halo://join/...`). System Link joins the address, ONLINE GAMES' **Y** the invite. |
| `trace` | `true` writes detailed network traces to `debug.txt`, for reporting a join that fails. Other hosts appear only as tags, never by their addresses. |
| `memory_class` | `"auto"` (the default): a console with 128 MB or more gets the extras, such as larger texture and sound caches; a stock 64 MB console keeps the Xbox's sizes. `"64"` gives any console the stock sizes, to test the 64 MB baseline or if an extra misbehaves. |
| `perf_log` | `true` writes a `perf:` line to `debug.txt` once a second: the frame time and each part of the frame. For finding what makes a scene slow. |
| `debug_geometry` | For development: `true` brings back the debug build's debug lines and triangles, whose buffers take 1.4 MB. Off by default. |
| `log_flush` | When `debug.txt` is written: `"buffered"` (the default) once a second and at once for a crash; `"every_line"` each line as it comes (slow), for a crash hunt. |
| `large_caches` | Deprecated and ignored: `memory_class` decides now. |
| `simulate_64mb` | Deprecated and ignored. A devkit gets 64 MB from the executable's flag: `tools/xbox_package.py --devkit-64mb`. |

Many of the PC builds' settings work on the console too: field of view, the
follow camera, chat, the scoreboard, music and effects volume, the network
ones. [port/xbox/README.md](port/xbox/README.md) lists them, and
[port/xbox/FEATURES.md](port/xbox/FEATURES.md) shows which PC features the
console has.

`brokers.txt` beside `default.xbe` lists the signalling servers internet
play uses, one `host:port` a line, if the built-in ones can't be reached.

The game writes its log to `D:\debug.txt`. Attach it to bug reports.

## Chat and keyboard

Chat works in a game whose host runs Chupathingy.

- **Controller:** **D-pad up** picks a quick line, **D-pad down** sends it.
- **USB keyboard** (on a controller port): **T** talks to everyone, **Y** to
  your team, **H** shows the recent lines.

## Known quirks

- **Zoom frame rate.** Sniper and pistol zoom drop the frame rate to about
  20 fps. Being measured (`perf_log`); not fixed yet.
- **First join of a map.** The first time a map loads after the console
  starts, it takes longer than later loads, while the console caches it.
- **Max players.** A game that grows past the console's 32 slots is left,
  as a full one is.
- **No network co-op.** Co-op games are left out of the console's lists.
  Split screen and the campaign play as they always did.

## Building it yourself

Clone with the game:

```sh
git clone --recursive https://github.com/ChupathingyCE/warthog-xbox.git
cd warthog-xbox
```

(In a clone you already have: `git submodule update --init`.)

You need:
- Python 3, [ninja](https://ninja-build.org/) and LLVM (clang and lld-link).
  On macOS: `brew install llvm lld ninja`.
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
- Windows: no build path yet. Help is welcome in #warthog-dev on the Discord.
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

### Contributors

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
