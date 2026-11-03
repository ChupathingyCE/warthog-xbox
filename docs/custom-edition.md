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

`port/xbox/FEATURES.md` listed Custom Edition maps as cut for memory and
storage on 2026-10-19. This is the case for putting them back, on a console
of 128 MB for now.

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

Not tested: a real console (only xemu), a 64 MB console, the HUD's button
icons (`ui.map` has none this reads: a warning), joining a host of network
version 41 from this version 32 build.

## How it works

**The tag cache.** Custom Edition maps are linked to a tag cache of their own
at `0x40440000`, 23 MB. On a console above 64 MB, `src/xbox_memory.c` makes
it there with `VirtualAlloc` once the caches are made
(`platform_ce_tag_cache_ready`). A 64 MB console has no room for a second tag
cache, and its maps are left out of the menus' list.

For 64 MB, the way is known and was tried alone: the range at `0x40440000`
aliased onto the Xbox tag cache's own physical pages by writing the page
tables (the kernel maps them at `0xC0000000`), a map's tags at the bottom of
the range and its BSP at the top, on the same 22 MB, which a Custom Edition
map never uses at the same time as an Xbox map. A probe XBE did exactly that
in xemu at 64 MB: the two addresses read and wrote the same memory both ways,
disk reads went into the alias, and four real maps' own pointers (about 9,700
tags, their BSPs at the top of the range) all resolved, with no fixups. Not
in this branch: `cache_files.c` clears the whole 23 MB as a map loads, and
`ce_map_checks.c` takes a 23 MB image of it to check it, both of which a
64 MB console needs done another way first.

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
  at a time, with each one's screenshot (the store's DXT1, decoded into a
  texture of the screen's), author, date, size, the tag space it needs, and
  its description. A installs into `D:\maps_ce`, each map checked against the
  signed list's BLAKE2b as it comes; Y removes.
- **Xbox packs**: the store's Warthog packs, into `D:\maps_<pack>`, each map
  checked against Warthog's own fingerprint (`map_pack_fingerprint`), with
  `maplist.txt` merged.

The work is a thread of its own (`browser/xbox_maps.c`): plain HTTP to
`api.xboxhomebrew.store`, through XNet's Winsock (`browser/plat_warthog.c`),
so nothing holds up the menus. `browser/core.c`, `ce.c`, `http.c`, `json.c`,
`dxt.c` and `blake2b.c` are the store's own map browser's, shared with its
Linux command line. A name the Xbox's disk won't take is made one it will
(`=ac=timberland.map` becomes `actimberland.map`, at most 42 characters).

## Online Games

The console's list (`/v1/console/games`) leaves out Custom Edition games. On
a console that plays them, `src/xbox_game_list.c` asks the desktop list too
(`/v1/games.txt?fields=23&coop=1`) and adds its games whose map ends `@ce`
(`game_list_add_custom_edition`). They show as `<map> (CE)`, every game type
(CTF, Slayer, King of the Hill, Race...).

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

## Building

As `docs/building.md` says. On Linux, its "Linux" section: the SDK's names
made case-blind once (`tools/xdk_case_links.py`), and the link's own fix
(`tools/zero_urw.py`). The image is 5852 KB with all of this, from 5254 KB.

## What's left

- A 64 MB console (above: the alias, and the loader's two 23 MB needs).
- A real console.
- Downloading a missing map as a join asks for it (the desktop builds' map
  repositories), and HaloMD maps.
- DXT multipurpose maps' channels; the HUD's button icons on Custom Edition
  maps.
