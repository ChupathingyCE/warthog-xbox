# Custom Edition maps on the original Xbox

Custom Edition maps (Halo PC's, cache version 609) play on Warthog as they
are: no conversion, no Invader, the map file straight from `D:\maps_ce`, with
`bitmaps.map`, `sounds.map` and `loc.map` beside it. The game's own Custom
Edition loader does the work, the one the desktop builds use (`ce_*.c` in
`chupathingyce/port/linux/game`), built for the console with what the
console needs around it.

A Map Browser in the Multiplayer menu downloads them from the
[xboxhomebrew.store](https://xboxhomebrew.store) archive (halomaps.org's
maps, mirrored), and Online Games lists the Custom Edition games hosted on
the desktop builds and the dedicated servers, so the console can join them.

Custom Edition maps were cut from the Xbox build on 2026-10-19, for memory
and storage. They came back in the November test builds: on a console of
128 MB, and, experimentally, on a stock 64 MB one.

## Getting the maps onto the console

1. Make a `maps_ce` folder next to `default.xbe`.
2. Copy `bitmaps.map`, `sounds.map` and `loc.map` from your own Halo
   Custom Edition install into it.
3. Add maps with **Multiplayer > MAP BROWSER**, or copy `.map` files into
   `maps_ce` yourself.

They show in the multiplayer map list marked `[CE]`.

## What was tested

In xemu 0.8.139, 128 MB, with the retail kernel:

- Custom Edition maps listed in Select Map (`[CE]`), their spawn counts and
  vehicles read from the map.
- King Battle 1, ndugu, Pillars V2, Ctf 1 and Timberland loaded and played:
  levels, models, sky, HUD, radar and weapons as they should look, their Ogg
  Vorbis sounds decoded and started (`0 could not start`).
- Map changes in a row, back to the menus and into another map.
- The Map Browser: 2,246 entries listed, details and installed state read,
  maps installed (each checked against the archive's BLAKE2b), a download
  stopped, a map removed.
- Online Games: the five Custom Edition games on the list shown with the
  Xbox's; a join of one (ndugu, a dedicated server on network version 32)
  into its lobby and a game, over internet play.

In xemu at 64 MB (2026-11-03), experimental:

- ndugu and Ctf 1 loaded and played, the snow, lightmaps and HUD as at 128 MB.
- Online Games: Warman World (ndugu, a dedicated server now on network
  version 41) listed, its map missing, downloaded from the store as the
  join offered it, and joined into a game.
- The Map Browser's search.

On real consoles (the test builds from 2026-11-04): testers play them on
modified retail consoles, stock 64 MB ones included. Their reports fixed a
map whose name has a `#` in it (reported missing) and a map without Custom
Edition's resource maps (now refused with a message).

Not tested: the HUD's button icons
(`ui.map` has none this reads: a warning), large maps at 64 MB (their models
and BSP vertices now share the tag cache: some won't fit, and are refused
saying so).

## How it works

**The tag cache.** Custom Edition maps are linked to a tag cache of their own
at `0x40440000`, 23 MB. On a console above 64 MB, `src/xbox_memory.c` makes
it there with `VirtualAlloc` once the caches are made
(`platform_ce_tag_cache_ready`).

**A 64 MB console** has no room for a second tag cache, so the range is the
Xbox tag cache's own, which holds nothing while a Custom Edition map is
loaded (`src/xbox_ce_files.c`):

- **The page tables.** The range at `0x40440000` is mapped onto the Xbox tag
  cache's physical pages (the kernel's page tables at `0xC0000000`, its
  directory's own entry checked against CR3 before anything is written). Its
  bottom 22 MB are the cache's pages in order as a map loads; once the
  lowest BSP base is known, the top of the range is moved onto the top of
  the cache, and the megabyte between is left unmapped:
  `platform_ce_room_end` holds the loader's room (`ce_resources.c`) below it.
  A Custom Edition map's BSPs sit at the top of its 23 MB; every map read
  this way resolved with no fixups.
- **The checks, as it loads.** `ce_map_checks.c` checked a map as it opened
  in a 23 MB image of the tag cache. At 64 MB a map opens unchecked in the
  menus (whose `ui.map` is in the tag cache then) and is checked in the tag
  cache itself as it loads (`ce_map_check_deferred`), then read again.
- **Memory lent for the load.** The checks, the 7 MB model data block and
  the Ogg Vorbis decoding take more than a 64 MB console has free. For the
  load, their `malloc` (the game's `debug_malloc`, and the C runtime's in
  `ce_vorbis.c`, renamed per file in `build.py`) is the texture cache's
  memory, lent as the game lends it for a movie
  (`texture_cache_steal_memory`), and returned (the CPU's caches written out
  first) before anything of the map is drawn. ndugu's load took 6.9 MB of
  17 MB.
- **Vertices in the room.** The converted model and BSP vertices come from
  the loader's room below the BSPs, at the cache's own address, and the
  checks count them (`ce_bsp.c`, `ce_models.c`), so a map too large is
  refused with "too big for the game's tag cache".
- **No copies.** The bitmaps' layout (below) copied each one; with no memory
  for the copy, the Xbox's order is made in place: a bitmap's index bits
  swapped two at a time, a cube map's blocks rotated into place. The map
  hash reads 16 KB at a time (`map_archive.c`, the same hash), and the
  desktop game list comes into the list thread's stack.

**The converted vertices.** `ce_bsp.c` and `ce_models.c` ask for a block of
contiguous memory for each model part and BSP material (`XPhysicalAlloc`).
A console's free memory is in pieces (128 MB in xemu: 59 MB free, the largest
piece 1120 KB), so a level's failed and the game drew vertices it couldn't
read. While a Custom Edition map is loaded, the Xbox's own tag cache at
`0x803A6000` holds nothing, and it is one contiguous block the GPU already
reads vertices from: `src/xbox_ce_files.c` gives them out of it, first fit,
in any order they're freed (a level's outlives its map until the next
level's is made).

**The bitmaps.** The desktop builds' renderer is told a bitmap's pixels are
Halo PC's (`D3DCOMMON_PORT_PC_LAYOUT`, `..._PC_METER`, `..._PC_MULTIPURPOSE`)
and draws them so; the NV2A knows none of that. `game/xbox_ce.c` makes each
one the Xbox's in place as its pixels are read (`xbox_texture_cache.c`):
uncompressed texels in Morton order, a cube map's faces each with their
levels (Halo PC's: each level with its six faces, the second and third
swapped), a HUD meter's and a model multipurpose map's channels where the
Xbox's shaders read them. A DXT multipurpose map keeps Halo PC's channels:
its blocks aren't decoded.

**Reads.** A Custom Edition map's file is opened for overlapped reads, and
its header and checks read it without an `OVERLAPPED`, at the file's
position, which the desktop builds' layer allows. The kernel refuses that
(`NtReadFile` with no byte offset on a file without synchronous I/O):
`src/xbox_ce_files.c`'s `ReadFile` (`halo_xbox_prefix.h`) makes such a read
at the file's position and moves it on.

**Map identity.** A host of network version 29 or later names its Custom
Edition map's version by its whole-file hash (`map_archive_map_hash`):
`src/xbox_ce_files.c` reads the map once to say whether the console has the
host's, kept by path and size.

## The Map Browser

The Multiplayer menu's MAP BROWSER, a second copy of System Link's item under
ONLINE GAMES (`ui_widget.c`, `HALO_CONSOLE_MAP_BROWSER`), opens a screen of
code over the menus on Online Games' hooks (`game/xbox_maps_screen.c`). Two
lists, White or Black between them:

- **Custom Edition**: the archive's entries with a map on the mirror, a page
  at a time (X searches titles and authors, with the game's keyboard), with each one's screenshot (the store's DXT1, decoded into a
  texture of the screen's), author, date, size, the tag space it needs, and
  its description. A installs into `D:\maps_ce`, each map checked against the
  BLAKE2b the store's list gave (over HTTPS) as it comes; Y removes.
- **Xbox packs**: the store's Warthog packs, into `D:\maps_<pack>`, each map
  checked against Warthog's own fingerprint (`map_pack_fingerprint`), with
  `maplist.txt` merged.

The work is a thread of its own (`browser/xbox_maps.c`), through XNet's
Winsock (`browser/plat_warthog.c`), so nothing holds up the menus. The
store's API (`api.xboxhomebrew.store`: the lists, the hashes, the pictures)
is asked over HTTPS, by the game's console client
(`chupathingyce/port/console/https`, `console_https_get_stream`), which
checks the store's chain to Let's Encrypt's roots. The map files come over
plain HTTP, so a long download doesn't hold the client's one-request lock
from the game list and the reports, and each is checked against the hash
that came over HTTPS. The BLAKE2b is the game's Monocypher.
`browser/core.c`, `ce.c`, `http.c`, `json.c` and `dxt.c` are the store's
own map browser's, shared with its Linux command line. A name the Xbox's disk won't take is made one it will
(`=ac=timberland.map` becomes `actimberland.map`, at most 42 characters).

## Online Games

The console's list (`/v1/console/games`) leaves out Custom Edition games. On
a console that plays them, `src/xbox_game_list.c` asks the desktop list too
(`/v1/games.txt?fields=23&coop=1`) and adds its games whose map ends `@ce`
(`game_list_add_custom_edition`). They show as `<map> (CE)`, every game type
(CTF, Slayer, King of the Hill, Race...).

A game whose Custom Edition map isn't on the console offers it first (MAP
NEEDED): A downloads it from the store's map index by its file's name
(`/api/mapindex/maps?name=`, `ce_fetch_map`), checked against its BLAKE2b,
and joins the game once it's in `D:\maps_ce`.

## Bugs found on the way

- **lld fills code sections with 0xCC** (a Linux build, LLVM 22.1.8): the
  SDK's uninitialized data in `D3D_URW` and `DSOUND_URW` started as
  `0xCCCCCCCC`, and `DirectSoundCreate` crashed at boot on
  `CDirectSound::m_pDirectSound`. `tools/zero_urw.py` zeroes them after the
  link.
- **`dsound_globals` reserved 8 bytes short.** `halo_linker_common.c`
  reserves it 8 bytes larger with `HALO_CUSTOM_EDITION` (eight channel
  types); `tools/xbox_sources.py` took the `#else` branch's size, and
  `effect_data`, next to it, was overwritten with `1.0f` (a crash loading
  `ui`).
- **Sockets are kernel addresses.** The browser's HTTP took a negative socket
  for a failure; the console's are negative as an `int`.
- **A response's Content-Length was capped at the console list's buffer.**
  `game_list_parse_response` refused any larger than `GAME_LIST_RESPONSE_SIZE`
  (10 KB); it's now held to what came.

- **A failed hash was another version.** At 64 MB the map hash's 256 KB
  buffer couldn't be had, and a join said the console's map was another
  version than the host's: it's now read 16 KB at a time.
- **A bitmap with no memory to lay out was drawn in Halo PC's order** (HUD
  meters speckled, a level's light in blocks, snow dark). Now laid out in
  place.

## Building

As `docs/building.md` says. On Linux, its "Linux" section: the SDK's names
made case-blind once (`tools/xdk_case_links.py`), and the link's own fix
(`tools/zero_urw.py`). The image is 5852 KB with all of this, from 5254 KB.

## Not done yet

- More time on real consoles, 64 MB above all.
- A map the host changes to in the lobby that isn't on the console. The
  download is offered only as a game is joined.
- HaloMD maps.
- Custom Edition's resource maps from its installer. Today you copy them
  yourself.
- DXT multipurpose maps' channels, and the HUD's button icons on Custom
  Edition maps.
