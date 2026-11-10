# The PC features on the original Xbox

What the desktop builds (ChupathingyCE's main, merged into Warthog on
2026-10-21) have, and where each stands on the original Xbox. **Yes**: the
console has it. **Cut**: left out on purpose (the reason says why). **Not
yet**: wanted, with the plan. Every build should play the same where it
can; a difference here is a choice, written down.

The target is a stock 64 MB, 733 MHz console. Nothing below needs a bigger
one; a feature that would is to ask for it through the memory class
(`src/xbox_memory.c`).

## Online and cross-play

| Feature | PC | Xbox | Why, or the plan |
| --- | --- | --- | --- |
| Online Games (the game list) and joining | Yes | Yes | The console's own screen (`game/xbox_browser_screen.c`), over HTTPS (plain HTTP if HTTPS fails) |
| Hosting a listed internet game | Yes | Not yet | The console hosts System Link; its games are not listed and PCs do not join it over the internet yet |
| Invite links | Yes | Yes, from `console.join` | No clipboard on a console |
| Cross-play with PCs and OpenCE | Yes | Yes | Up to 32 players in test builds (16 in a build without `--console-players 32`), network versions as `README.md` says |
| Delta Peer (platform, player ID) | Yes | Yes, as a client | `console.delta`; the player ID from `D:\game_list_player.key` |
| Text chat | Yes | Yes | Shown lines, quick lines on the D-pad, a USB keyboard's T, Y and H; the client's part only (a console never hosts on Delta) |
| Server messages | Yes, in a banner | Yes, in the chat lines | The banner is the desktop's overlay; the console shows them as lines |
| Team changes in a game | Yes | Not yet | Needs CHANGE TEAMS in the console's pause menu (`team_change.c` is 505 lines, little memory) |
| Delta Profile screen | Yes | Yes, Link Profile | Online Games' Start: link the console to a profile with a code (`D:\delta_device.key`), then its name and clan tag show there |
| Profile names and clan tags in games | Yes | Not yet | The names come from the site, for each player in the game |
| Platform logos on the scoreboard | Yes | Not yet | The logos are drawn with the high-res text (cut) |
| Stats reports of joined games | Yes | Yes | The desktop's report over HTTPS, with the identity file's key or signed by a linked console (`src/xbox_reports.c`); `network.report_joined_games`; about 14 KB of recorder state and a 128 KB thread for a few seconds after each game |
| Delta Stats of hosted games | Yes | Not yet | Only for a listed host |
| Moderation (moderator sign-in, kicks) | Yes | Not yet | Desktop and dedicated server only |
| Network co-op | Yes | Not yet | Not built for the console (`network_coop.c`, 3,100 lines); co-op games are left out of its lists |
| Custom Edition maps | Yes | Yes (64 MB experimental) | Played as they are, with no conversion: the shared code's loader (`ce_*.c`), its tag cache at `0x40440000`, the converted vertices in the Xbox's idle tag cache, the bitmaps made the Xbox's as they're read (`docs/custom-edition.md`). A 64 MB console: the Xbox's tag cache by the page tables, the load's memory lent by the texture cache, experimental |
| Map downloads on join | Yes | Yes, Custom Edition | Online Games offers a missing Custom Edition map (MAP NEEDED) from the store's map index, then joins. HaloMD maps: not yet |
| Map Browser (Multiplayer menu) | No | Yes | The xboxhomebrew.store archive's Custom Edition maps and the store's Xbox map packs, with screenshots, details, install and remove (`port/xbox/browser`, `game/xbox_maps_screen.c`) |
| Xbox map packs (Halo CE+ in `maps_cex`) | Yes | Yes | Xbox-format maps, read by the console's own loader; the menus' map list (`ui_map_list.c`, `map_packs.c`) is on the console for them, about 35 KB |
| Pings in the game list | Yes | Not yet | The console's list shows none; the probe code (`p2p_probe.c`) is already linked |

## Menus and controls

| Feature | PC | Xbox | Why, or the plan |
| --- | --- | --- | --- |
| Chupathingy menus (Xbox menus with extensions) | Yes | Not yet | Built from XML and PNG files at load (`menu_files.c`, `menu_tags.c`, `menu_functions.c`: about 12,000 lines and their art). The console keeps the Xbox's own menus with ONLINE GAMES added. Plan: measure what the built menus take on a 64 MB console before porting |
| Settings submenus (Display, Online, Interface, Chat, Controls) | Yes | Not yet | Part of the Chupathingy menus; the console's settings are `D:\config.toml` |
| Gearbox menus, Server Browser in them | Yes | Cut | PC-only look |
| Button layouts: the Xbox's five | Yes | Yes | The profile's Controller Setup |
| Bumper Jumper | Yes | Yes | The sixth in Controller Setup's Button Layout (no picture for it) |
| Custom button layout, Controls screen | Yes | Not yet | Needs the Controls screen; plan: its controller page as a console screen, opened from Controller Setup |
| CONTROLS in the pause menu | Yes | Not yet | With the Controls screen |
| Keyboard and mouse controls | Yes | Cut | Not a console's; a USB keyboard only chats |
| Game Setup and the game type editor for hosted games | Yes | Not yet | Part of the Chupathingy menus; the console has the Xbox's game type editor |
| Saved game types as files | Yes | Not yet | With Game Setup |
| Start with one player | Yes | Yes | `game.start_with_one_player` |
| Controller prompts for other pads | Yes | Cut | The console has the Xbox's pad |
| Touch controls | Android | Cut | |

## Display and HUD

| Feature | PC | Xbox | Why, or the plan |
| --- | --- | --- | --- |
| Wide screen (16:9) | Yes, the window's shape | Yes, the dashboard's | `display.widescreen`: the 640x480 picture stretched by a 16:9 TV, with the desktop's wide view, HUD and menus; no memory (README "Video modes"). Needs a check on hardware |
| Field of view, weapon field of view | Yes | Yes | `display.field_of_view`, `display.viewmodel_field_of_view`; wider draws more |
| Follow camera while waiting to respawn | Yes | Yes | `display.follow_camera` |
| Power-up timers | Yes | Yes | `display.powerup_timers`, off by default |
| Match timer | Yes | Yes | `display.match_timer`, off by default |
| Scoreboard: team columns, panel, quit players | Yes | Yes | `display.scoreboard_*`, `display.show_quit_players` |
| Version watermark | Yes | Yes | `display.version_watermark` |
| High-res HUD and text | Yes | Cut | 2026-10-19: the atlas's texture memory |
| Names above players' heads | Yes | Cut | Retail names the player under the reticle only |
| High frame rates, interpolation, smooth object functions | Yes | 60, the view only | `display.frame_rate = 60` (off by default): the view turns every frame and eases into each tick, objects stay as the tick left them; no memory (README "60 frames a second"). Object blending and smooth functions are cut (about 0.5 MB and 384 KB) |
| Flicker filter | No (no TV) | Yes | `display.flicker_filter`, 0 to 5, the retail 5 by default |
| Anti-aliasing, anisotropic filtering, shadows, per-pixel lighting, full model detail, far clip | Yes | Cut | The desktop renderer's; a cost on the console's GPU |
| The desktop's loading screen | Yes | Cut | The Xbox's own |

## Other

| Feature | PC | Xbox | Why, or the plan |
| --- | --- | --- | --- |
| Telnet console, network test bots | Yes | Cut | 2026-10-19 |
| Tag validator | Yes | Cut | 170 KB; the console plays its own retail maps |
| Updater, Discord | Yes | Cut | A console's builds are installed by hand |
| Music and effects volume | Yes | Yes | `audio.music_volume`, `audio.effects_volume` |
| Unlock all levels | Yes | Yes | `game.unlock_all_levels` |

## Memory

What the features above added to the console's image when they came in
(`build/xbox/halo.map`, against the merge of main alone): text chat 13 KB of
code and 28 KB of data (the shown lines, 16 in the log, and the built-in
blocked words list; the desktop's own word file and the host's filter are
left out), the follow camera and power-up timers about 4 KB. All of it is
static: nothing is allocated at run time. `HALO_CONSOLE_NO_CHAT` leaves chat
out of a build.
