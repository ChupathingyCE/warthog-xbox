# Settings on the original Xbox

The game reads its settings from `config.toml`. It sits next to
`default.xbe`, for example `E:\Games\Warthog\config.toml`. The game calls
that folder `D:\`.

If there is no `config.toml`, the first start writes one. Every setting is
in it, commented out at its default, with a short help line.

To change a setting:

1. Copy `config.toml` to your computer, or open it over FTP.
2. Find the setting and remove the `# ` in front of it.
3. Change the value and save the file.
4. Restart the game.

The game reads the file only when it starts.

```toml
[console]
bypass_security = true

[display]
frame_rate = 60
```

Older test builds used empty switch files beside `default.xbe`, such as
`bypass_security.txt`, `trace.txt` or `join.txt`. They still work for now.
`debug.txt` says which setting to use instead. Use `config.toml`.

## The console's own settings (`[console]`)

| Setting | Default | What it does |
| --- | --- | --- |
| `bypass_security` | `false` | `true` starts the network in its open mode. Online Games, internet play and playing with PCs need it. `tools/xbox_package.py` writes `true`. `false` keeps System Link with other Xboxes only. |
| `game_list_server` | `""` | Where to get the game list if the console's DNS can't find it. A host name or an address, with `:port` if needed. |
| `join` | `""` | For testing joins. A host's LAN address joins it from System Link. An invite (`halo://join/...`) joins it with **Y** in Online Games. |
| `trace` | `false` | `true` adds the network's steps to `debug.txt`. Use it when a join fails. Other machines show only as tags, never by their addresses. |
| `delta` | `"auto"` | Tells a Chupathingy host that this is an Xbox, and its memory size. Nothing else is sent. The site and the scoreboard then show Xbox. `"off"` turns it off. The game plays the same either way. |
| `memory_class` | `"auto"` | `"auto"` gives a 128 MB console larger texture and sound caches. A stock 64 MB console keeps the Xbox's sizes. `"64"` gives any console the stock sizes. Use it to test the 64 MB setup, or if an extra misbehaves. |
| `log_flush` | `"buffered"` | When `debug.txt` is written. `"buffered"` writes once a second, and at once for a crash. `"every_line"` writes each line as it comes. It is slow, but it keeps the very last line before a freeze. |
| `perf_log` | `false` | `true` writes a `perf:` line to `debug.txt` once a second. It has the frame time and the time each part of the frame took. Use it to find what makes a scene slow. |
| `perf_gpu_sync` | `false` | With `perf_log`: wait for the graphics chip at each timed part. The times are then the chip's own. It slows the game down. |
| `widget_pool_log` | `false` | For sizing the menus' memory. Writes a line to `debug.txt` each time the menus use more of it. |
| `debug_geometry` | `false` | For development. Brings back the debug build's lines and triangles. They take 1.4 MB, so use it on a console with room. |
| `large_caches` | `false` | No longer used. `memory_class` decides now. |
| `simulate_64mb` | `false` | No longer used. To give a devkit 64 MB, package with `tools/xbox_package.py --devkit-64mb`. |

## Picture

| Setting | Default | What it does |
| --- | --- | --- |
| `display.widescreen` | `"dashboard"` | Follows the dashboard's Widescreen setting. With it on, the game shows more to the sides and fits the HUD and menus to a 16:9 TV. `"off"` keeps the 4:3 picture. |
| `display.frame_rate` | `30` | `30` draws as the original game did. `60` turns the view every frame. The game still runs 30 ticks a second, so play and the network are the same. A busy scene drops to a steady 30 until it can hold 60 again. |
| `display.flicker_filter` | `5` | How much a 480i picture is softened against flicker, from 0 to 5. The original game used 5. Lower is sharper and flickers more. It does nothing at 480p. |
| `display.field_of_view` | `70.0` | The field of view in degrees, from 50 to 120. Wider draws more of the map. On a stock console, keep 70 if a busy map slows down. |
| `display.viewmodel_field_of_view` | `0.0` | The weapon's field of view, from 50 to 120. `0` uses `display.field_of_view`. |
| `display.reticle_scales_with_fov` | `true` | Sizes the reticle to the field of view. It makes no difference at 70. |
| `display.direct_camera` | `true` | Turns the view in the same frame the stick moves. |
| `display.dynamic_light_limit` | `8` | The most dynamic lights, such as muzzle flashes and explosions, that light the level at once. `0` is no limit. |
| `display.specular_light_limit` | `2` | Of those lights, the most that also add their shine. `0` is no limit. |

## HUD and scoreboard

| Setting | Default | What it does |
| --- | --- | --- |
| `display.match_timer` | `"off"` | A clock in multiplayer games: `"off"`, `"remaining"` or `"elapsed"`. |
| `display.match_timer_position` | `"top"` | Where the clock goes: `"top"` or `"bottom_right"`. |
| `display.powerup_timers` | `false` | Shows the seconds left on camouflage, overshield and full spectrum vision. |
| `display.medals` | `false` | Shows your medals as HUD messages as you earn them. They count on your service record either way. |
| `display.follow_camera` | `"off"` | After you die, watch a living player, where the host allows it. `"long_waits"` does it only on a long respawn wait. `"always"` does it every time. |
| `display.follow_camera_after` | `5.0` | For `"long_waits"`: how many seconds make a long wait. |
| `display.scoreboard_team_layout` | `"teams"` | `"teams"` gives each team a column. `"score"` lists everyone by score. |
| `display.scoreboard_background` | `true` | Draws a panel behind the scoreboard. |
| `display.scoreboard_background_color` | `"16, 16, 16, 150"` | The panel's color, as red, green, blue and alpha, each 0 to 255. |
| `display.show_quit_players` | `true` | Keeps players who quit on the scoreboard. |
| `display.server_messages` | `true` | Shows a Chupathingy server's notices. On the Xbox they appear as chat lines. |
| `display.version_watermark` | `false` | Shows the build's version in the corner while you play. |

## Sound, chat and game

| Setting | Default | What it does |
| --- | --- | --- |
| `audio.music_volume` | `1.0` | Music volume, from 0.0 to 1.0. |
| `audio.effects_volume` | `1.0` | Effects and speech volume, from 0.0 to 1.0. |
| `chat.enabled` | `true` | Text chat with Chupathingy players. |
| `chat.show` | `"all"` | Whose chat you see: `"all"`, `"team"` or `"off"`. |
| `chat.filter` | `"mask"` | Blocked words: `"mask"` stars them out, `"hide"` hides the line, `"show"` shows it. |
| `chat.streamer_mode` | `false` | Shows other players in chat by their team's name. |
| `chat.quick_messages` | six lines | The controller's quick lines, separated by `\|`. **D-pad up** picks one, **D-pad down** sends it. |
| `game.start_with_one_player` | `true` | A game you host can start with only you. |
| `game.unlock_all_levels` | `false` | New profiles start with every campaign level unlocked. |
| `game.console_log` | `"important"` | What the on-screen console shows: `"important"`, `"all"` or `"none"`. `debug.txt` always has every line. |
| `game.pack_menus` | `""` | `"cex"` uses the Halo CE+ pack's own menus from `maps_cex\ui.map`. Empty keeps the game's menus. |

## Network

| Setting | Default | What it does |
| --- | --- | --- |
| `network.online` | `true` | Internet play. It is off when `console.bypass_security` is `false`. |
| `network.report_joined_games` | `true` | Sends the scores of online games you join to your service record. `false` keeps them off. |
| `network.late_join_wait` | `5` | In a game you host, how long a console joining late waits for its other controllers' players, from 0 to 30 seconds. |
| `network.tunnel_port` | `0` | The UDP port for internet play. `0` picks a new one each time. |
| `network.brokers_file` | `"brokers.txt"` | A file next to `default.xbe` that lists the servers that connect invites, one `host:port` a line. Use it only if the built-in ones can't be reached. |
| `network.stun_servers` | two servers | The servers that find your internet address, as `host:port`, separated by commas. |

## Developer builds only

A build made with `--console-debug-tools` also has `debug.audio_probe` and
`debug.audio_capture_seconds`. They record the console's sound for
comparing with the original game. `port/xbox/README.md` explains them.
Test builds don't have them.

## Files next to `default.xbe`

| File | What it is |
| --- | --- |
| `config.toml` | The settings on this page. |
| `debug.txt` | The game's log. Send it with a bug report. |
| `delta_device.key` | Made when you link a Delta profile. Keep it to yourself: a copy plays as your console. |
| `delta_link.txt` | The linked profile's handle. |
| `game_list_player.key` | Optional. Your identity file from your profile page on the site. Keep it to yourself. |
