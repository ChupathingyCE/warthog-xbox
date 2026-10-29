# Original Xbox

`ninja xbox` builds the game for the console that it was written for:
`build/xbox/default.xbe`. The build uses clang and lld-link as
[OXDK](https://github.com/MrMilenko/OXDK) does, on macOS or Linux. It is the
game only: the platform layer of the other ports (`port/linux`) is not in it,
because the game's Xbox code talks to the Xbox SDK directly.

The game is the `chupathingyce` submodule. Paths on this page under
`source/`, `port/linux/`, `port/include/` and `port/third_party/` are the
game's, in `chupathingyce/`; `port/xbox/` and `tools/` are this
repository's.

`python3 tools/easy_build.py` walks through everything below: it checks the
tools, asks for the files you supply, builds and packages
(`--check` only checks; `docs/building.md` has the options).

## Requirements

- Python, [ninja](https://ninja-build.org/) and LLVM (clang and lld-link).
  On macOS: `brew install llvm ninja`. The build looks for LLVM in `LLVM_DIR`
  and then in Homebrew's folder.
- [OXDK](https://github.com/MrMilenko/OXDK) in `~/OXDK` (or give
  `--oxdk DIR`, or set `OXDK_DIR`), with its `cxbe` built.
- The Xbox SDK's libraries and headers in OXDK's `xbox/xdk/lib` and
  `xbox/xdk/include`, as the OXDK instructions tell. This repository does not
  supply them. The SDK comes as a Windows installer that installs into
  Microsoft Visual Studio .NET; you must install it (and a Visual Studio that
  it accepts) on a Windows computer or virtual machine, and then copy the
  files. The build was made with the 5933 SDK.
- An Aug 2001 build of the Direct3D library, `d3d8ntpr.lib`. This repository
  does not supply it. Refer to "Direct3D".
- The North American (NTSC) disc's maps. Refer to "Game data".

## Build

```sh
git submodule update --init
python3 chupathingyce/configure.py --console-port port/xbox \
    --xbox-d3d8 /path/to/d3d8ntpr.lib --console-no-debug-asserts --console-players 32
ninja xbox
```

The game's `configure.py` loads this port's `build.py` (`--console-port`)
and records the options, so later runs of ninja keep them. If
OXDK or the Direct3D library is not found, there is no `xbox` target.
The console-wide options: `--release` (assertions are not checked),
`--console-no-debug-asserts` (the release-style build testers get: the
debug decomp's asserts compiled out but for the memory checks),
`--console-players {16,24,32,64}` (the game's default is 16; test builds
are 32) and `--console-debug-tools` (see "Memory");
`--oxdk DIR` names OXDK's checkout. `docs/building.md` lists them all.

## Game data

The game is build 01.01.14.2342, but it plays the maps of the North American
(NTSC) release, 01.10.12.2276, as the other ports and the dedicated servers
do: playing online with them needs the same maps. The maps of the PAL
release (01.01.14.2342) work too, played as the NTSC maps are
(`port/linux/game/pal_tags.c`). Use `maps/*.map` and `maps/loading.tga`
from the disc.

`python3 tools/xbox_package.py --maps DIR --out DIR` lays the build out.
Put the files on the console's hard disk in one folder, for example:

```
E:\Games\Warthog\default.xbe
E:\Games\Warthog\maps\ui.map
E:\Games\Warthog\maps\loading.tga
E:\Games\Warthog\maps\a10.map ...
```

`D:\` is the folder of the XBE when the game runs, and the game writes its
log to `D:\debug.txt`. Saved games and profiles go to the utility drive
(`Z:\`), as on the disc. A normal build is the debug decomp's: an assertion
stops it and shows the error on the screen and in `debug.txt`. A `--release`
build notes it in `debug.txt` and goes on.

## Direct3D

The game compiles against the January 2002 SDK's declarations
(`port/include/xdk`), as the other ports do, and links with a later SDK's
libraries. Direct3D is the exception. Later SDKs renumbered the render
states and texture stage states that the game sets, so the game needs a
Direct3D of its own time. The reconstructed `libs/d3d8` needs the January
SDK's headers, which are not available. The build therefore links
`d3d8ntpr.lib`, the Aug 2001 Direct3D that the kernel's boot animation
used. Its state numbers are the January SDK's, and its device has the
layout that `source/main/d3d_intimacy.cpp` reads. `port/xbox/src/
xbox_support.cpp` supplies the memory functions that the kernel gave it.

## Differences from the SDK

`port/xbox/include/halo_xbox_prefix.h` is included before each game unit.
The files in `port/xbox/src` supply what the later SDK does not:

- `xbox_dsound.c`: the January SDK gave a voice's mix bins as a mask. Later
  SDKs take a list. The file translates the game's calls. The game's reverb
  image is for the January DirectSound, so it is not loaded: there is no
  reverb.
- `xbox_d3dx.c`: the four D3DX functions that the game calls. The SDK's
  D3DX library needs the SDK's own Direct3D.
- `xbox_support.cpp`: `fast_ftol_C` (not yet in the reconstruction), and
  the debug monitor's module functions. A title that imports `xbdm.dll`
  does not start on a retail console, so the game finds no modules.
- `xbox_winsock_hooks.c`: the game's 12-byte `XNADDR` (the 2002 SDK's, and
  the wire's) to and from the 5933 SDK's 36-byte one, and
  `XNetStartupParams` at the 5933 library's 12 bytes (`port/include/xdk`).
  The other SDK structures the game hands the libraries have the same size
  in both; `XNetGetConfigStatus`'s is in no header, so the game's copy has
  room to spare.

`tools/xbox_sources.py` makes the pooled globals and the Bink stub from the
Linux port's files. `port/linux/game/msvc_comdat.c` supplies the header
inline functions that units call through a prototype.

## The game list

At the start, once the network is up, a thread of the game's own gets the
game list from `http://warthog.milenko.org/v1/console/games` (plain HTTP:
the console has no TLS) and the game logs it, to the debug monitor as
`halo: game list:` lines and to `debug.txt` as `game list:` lines. Nothing
waits on it. Multiplayer's ONLINE GAMES shows it (`game/xbox_browser_screen.c`).

- `src/xbox_game_list_fetch.c`: HTTP/1.0 over XNet's Winsock. The host is
  looked up with `XNetDnsLookup`; if it does not resolve,
  `console.game_list_server` names another: a host or an IPv4 address, `:port` optional
  (the request still says `Host: warthog.milenko.org` for an address).
  The response is read to the server's close or its Content-Length, at
  most 64 KB, each wait bounded (15 seconds in all); three tries.
- `src/xbox_game_list_parse.c`: the response (status line, headers, body)
  and the list, with a bound on every read: lines of 1,024 bytes (headers)
  and 255 (the list), 64 headers, 64 games, each field its size; anything
  cut short is an error. The format is the site's list for consoles (format 1).
- `src/xbox_game_list.c`: the thread and the logging. No address is logged.
- `tests/run.sh`: the parsers on the host under AddressSanitizer and
  UndefinedBehaviorSanitizer (known inputs, every truncation, a mutation
  fuzzer), `--fuzz SECONDS` with libFuzzer, `--fetch` the real list over
  the host's sockets.

XNet speaks only to other consoles unless it starts insecure, so the list,
like play with the PCs, needs `console.bypass_security = true`.

## config.toml

The console's settings are in `D:\config.toml`, beside `default.xbe`: the
desktop builds' file (`port/linux/src/port_config.c`, its TOML parser and
its table of settings), with the settings marked for the console
(`_platform_xbox`) and no environment variables. On the first start
without it the game writes it with every console setting commented out at
its default, each with its help; a setting is chosen by taking out its
`# ` and changing the value. The file is read when the game starts.
`src/xbox_config.c` is the console's side (`xbox_config.h`), and
`tools/xbox_package.py` writes the file with `console.bypass_security`
(and `console.game_list_server` with `--game-list-server`).

The console's own, in `[console]`:

| Setting | Default | What it does |
|---|---|---|
| `bypass_security` | `false` | XNet starts insecure: the internet, the game list and play with PCs need it (`tools/xbox_package.py` writes `true` unless `--secure`); internet play (`network.online`) is off without it |
| `game_list_server` | `""` | the list's server (host or address, `:port` optional) when `warthog.milenko.org` does not resolve |
| `join` | `""` | a host's LAN address (System Link joins it directly) or an invite (ONLINE GAMES' Y joins it) |
| `trace` | `false` | the bring-up traces: the main loop each second, every hop of an internet join, the advertisements seen, other hosts only as `enet#` tags (salted per run), never their Ethernet addresses; and XLink Kai's address for the console |
| `memory_class` | `"auto"` | the memory class (see "Memory"): `"auto"` is the console's own, so a console of 128 MB or more has the extras; `"64"` holds any console to the 64 MB baseline's sizes |
| `large_caches` | `false` | deprecated and ignored (`debug.txt` says so): a console of 128 MB or more has the larger caches on its own (`memory_class`) |
| `debug_geometry` | `false` | the debug build's debug geometry (lines and triangles drawn by debug commands): its three 480 KB buffers are made at the start only with it on. For development, on a console with room (a devkit) |
| `simulate_64mb` | `false` | deprecated and ignored (`debug.txt` says so): a devkit gets 64 MB from the XBE's flag now, `tools/xbox_package.py --devkit-64mb` (see "Memory") |
| `delta` | `"auto"` | Delta Peer as a client: joining a game whose host offers it (the desktop builds and the dedicated servers), the console says it is an Xbox, with its memory class, and nothing else, so that the stats site, the scoreboard and the server show Xbox. `"auto"` and `"on"` send it; `"off"` keeps the legacy protocol alone. A host without Delta, or any failure, leaves the game as it was. About 50 KB of the console's memory (all static: its code and 11 KB of data) |
| `perf_log` | `false` | the frame timing log: once a second a `perf:` line in `debug.txt` with the frame time (average and worst), each part of the frame (game ticks, the render's parts, sound, network, present, the wait for the screen), the draws and objects, and a line when the player zooms. For finding what makes a scene slow |
| `log_flush` | `"buffered"` | when `debug.txt` is written: `"buffered"` keeps the game's lines in memory and writes them once a second, when 8 KB wait, and at once for an assert, an exit or a crash; `"every_line"` writes each line as it comes (slow), for a crash hunt |


**Link Profile (optional).** Online Games' Start opens DELTA PROFILE. A asks for a code: the screen shows it (XXXX-XXXX) and its QR code; enter it at stats.chupathingy.org/connect, signed in on a phone or computer, then press A on the console to confirm. The console makes its own key for this the first time (`D:\delta_device.key`, next to `config.toml`; it never leaves the console, and nothing secret is sent over the plain HTTP the console uses: the game's `port/console/link`, docs/delta.md "Link Profile on a console"). Once linked it keeps the handle in `D:\delta_link.txt`, shows the profile's name and clan tag, and Delta Peer's PROFILE carries its ID, so its games count toward the profile. Unlink it on the site's profile page. Keep `delta_device.key` to yourself: a copy of it plays as your console. An identity file (below) still works and wins when present.

**Player identity (optional).** A copy is anonymous unless its player links it (above) or puts their identity file at `D:\game_list_player.key` (by FTP, next to `config.toml`): the file the site's profile page exports, the same 32-byte player key a desktop copy keeps in `game_list_player.key`, either as the raw bytes or as 64 hex digits (use the hex form if you transfer in ASCII mode). The console never makes a key and never sends it anywhere: it works out the public player ID (the first 16 bytes of the SHA-256 of `halo-ce-universal player id\n` and the key) and wipes the key. On a console the file being there is the consent (`network.share_profile` is a desktop setting): Delta Peer's HELLO adds the `profile` capability and PROFILE carries the ID. Once per boot, after the first online game has ended, the console also makes a signed platform claim over plain HTTP to the site (`GET /v1/claim_challenge`, then `POST /v1/claim_signed`, `xbox_claim.c`): it signs the site's one-time challenge with the Ed25519 key derived from the player key (the desktop builds' moderator key), so the key itself is never sent; a failure is one line in `debug.txt`, with no retry. Neither the key, the signature nor the ID is written to `debug.txt`. Without the file or a link nothing is shown and nothing is sent. The file is the player's identity: it must not be shared.

In a build with the developer tools (`--console-debug-tools`),
`debug.audio_probe` (in `[debug]`) runs the audio probe
(`port/linux/game/audio_probe.c`; a build without them leaves the probe and
its two settings out): a probe file's path, such as
`'D:\audio_probe\retail_maps.txt'` (`tools/audio_probes`). The game plays
the file's sounds at its spots and records what the console's audio hardware
plays, its final mix read back from the AC'97 controller's output
(`src/xbox_audio_capture.c`), as WAVE files beside the probe file, for
`tools/audio_probe_compare.py`; then it goes back to the main menu. Empty
(the default): none of it runs.
`debug.audio_capture_seconds` records real play instead: that many seconds of
each game (solo, system link or online) once the player has a unit, as
`D:\audio_probe\capture-<map>-<n>-xbox-final.wav` with `-events.txt` (the
player's position each second, and every sound a channel starts, with its
type, gain, obstruction and occlusion). The folder must be there. 0 (the
default): none.

And the desktop builds' that apply on the console: `display.direct_camera`,
`display.field_of_view`, `display.viewmodel_field_of_view`,
`display.match_timer`, `display.version_watermark`, `display.follow_camera`,
`display.follow_camera_after`, `display.powerup_timers`,
`display.scoreboard_team_layout`, `display.scoreboard_background`,
`display.scoreboard_background_color`, `display.show_quit_players`,
`display.server_messages`, `chat.enabled`, `chat.show`, `chat.filter`,
`chat.streamer_mode`, `chat.quick_messages`, `audio.music_volume`,
`audio.effects_volume`, `game.console_log`, `game.unlock_all_levels`,
`game.start_with_one_player`, `network.online`, `network.tunnel_port`,
`network.brokers_file`, and `network.stun_servers`. A wider field of view
draws more of the map each frame: on a stock console, keep the Xbox's 70 if
a busy map slows down. `FEATURES.md` lists the desktop's features and where
each stands on the console.
Another setting the shared code reads is empty, 0 or false on the console,
as before it had the file.

The switch files that came before it are still read for this release, each
logged as deprecated in `debug.txt` ("D:\memory_64.txt is deprecated, use
console.simulate_64mb in config.toml", which now ignores it): `bypass_security.txt`,
`memory_64.txt`, `large_caches.txt` and `trace.txt` (an empty file turns
the setting on), and `join.txt` and `game_list.txt` (one line, used when
the setting is empty).

## Memory

`src/xbox_memory.c` reads the console's memory at the start
(`GlobalMemoryStatus`, else the kernel's `MmQueryStatistics`) and logs it
with its class: 64 MB (a stock Xbox), 128 MB (a devkit, or the common
upgrade) or 256 MB (the bigger mods). The XBE does not limit itself to 64 MB
(`cxbe -LIMIT64MB:no`), so a console whose kernel knows the upper 64 MB
reports it; `tools/xbox_package.py --devkit-64mb` sets the flag that
limits a devkit again.

64 MB is the baseline: every feature must run on a stock console. A
console of 128 MB or more gets extras, decided once at the start, before
anything is allocated:

| | 64 MB class | 128 MB class (256 MB) |
|---|---|---|
| texture cache | 22 MB, the Xbox's (down to 19 MB if the console is short) | 44 MB (64 MB) |
| sound cache | 4 MB, 512 sounds | 8 MB, 1024 sounds |
| dynamic triangles a frame | 32768 (192 KB index buffer) | 65536 (384 KB) |
| transparent geometry groups a frame | 384 | 1024 (100 KB more heap) |
| network connection queues | 64 KB reliable, 32 KB unreliable, 16 KB a joined machine (16 players) | four times (the desktop builds': 256 KB, 128 KB), 32 KB a machine |

That is about 30 MB of a 128 MB console's upper 64 MB; the rest stays free.
The 64 MB class's sizes are the ones it had before the classes (the image
grows by 40 bytes). Only what is drawn, heard and buffered on the console
changes: the game's limits (players, machines, objects, particles, the game
state) and what goes on the wire stay the Xbox's on every console, so all
of them, and the PCs, play the same game; docs/cross-play.md has why the
bigger game state needs more than a bigger console. The high-res text is
not among the extras: the Xbox build leaves it out (it draws the maps' own
fonts, as retail did).

`debug.txt` says which class the game uses, once, at the start:
`boot: memory: 128 MB console, extras on: texture cache 45056 KB, sound
cache 8192 KB, and larger render buffers and network queues`, or
`boot: memory: 64 MB console, extras off: the Xbox's sizes`, or, on a
bigger console held to the baseline, `boot: memory: 128 MB console held to
64 MB by console.memory_class, extras off` (or `by the XBE's 64 MB flag`).

`console.memory_class` in `config.toml` chooses: `"auto"` (the default) is
the console's own class; `"64"` holds any console to the 64 MB class, for a
test of the baseline on a bigger console or if an extra misbehaves.
`console.large_caches`, which raised the texture cache on request, is
deprecated and ignored: the class does it.

For code: `console_memory_extras()` (port/linux/include/halo_console_memory.h,
console-neutral, answered here by `src/xbox_memory.c`) says whether this
console has the extras; `HALO_CONSOLE_MEMORY_EXTRAS(baseline, extras)` picks
a size by it (the baseline on every build that has no class). A heavy
feature that only a bigger console can carry checks it at its start; one
that should tell the player calls `xbox_memory_require`, a message at the
main menu, and the game goes on without it. Another console port answers
the same two calls with its own classes (an Xbox 360 has 512 MB) and
defines `HALO_CONSOLE_MEMORY_CLASS`. `xbox_memory_delta_class` is Delta
Peer's platform key's `memory_class` (log2 of the megabytes: 6, 7 or 8).

The network tables hold 16 players (`HALO_CONSOLE_PLAYERS`, as the Xbox's;
`configure.py --console-players`, console-neutral like `--console-debug-tools`): the
players, the machines (one more, for a dedicated host) and the internet tunnel's peers. A host's game with
more machines or players is left as a full game, never overflowed. A test
build with 32 or 64 (`configure.py --console-players 32`) is more than a stock
console's memory holds, and says so on screen when the main menu comes up.

A stock 64 MB console has about what the Xbox game had: its caches (game
state, tags, textures, sound: 51 MB) are the Xbox's, so the image and the
heap must stay near the Xbox game's. The debug build the port comes from
kept debug records the console does not (the AI's per-actor debug records
and path copies, 10 MB of heap; the profiler's 256-frame history; the
oversized surface bit vector); the map checks are left out of the build
(`HALO_CONSOLE_NO_TAG_VALIDATE`: the console loads maps unchecked, as the
Xbox game did). If what is free at the start still
cannot hold the Xbox's 22 MB texture cache, what Direct3D and the heap take
before the main loop (up to 7,741 KB measured on the devkit, 7,744 KB
assumed) and a reserve of 1,536 KB, the cache is made smaller, down to 19 MB (the attract movie
borrows 16 MB of it), rather than the start failing. Below 22 MB it grows a
megabyte at a time only while the reserve is still expected free at the
main loop (`_main_loop_reserve`, xbox_memory.c), so memory the build saves
stays free for the menus and joins instead of going to textures; the
`boot: memory: texture cache` line gives the expected free memory with the
cache made and with a step more.

Every step of the start is in `debug.txt` before the game's own log opens
(`boot:` lines, the file closed after each): the memory and the class, the
image's size, each cache as it is made, the first Direct3D device, each heap
allocation of 256 KB or more and any that fails, and the free memory at the
main loop; later, `memory: N KB free (a new low)` as a map loads or a match
peaks.

The target is a stock 64 MB console; 128 MB (a devkit, the common upgrade)
only makes it smoother. To test the 64 MB path on a 128 MB console, set
`console.memory_class = "64"` (any console: the sizes are the baseline's,
though the memory free is still a 128 MB console's), or, on a devkit, package
with `tools/xbox_package.py --devkit-64mb`: it sets `XINIT_LIMIT_DEVKIT_MEMORY`
in the XBE's init flags (`0x4`, in the header at `0x124`; `cxbe`'s `Xbe.h`
has them), and the devkit's kernel gives the game 64 MB, as a stock console
has. A stock console ignores the flag. (`console.simulate_64mb`, which tried
to hold the memory above 64 MB from inside the game, cannot take a devkit's
upper 64 MB and starved the debug kernel; it is replaced by the flag.)

The build leaves out every function nothing calls (each is compiled in a
section of its own, `-ffunction-sections`, and linked with `/opt:ref`):
about 1,400 of the debug decomp's, 240 KB. The platform code that runs only
at the start or at a join (the settings, the tunnel's keys and signatures,
the game list, the claim) is built for size (`-Os`), another 120 KB. The
game's own code keeps `-O2` and its EBP frames, which the crash reports
follow, but for the functions that call no other.

The main thread's stack is the XBE's 1 MB, and the console commits all of
it. `debug.txt` says how much of it the game has used: a `stack:` line at
the start and one each time the deepest grows by 16 KB
(`src/xbox_stack.c`). Some of the debug decomp's AI functions keep 80 to
160 KB of arrays on the stack, so the size is cut only once a campaign
mission's and a busy match's lines are known.

The debug decomp's developer tools are not in the console's build unless
it is configured with `--console-debug-tools` (`HALO_CONSOLE_DEBUG_TOOLS`): the
profiler, the AI debug displays, the render and collision debug, the stack
walker, the texture cache listing, libtiff and the bitmap tools. Their code
is kept, not removed, and what the game calls in them is a stub in the
console's build; a console command or script that reaches one prints
"not in this build". About 0.9 MB of the image. The collision log, which
counted and timed every collision test for one of those displays, counts
nothing without them.

## XLink Kai

At the start `debug.txt` says whether the console's address is XLink
Kai's for its MAC: yes or no. Kai's address itself (`10.252.EE.FF`, mask
`255.255.0.0`, from the MAC's last two bytes) is logged only with
`console.trace`. The "XLink mode" that would set it is a design
(docs/xlink.md).

## Status

The build boots to the menus at a steady 30 fps on a development kit and on
modified retail consoles, gets its address by DHCP, shows the game list
(ONLINE GAMES) and joins [D] dedicated servers and PC hosts through the PCs'
tunnel. It targets a stock 64 MB console (first matches played on October 18);
128 MB only adds extras (see "Memory"). Music and the campaign's effects
play. The code is ChupathingyCE's at network version 28 (it joins hosts of 11
through 30).
Network co-op is not built for the console (`game/xbox_coop.c`): co-op
games are left out of its lists, and a game that turns co-op is left with
a message. Known:

- There is no Bink video (the movies are skipped) and no reverb.
- The netcode is the PC builds' (distributed). The console joins a PC
  host's game as a client: it folds the PCs' 128-slot game settings record
  and Slayer's state into its 16 slots, and leaves a game that grows past
  them as a full one (docs/cross-play.md, "What is built"). It joins an
  internet game by its invite from ONLINE GAMES (A on a listed game, Y for
  the invite in `console.join`), through the desktop builds' tunnel
  (`port/linux/src/p2p*.c`, built as they are; `src/xbox_p2p.c` and
  `src/xbox_winsock_hooks.c` are its platform), with
  `console.bypass_security`. On a LAN it finds a PC host in System Link, or joins the host
  whose IPv4 address is in `console.join` (`src/xbox_direct_join.c`). It
  does not yet host the PCs, and its object array (2,048) is the Xbox's:
  `debug.txt` logs how high a PC host's object indices go ("cross-play:"
  lines).
- The menus are the Xbox's; the PC menus are not built.
- Campaign and multiplayer levels are still being tested.
