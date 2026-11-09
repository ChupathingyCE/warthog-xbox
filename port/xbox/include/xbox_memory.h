/*
XBOX_MEMORY.H

The console's memory (xbox_memory.c): a stock Xbox has 64 MB, a devkit and
most modified consoles 128 MB, a few mods more. Read once at the start from
the system (GlobalMemoryStatus, else the kernel's MmQueryStatistics),
logged, and used for what may grow with it: 64 MB is the baseline, and a
console above it has the extras (console_memory_extras,
port/linux/include/halo_console_memory.h, which this port answers). The
game's own limits never change with it: the network, the game state and
the objects stay the Xbox's (docs/cross-play.md), so every console plays
the same game.
*/

#ifndef __XBOX_MEMORY_H
#define __XBOX_MEMORY_H

#include <stddef.h>

enum
{
	_xbox_memory_class_unknown = 0,
	/* a stock Xbox */
	_xbox_memory_class_64mb = 64,
	/* a devkit, or a console with the common upgrade */
	_xbox_memory_class_128mb = 128,
	/* the rarer bigger mods */
	_xbox_memory_class_256mb = 256
};

/* the class of a total in bytes: 64 below 96 MB, 128 below 192 MB, 256
above (pure: the tests build it on the host) */
int xbox_memory_class_of(unsigned long total_bytes);

/* this console's total memory in bytes, and its class (read once): the
class the game uses, 64 MB on a console held to it (the XBE's 64 MB flag,
console.memory_class = "64") */
unsigned long xbox_memory_total(void);
int xbox_memory_class(void);

/* the texture cache's size (cache/physical_memory_map.c and
xbox_texture_cache.c), and the highest physical address it may have: the
Xbox's 22 MB, 44 MB on a 128 MB console (64 MB on a 256 MB one), less on a
64 MB console with too little free for it */
unsigned long xbox_memory_texture_cache_size(void);
unsigned long xbox_memory_texture_cache_maximum_address(void);
/* the sound cache's (physical_memory_map.c, xbox_sound_cache.c): the Xbox's
4 MB, 8 MB on a console above 64 MB */
unsigned long xbox_memory_sound_cache_size(void);
unsigned long xbox_memory_sound_cache_maximum_address(void);
/* the screen effect's swizzled target (rasterizer_xbox_screen_effect.c):
2 MB of contiguous memory made at the start, before the texture cache (out
of it on a 64 MB console), with its size; NULL and 0 if it could not be */
void *xbox_memory_screen_effect_target(unsigned long *size);

/* whether this console has the class a feature needs; if not, the menus
show text (wide characters, the error dialog's) once the main menu is up,
and the caller goes on without the feature. Never a crash */
int xbox_memory_require(int memory_class, const wchar_t *text);

/* the first thing main does (shell_xbox.c): the memory read and logged
(after D:\config.toml is read: xbox_config.h) before anything else is
allocated */
void xbox_memory_startup(void);

/* a line straight to D:\debug.txt ("boot: "), opened and closed for each,
for the start, before the game's log is up */
void xbox_boot_log(const char *format, ...);

/* the memory free now, in bytes */
unsigned long xbox_memory_free(void);

/* the start's allocations, logged: physical_memory_allocate's caches, and
debug_malloc's large ones and failures */
void xbox_memory_allocation(const char *name, unsigned long size, const void *address);
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);

/* the main loop's (main.c): a message from xbox_memory_require shown once
the main menu is up, and the free memory's new lows logged */
void xbox_memory_update(void);
/* a test build of more than 16 players' memory line (the main loop, each
map's start and end); nothing in a stock build */
void xbox_memory_log_point(const char *where);

/* Delta Peer's memory class (struct delta_platform_key's memory_class,
docs/delta.md: log2 of the megabytes, 6 for 64 MB, 7 for 128, 8 for 256,
0 unknown): of a class (pure), and this console's */
int xbox_memory_delta_class_of(int memory_class);
int xbox_memory_delta_class(void);

#endif
