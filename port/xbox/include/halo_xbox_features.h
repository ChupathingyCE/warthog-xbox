/*
HALO_XBOX_FEATURES.H

The shared sources' extras the original Xbox's build leaves out
(port/linux/include/halo_port_features.h's HALO_CONSOLE_NO_* switches). Read
by every game unit through halo_xbox_prefix.h, and by the settings' unit
(port_config.c, which port/xbox/build.py compiles with the SDK's headers and
without the prefix), so the settings of what is left out leave the
console's table with it.
*/

#ifndef __HALO_XBOX_FEATURES_H
#define __HALO_XBOX_FEATURES_H

/* The debug decomp's developer tools (the profiler, the AI, render and
collision debug, the stack walker, libtiff and the bitmap tools) are not in
the console's build unless it is configured with --console-debug-tools
(HALO_CONSOLE_DEBUG_TOOLS). Their modules keep their code under
#ifndef HALO_CONSOLE_NO_DEBUG_TOOLS and answer their calls with stubs;
console_debug_tools_unavailable (port/xbox/game/console_debug_tools.c) says so
for the console commands and scripts that reach them. */
#ifndef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_NO_DEBUG_TOOLS
void console_debug_tools_unavailable(char const *name);
#endif

/* The consoles have no telnet script console (source/networking/
telnet_console.h) */
#define HALO_CONSOLE_NO_TELNET

/* and no automated network tests (port/linux/game/network_test.c) */
#define HALO_CONSOLE_NO_NETWORK_TEST

/* A console's game never exits: what would quit on the desktop (the audio
probe's end: port/linux/game/audio_probe.c) goes back to the main menu */
#define HALO_CONSOLE_NO_EXIT

/* and no high-res text or HUD: the maps' own fonts and bitmaps, as retail */
#define HALO_CONSOLE_NO_HIRES

/* and no players' names above their heads (hud.c): retail names the player
under the reticle only */
#define HALO_CONSOLE_NO_PLAYER_NAMES

/* and no tag schema or validator (port/linux/game/tag_validate.c: 170 KB of
the image, and already off on a 64 MB console, which has no room for its
claims): the maps are the Xbox's own */
#define HALO_CONSOLE_NO_TAG_VALIDATE

/* and, for now, none of the desktop's other extras that a console leaves out
(port/linux/include/halo_port_features.h): Discord and the updater, the
object functions drawn between ticks (384 KB), the Controls screen and Delta
Profile in the game */
#define HALO_CONSOLE_NO_DISCORD
#define HALO_CONSOLE_NO_UPDATER
#define HALO_CONSOLE_NO_SMOOTH_FUNCTIONS
#define HALO_CONSOLE_NO_CONTROLS
#define HALO_CONSOLE_NO_DELTA_PROFILE

/* and the audio probe and the recording of real play's sound
(port/linux/game/audio_probe.c, src/xbox_audio_capture.c): a developer's
tool, in a build with the developer tools only */
#ifndef HALO_CONSOLE_DEBUG_TOOLS
#define HALO_CONSOLE_NO_AUDIO_PROBE
#endif

#endif
