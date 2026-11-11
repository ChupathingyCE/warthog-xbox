/*
XBOX_MEMORY.C

The console's memory (xbox_memory.h), read the first time anything asks:
physical_memory_allocate, at the start, asks for the texture cache's size.

The total is the system's own answer. GlobalMemoryStatus (xapilib, the
SDK's) gives dwTotalPhys; if it gives nothing, the kernel's
MmQueryStatistics (xboxkrnl, which the SDK's headers leave out) gives the
physical pages. Both count what the kernel uses, so a 128 MB console
reads 128 MB only when the XBE does not ask for 64 (port/xbox/build.py's
cxbe -LIMIT64MB:no) and the kernel knows the upper 64 MB (a devkit, or a
modified BIOS that supports the upgrade). Allocating 70 MB and freeing it
was suggested as a test; it is not needed with this toolchain, and would
fragment the memory the game then lays out, so it is only the fallback if a
toolchain ever lacks both calls.

The class is 64 MB (a stock Xbox: the baseline, which every feature must
run on), 128 MB (the common upgrade, and a devkit) or 256 MB (the rarer
mods). A devkit started with the XBE's 64 MB flag is in the 64 MB class,
and so is any console whose D:\config.toml has console.memory_class = "64"
(xbox_config.h): for a test of the baseline, or if an extra misbehaves.
The default, "auto", is the console's own.

What follows from the class (console_memory_extras, halo_console_memory.h:
a console above 64 MB has the extras). Nothing of the simulation or the
network changes with it: the game state, the objects and what goes on the
wire stay the Xbox's, so every console and PC plays the same game.
- the texture cache: the Xbox's 22 MB on a 64 MB console; 44 MB on a 128
MB one, 64 MB on a 256 MB one, kept in the low 128 MB, which the GPU
reaches. A 64 MB console whose free memory cannot hold the Xbox's caches
and still leave Direct3D and the heap theirs gets a smaller texture cache
(down to 19 MB) rather than a failed start, a megabyte larger only while
_main_loop_reserve is still expected free at the main loop.
- the sound cache: the Xbox's 4 MB, 8 MB with the extras (and twice the
sounds in it at once), in the low 128 MB too.
- the shared code's own (halo_port_capacity.h, network_connection.c): the
dynamic triangles (32768, 65536 with the extras), the transparent geometry
groups (384, 1024), the network connection's queues (four times, the
desktop builds').
A feature that only a bigger console can carry asks console_memory_extras,
or xbox_memory_require for a message at the main menu when it cannot run.

A devkit tests the 64 MB path with the XBE's own flag (tools/xbox_package.py
--devkit-64mb: XINIT_LIMIT_DEVKIT_MEMORY), which makes its kernel give the
game 64 MB, as a stock console has. console.simulate_64mb, which held the
memory above 64 MB from inside the game, is replaced by it.

From the first line of main (xbox_memory_startup), each step of the start
is written to D:\debug.txt at once ("boot:" lines: the memory, each cache
as it is made, each large heap allocation), the file closed after each, so
a console that stops before the game's own log opens still leaves where.

Only the parse of a total into a class (xbox_memory_class_of and
xbox_memory_delta_class_of) is built on the host (port/xbox/tests).
*/

#include "../include/xbox_memory.h"

/* XINIT_LIMIT_DEVKIT_MEMORY, in the XBE header's init flags (at 0x124:
tools/xbox_package.py --devkit-64mb) */
#define XBE_INIT_LIMIT_DEVKIT_MEMORY 0x00000004UL

int xbox_memory_class_of(unsigned long total_bytes)
{
	if (!total_bytes)
		return _xbox_memory_class_unknown;
	if (total_bytes < 96UL << 20)
		return _xbox_memory_class_64mb;
	if (total_bytes < 192UL << 20)
		return _xbox_memory_class_128mb;
	return _xbox_memory_class_256mb;
}

int xbox_memory_delta_class_of(int memory_class)
{
	/* (docs/delta.md's: log2 of the megabytes, 0 unknown) */
	switch (memory_class)
	{
	case _xbox_memory_class_64mb:
		return 6;
	case _xbox_memory_class_128mb:
		return 7;
	case _xbox_memory_class_256mb:
		return 8;
	default:
		return 0;
	}
}

#ifdef _XBOX

#include <xtl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_config.h"
#include "port/linux/src/port_config.h"

unsigned long __cdecl DbgPrint(const char *format, ...);
void platform_log(const char *format, ...);
/* the game's (interface/ui_widget.c) */
void display_error_text_deferred(const wchar_t *text, short local_player_index);
unsigned char main_menu_is_active(void);

/* the kernel's (MM_STATISTICS: its Length must be this size, 36 bytes, or
the call fails) */
typedef struct
{
	ULONG Length;
	ULONG TotalPhysicalPages;
	ULONG AvailablePages;
	ULONG VirtualMemoryBytesCommitted;
	ULONG VirtualMemoryBytesReserved;
	ULONG CachePagesCommitted;
	ULONG PoolPagesCommitted;
	ULONG StackPagesCommitted;
	ULONG ImagePagesCommitted;
} xbox_mm_statistics;

typedef char xbox_mm_statistics_size_assert[sizeof(xbox_mm_statistics) == 36 ? 1 : -1];

LONG __stdcall MmQueryStatistics(xbox_mm_statistics *statistics);

/* the game state (the Xbox's 0x345000, a test build of more than 16 players'
too: halo_port_capacity.h, which this unit does not see) */
#define _game_state_size 0x345000

/* a test build of more than 16 players (configure.py --console-players):
what it takes beyond a stock build's, measured on the build (the XBE's
static data, halo.map: 124 KB at 24 players, the netcode's arrays) and
logged where it counts (xbox_memory_log_point) */
#if defined(HALO_CONSOLE_PLAYERS) && HALO_CONSOLE_PLAYERS > 16
#define XBOX_MEMORY_PLAYERS_TEST 1
#define XBOX_MEMORY_STRING_OF(x) #x
#define XBOX_MEMORY_WIDE_OF(x) L ## x
#define XBOX_MEMORY_WIDE(x) XBOX_MEMORY_WIDE_OF(x)
/* "24", wide, from HALO_CONSOLE_PLAYERS */
#define XBOX_MEMORY_PLAYERS_TEXT XBOX_MEMORY_WIDE(XBOX_MEMORY_STRING(HALO_CONSOLE_PLAYERS))
#define XBOX_MEMORY_STRING(x) XBOX_MEMORY_STRING_OF(x)
#endif

enum
{
	_texture_cache_stock = 0x1600000,
	/* the smallest the cache is made on a console short of memory: the
	attract movie borrows 16 MB of it (bink_playback.c's largest pool,
	texture_cache_steal_memory), between two guards of 0x104000 */
	_texture_cache_minimum = 0x1300000,
	_texture_cache_128mb = 0x2C00000,
	_texture_cache_256mb = 0x4000000,
	/* the sound cache: the Xbox's, and the extras' (xbox_sound_cache.c's
	SOUND_CACHE_MAXIMUM_PAGE_COUNT pages of 4 KB at most) */
	_sound_cache_stock = 0x400000,
	_sound_cache_extras = 0x800000,
	/* (the largest is halo_port_capacity.h's
	HALO_PORT_TEXTURE_CACHE_MAXIMUM_SIZE, the texture cache's arrays' size;
	the GPU's reach, where a raised cache is kept) */
	_texture_cache_maximum_address = 0x7FFFFFF,
	/* the screen effect's swizzled target, 1024x512 at 4 bytes a pixel
	(rasterizer_xbox_screen_effect.c), where Direct3D keeps its own
	textures and targets: below 0x3FFB000 */
	_screen_effect_target_size = 0x200000,
	_screen_effect_target_limit = 0x3FFB000,

	/* what physical_memory_allocate takes besides the texture cache: the
	game state, the tag cache and the sound cache (physical_memory_map.c's
	sizes, the Xbox game's) */
	_other_caches_size = _game_state_size + 0x1600000 + _sound_cache_stock,
	/* what the game takes after the caches are made, up to the main loop:
	the Direct3D device (two 640x480 buffers and the depth buffer, 3.5 MB,
	and its push buffer), the rasterizer's pools, the game's other heap
	allocations and the system's (the sound's effects image among them).
	Measured on the devkit in the 64 MB class: 7304 KB before the effects
	image was downloaded (2026-10-20, builds 37857a71a and 02fead662:
	8644 KB free after the caches, 1340 KB at the main loop); with it, the
	leaner image (2033201d6) left 844 KB at the main loop with a 20 MB
	cache it had only just stepped up to, which puts the use at up to
	7741 KB. The larger, rounded up: a guess too high only keeps more free */
	_used_after_caches = 0x790000,
	/* what a 64 MB console keeps free at the main loop, at least, when the
	texture cache is sized from what it has free: the menus, a join and a
	map's start take their allocations from it (the game list's and the
	network's queues, the UI's widgets), and a join has failed with about
	56 KB left. A cache 1 MB larger is made only if this much would still
	be free after it */
	_main_loop_reserve = 0x180000,
	/* what must stay free once the caches are made */
	_free_after_caches = _used_after_caches + _main_loop_reserve,

	/* how often xbox_memory_update looks at the free memory (main loop
	passes), and how far below the last low it must be to be logged */
	_low_check_interval = 300,
	_low_log_step = 0x40000
};

static struct
{
	int known;
	unsigned long total;
	unsigned long available;
	const char *source;
	/* the class of the console's memory, and the class the game uses (held
	to 64 MB by the XBE's flag or console.memory_class), with why */
	int hardware_class;
	int memory_class;
	const char *held_by;
	unsigned long texture_cache_size;
	unsigned long sound_cache_size;
	int texture_cache_reduced;
	/* the screen effect's swizzled target (xbox_memory_screen_effect_target) */
	void *screen_effect_target;
	int screen_effect_target_tried;
	const wchar_t *message;
	int message_shown;
	unsigned long lowest_free;
	long update_count;
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* the objects' peaks since the last map started */
	long object_sample_count;
	long objects_peak;
	long object_pool_used_peak;
	long object_pool_contiguous_least;
	long objects_maximum;
	long object_pool_size;
#endif
} memory;

/* the commit and time of this build (tools/xbox_stamp.py, regenerated by every ninja run) */
extern const char xbox_build_stamp[];

/* the game's debug.txt lines, which wait in memory (source/cseries/errors.c) */
void error_log_flush(void);

void xbox_boot_log(const char *format, ...)
{
	char text[512];
	va_list arguments;
	FILE *file;

	va_start(arguments, format);
	_vsnprintf(text, sizeof(text) - 1, format, arguments);
	va_end(arguments);
	text[sizeof(text) - 1] = 0;
	DbgPrint("halo: %s\n", text);
	/* (the game's lines first, which wait in memory: errors.c) */
	error_log_flush();
	/* (opened and closed for each line: a console that stops on the next
	line still leaves this one in the file) */
	file = fopen("d:\\debug.txt", "a+b");
	if (file)
	{
		fprintf(file, "boot: %s\r\n", text);
		fclose(file);
	}
}

static void status_read(unsigned long *total, unsigned long *available, const char **source)
{
	MEMORYSTATUS status;

	memset(&status, 0, sizeof(status));
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	*total = status.dwTotalPhys;
	*available = status.dwAvailPhys;
	*source = "GlobalMemoryStatus";
	if (!*total)
	{
		xbox_mm_statistics statistics;

		memset(&statistics, 0, sizeof(statistics));
		statistics.Length = sizeof(statistics);
		*source = "MmQueryStatistics";
		if (MmQueryStatistics(&statistics) >= 0)
		{
			*total = statistics.TotalPhysicalPages << 12;
			*available = statistics.AvailablePages << 12;
		}
	}
}

unsigned long xbox_memory_free(void)
{
	unsigned long total, available;
	const char *source;

	status_read(&total, &available, &source);
	return available;
}

static void memory_read(void)
{
	if (memory.known)
		return;
	memory.known = 1;
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* a test build (configure.py --console-players): its network tables hold
	more than the Xbox's 16 players. The game state stays the Xbox's
	(halo_port_capacity.h); the rest is about 170 KB more of ordinary
	memory, which a 64 MB console is expected to hold, so it starts on
	every console and says so (the first message, shown at the main menu) */
	xbox_boot_log("players: %d, a test build (the Xbox's: 16); game state %lu KB, as the Xbox's",
		HALO_CONSOLE_PLAYERS, (unsigned long)_game_state_size >> 10);
	memory.message = L"Test build: up to " XBOX_MEMORY_PLAYERS_TEXT L" players.\r\nExperimental: please send D:\\debug.txt.";
#endif
	status_read(&memory.total, &memory.available, &memory.source);
	if (config_boolean("console.simulate_64mb"))
		xbox_boot_log("console.simulate_64mb is deprecated and ignored: a devkit gets 64 MB from the XBE's flag "
			"(tools/xbox_package.py --devkit-64mb)");
	if (config_boolean("console.large_caches"))
		xbox_boot_log("console.large_caches is deprecated and ignored: a console of 128 MB or more has the larger "
			"caches on its own (console.memory_class = \"64\" keeps the Xbox's)");
	memory.hardware_class = xbox_memory_class_of(memory.total);
	memory.memory_class = memory.hardware_class;
	/* a devkit started with XINIT_LIMIT_DEVKIT_MEMORY (the XBE's init flags,
	which the kernel keeps in the header at the image's base) still reports
	its 128 MB in all, but gives the title only a stock console's: it is in
	the 64 MB class */
	if ((*(unsigned long const *)0x00010124UL & XBE_INIT_LIMIT_DEVKIT_MEMORY) &&
		memory.memory_class > _xbox_memory_class_64mb)
	{
		memory.memory_class = _xbox_memory_class_64mb;
		memory.held_by = "the XBE's 64 MB flag";
	}
	/* the player's: "64" holds the console to the baseline; "auto" (the
	default, or anything else, said once) is the console's own */
	{
		const char *setting = config_string("console.memory_class");

		if (setting && !strcmp(setting, "64"))
		{
			if (memory.memory_class > _xbox_memory_class_64mb)
			{
				memory.memory_class = _xbox_memory_class_64mb;
				memory.held_by = "console.memory_class";
			}
		}
		else if (setting && *setting && strcmp(setting, "auto"))
		{
			xbox_boot_log("memory: console.memory_class \"%s\" is not \"auto\" or \"64\": auto", setting);
		}
	}
	memory.lowest_free = memory.available;

	memory.texture_cache_size = _texture_cache_stock;
	memory.sound_cache_size = _sound_cache_stock;
	if (memory.memory_class == _xbox_memory_class_128mb)
	{
		memory.texture_cache_size = _texture_cache_128mb;
		memory.sound_cache_size = _sound_cache_extras;
	}
	else if (memory.memory_class == _xbox_memory_class_256mb)
	{
		memory.texture_cache_size = _texture_cache_256mb;
		memory.sound_cache_size = _sound_cache_extras;
	}
	else if (memory.available < _other_caches_size + _free_after_caches + _texture_cache_stock)
	{
		/* a stock console (or one that cannot say): the Xbox's cache if
		what is free holds it and still leaves Direct3D and the heap
		theirs, else what does, down to the smallest the game can use */
		/* the cache grows in steps of 1 MB from the smallest, a step only
		if _main_loop_reserve is still expected free at the main loop
		after it (the free memory, less the other caches, the cache and
		_used_after_caches): the rest is headroom, not texture cache */
		unsigned long room = memory.available > _other_caches_size + _free_after_caches ?
			memory.available - _other_caches_size - _free_after_caches : 0;

		room &= ~0xFFFFFUL;
		memory.texture_cache_size = room > _texture_cache_minimum ? room : _texture_cache_minimum;
#ifdef XBOX_MEMORY_PLAYERS_TEST
		/* a test build of more than 16 players keeps the smallest: the
		room is counted in whole megabytes, and a console a few KB over
		the next would give the cache a megabyte the heap then lacks
		(the extra players' netcode takes 170 KB of it at 24 players,
		about 360 KB at 32) */
		memory.texture_cache_size = _texture_cache_minimum;
#endif
		memory.texture_cache_reduced = 1;
	}

	xbox_boot_log("memory: %lu MB in all, %lu KB free at the start (%s), image %lu KB: the %d MB class",
		memory.total >> 20, memory.available >> 10, memory.source,
		/* (the XBE's header, which the kernel keeps at the image's base:
		its SizeOfImage) */
		*(unsigned long const *)0x0001010CUL >> 10, memory.hardware_class);
	if (memory.memory_class > _xbox_memory_class_64mb)
	{
		xbox_boot_log("memory: %d MB console, extras on: texture cache %lu KB, sound cache %lu KB, and larger "
			"render buffers and network queues", memory.memory_class, memory.texture_cache_size >> 10,
			memory.sound_cache_size >> 10);
	}
	else if (memory.held_by)
	{
		xbox_boot_log("memory: %d MB console held to 64 MB by %s, extras off: the Xbox's sizes",
			memory.hardware_class, memory.held_by);
	}
	else
	{
		xbox_boot_log("memory: 64 MB console, extras off: the Xbox's sizes");
	}
	if (memory.texture_cache_reduced)
	{
		/* the decision: what is expected free at the main loop with this
		cache, and with a step more (signed: a console short of the
		smallest cache's room expects less than nothing) */
		long expected = (long)(memory.available >> 10) - (long)(_other_caches_size >> 10) -
			(long)(memory.texture_cache_size >> 10) - (long)(_used_after_caches >> 10);

		xbox_boot_log("memory: texture cache %lu KB (less than the Xbox's 22 MB: what this console has free); "
			"expected %ld KB free at the main loop, %ld KB with 1 MB more (the reserve: %lu KB)",
			memory.texture_cache_size >> 10, expected, expected - 1024, (unsigned long)_main_loop_reserve >> 10);
	}
}

void xbox_memory_startup(void)
{
	xbox_boot_log("start (build %s)", xbox_build_stamp);
	/* (the settings first: the memory's are among them) */
	xbox_config_startup();
	memory_read();
}

/* physical_memory_allocate's: each cache as it is made */
void xbox_memory_allocation(const char *name, unsigned long size, const void *address)
{
	if (address)
		xbox_boot_log("memory: %s %lu KB at %p, %lu KB free", name, size >> 10, address, xbox_memory_free() >> 10);
	else
		xbox_boot_log("memory: %s %lu KB FAILED, %lu KB free", name, size >> 10, xbox_memory_free() >> 10);
}

/* debug_malloc's (cseries/debug_memory.c): the large allocations, and any
that fails */
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address)
{
	const char *name = file ? strrchr(file, '\\') : NULL;

	name = name ? name + 1 : file ? file : "?";
	if (address)
		xbox_boot_log("heap: %lu KB for %s,#%ld, %lu KB free", size >> 10, name, line, xbox_memory_free() >> 10);
	else
		xbox_boot_log("heap: %lu KB for %s,#%ld FAILED, %lu KB free", size >> 10, name, line,
			xbox_memory_free() >> 10);
}

#ifdef XBOX_MEMORY_PLAYERS_TEST
/* the game's (saved games/game_state.c, objects/objects.c) */
void game_state_cpu_usage(long *used, long *size);
int objects_usage(long *count, long *maximum, long *pool_free, long *pool_size, long *pool_contiguous_free);

/* the largest run of contiguous physical memory free, to 16 KB: each size
tried is taken and given back at once (XPhysicalAlloc, as the caches and
Direct3D's resources are), so this is only done at the few points that
log it. The heap does not need contiguous memory; Direct3D and the
caches do. */
static unsigned long largest_free_block(unsigned long available)
{
	unsigned long low = 0;
	unsigned long high = (available & ~0x3FFFUL) + 0x4000;

	while (high - low > 0x4000)
	{
		unsigned long middle = (low + (high - low) / 2) & ~0x3FFFUL;
		void *block;

		if (middle <= low)
			middle = low + 0x4000;
		block = XPhysicalAlloc(middle, MAXULONG_PTR, 0, PAGE_READWRITE);
		if (block)
		{
			XPhysicalFree(block);
			low = middle;
		}
		else
		{
			high = middle;
		}
	}
	return low;
}

static void objects_sample(void)
{
	long count, maximum, pool_free, pool_size, pool_contiguous_free;

	if (!objects_usage(&count, &maximum, &pool_free, &pool_size, &pool_contiguous_free))
		return;
	memory.objects_maximum = maximum;
	memory.object_pool_size = pool_size;
	if (!memory.object_sample_count || pool_contiguous_free < memory.object_pool_contiguous_least)
		memory.object_pool_contiguous_least = pool_contiguous_free;
	memory.object_sample_count++;
	if (count > memory.objects_peak)
		memory.objects_peak = count;
	if (pool_size - pool_free > memory.object_pool_used_peak)
		memory.object_pool_used_peak = pool_size - pool_free;
}
#endif

/* a test build's line at the main loop, each map's start (a game joined or
hosted, or the main menu's) and each map's end: the players, the memory
free and its largest block, the game state's use and, at a map's end, the
objects' peaks over it (HALO_PORT_MAXIMUM_OBJECTS_PER_MAP and the object
pool) */
void xbox_memory_log_point(const char *where)
{
#ifdef XBOX_MEMORY_PLAYERS_TEST
	unsigned long available = xbox_memory_free();
	long game_state_used, game_state_size;

	game_state_cpu_usage(&game_state_used, &game_state_size);
	xbox_boot_log("players: %d, %s: %lu KB free, largest free block %lu KB, game state %ld of %ld bytes used",
		HALO_CONSOLE_PLAYERS, where, available >> 10, largest_free_block(available) >> 10,
		game_state_used, game_state_size);
	if (memory.object_sample_count)
	{
		xbox_boot_log("objects: peak %ld of %ld, pool peak %ld KB used of %ld KB, least contiguous free %ld KB "
			"(%ld samples)", memory.objects_peak, memory.objects_maximum,
			memory.object_pool_used_peak >> 10, memory.object_pool_size >> 10,
			memory.object_pool_contiguous_least >> 10, memory.object_sample_count);
	}
	memory.object_sample_count = 0;
	memory.objects_peak = 0;
	memory.object_pool_used_peak = 0;
	memory.object_pool_contiguous_least = 0;
#else
	/* (a stock build: the free memory alone, which a join's map start is
	the one to watch - the network's pool, console.xnet_pages, and the
	connection's queues are taken by then) */
	xbox_boot_log("memory: %lu KB free at %s", xbox_memory_free() >> 10, where);
#endif
}

unsigned long xbox_memory_total(void)
{
	memory_read();
	return memory.total;
}

int xbox_memory_class(void)
{
	memory_read();
	return memory.memory_class;
}

/* the largest run of contiguous physical memory free below limit, to 64
KB and at most ceiling: each size tried is taken and given back at once */
static unsigned long largest_block_below(unsigned long limit, unsigned long ceiling)
{
	unsigned long low = 0;
	unsigned long high = (ceiling & ~0xFFFFUL) + 0x10000;

	while (high - low > 0x10000)
	{
		unsigned long middle = (low + (high - low) / 2) & ~0xFFFFUL;
		void *block;

		if (middle <= low)
			middle = low + 0x10000;
		block = XPhysicalAlloc(middle, limit, 0, PAGE_READWRITE);
		if (block)
		{
			XPhysicalFree(block);
			low = middle;
		}
		else
		{
			high = middle;
		}
	}
	return low;
}

/* a cache larger than the Xbox's (the 128 MB extras) as large as the
memory free in one piece allows when the game makes it, keeping 1 MB of
the room the rest needs free in that piece and never less than the Xbox's own size: a 128 MB console's
free memory is in pieces (a devkit, 2026-10-21: 97 MB free, the largest
piece 24 MB), and a 44 MB texture cache that could not be made left the
game with none (a black screen, and on a retail console its lights
flashing red and green). Decided once, when the game first asks. */
static void fit_extras_cache(const char *name, unsigned long *size, unsigned long stock, unsigned long keep,
	int *fitted)
{
	unsigned long largest;
	unsigned long fit;

	if (*fitted)
		return;
	*fitted = 1;
	if (*size <= stock)
		return;
	/* (keep: what the piece must still hold after this cache, for what
	is made after it in one piece: the sound cache and Direct3D's device,
	its frame buffers and push buffer. A 37 MB texture cache in a 39 MB
	piece left the sound cache and the device nowhere to go, and the game
	went back to the dashboard: 2026-10-21) */
	largest = largest_block_below(MAXULONG_PTR, *size + keep);
	if (largest >= *size + keep)
		return;
	fit = largest > keep ? (largest - keep) & ~0xFFFFFUL : 0;
	if (fit < stock)
		fit = stock;
	xbox_boot_log("memory: %s %lu KB is more than the largest free piece (%lu KB): %lu KB instead",
		name, *size >> 10, largest >> 10, fit >> 10);
	*size = fit;
}

static int texture_cache_fitted;
static int sound_cache_fitted;

/* the screen effect's target, made before the texture cache while the
memory is still in large pieces: on a 64 MB console its 2 MB come out of
the texture cache (memory_read's size, 22 MB, then 20 MB; a console whose
cache is already near its smallest goes without, and the effect draws as
the Xbox did); a bigger console's cache is fitted to what is left */
static void screen_effect_target_reserve(void)
{
	int from_cache;

	if (memory.screen_effect_target_tried)
		return;
	memory.screen_effect_target_tried = 1;
	from_cache = memory.memory_class == _xbox_memory_class_64mb;
	if (from_cache && memory.texture_cache_size < _texture_cache_minimum + _screen_effect_target_size)
	{
		xbox_boot_log("memory: no screen effect target: the texture cache (%lu KB) has no room for it",
			memory.texture_cache_size >> 10);
		return;
	}
	/* (anywhere, as the caches: a highest address below the top fails on
	a 128 MB devkit, as 0x7FFFFFF did for the caches, 2026-10-21; then
	the address is checked against Direct3D's limit) */
	memory.screen_effect_target = XPhysicalAlloc(_screen_effect_target_size, MAXULONG_PTR, 0,
		PAGE_READWRITE | PAGE_WRITECOMBINE);
	if (memory.screen_effect_target &&
		((unsigned long)memory.screen_effect_target & 0x7FFFFFFFUL) + _screen_effect_target_size > _screen_effect_target_limit)
	{
		xbox_boot_log("memory: screen effect target at %p is past the graphics chip's 64 MB: given back",
			memory.screen_effect_target);
		XPhysicalFree(memory.screen_effect_target);
		memory.screen_effect_target = NULL;
	}
	if (!memory.screen_effect_target)
	{
		xbox_boot_log("memory: screen effect target %lu KB FAILED, %lu KB free", (unsigned long)_screen_effect_target_size >> 10,
			xbox_memory_free() >> 10);
		return;
	}
	if (from_cache)
		memory.texture_cache_size -= _screen_effect_target_size;
	xbox_boot_log("memory: screen effect target %lu KB at %p%s, %lu KB free", (unsigned long)_screen_effect_target_size >> 10,
		memory.screen_effect_target, from_cache ? " (from the texture cache)" : "", xbox_memory_free() >> 10);
}

void *xbox_memory_screen_effect_target(unsigned long *size)
{
	*size = memory.screen_effect_target ? _screen_effect_target_size : 0;
	return memory.screen_effect_target;
}

unsigned long xbox_memory_texture_cache_size(void)
{
	memory_read();
	screen_effect_target_reserve();
	fit_extras_cache("texture cache", &memory.texture_cache_size, _texture_cache_stock, 12 * 0x100000,
		&texture_cache_fitted);
	return memory.texture_cache_size;
}

unsigned long xbox_memory_texture_cache_maximum_address(void)
{
	memory_read();
	/* (the Xbox's own cache, or a smaller one, anywhere, as it always
	was) */
	/* anywhere, as the Xbox's own cache always was: a limit of 128 MB
	(0x7FFFFFF) made every allocation fail on a 128 MB devkit, even one
	of the Xbox's own size, and the game ran without a texture cache (a
	black screen, then its lights flashing red and green; 2026-10-21) */
	return 0xFFFFFFFFUL;
}

unsigned long xbox_memory_sound_cache_size(void)
{
	memory_read();
	fit_extras_cache("sound cache", &memory.sound_cache_size, _sound_cache_stock, 8 * 0x100000, &sound_cache_fitted);
	return memory.sound_cache_size;
}

unsigned long xbox_memory_sound_cache_maximum_address(void)
{
	memory_read();
	/* (anywhere, as the texture cache's) */
	return 0xFFFFFFFFUL;
}

int console_memory_class(void)
{
	return xbox_memory_class();
}

int console_memory_extras(void)
{
	return xbox_memory_class() > _xbox_memory_class_64mb;
}

int xbox_memory_require(int memory_class, const wchar_t *text)
{
	memory_read();
	if (memory.memory_class >= memory_class)
		return 1;
	xbox_boot_log("memory: a feature needs the %d MB class; this console is in the %d MB class", memory_class,
		memory.memory_class);
	/* (one message at a time: the first) */
	if (!memory.message)
		memory.message = text;
	return 0;
}

/* Custom Edition maps' tag cache, at the address their tags are linked to
(chupathingyce's port/linux/src/platform.h), once the caches are made: on a
console above 64 MB, 23 MB of its own. A 64 MB console has no room for this:
there it's the Xbox's own tag cache, by the page tables (xbox_ce_files.c) */
int platform_ce_tag_cache_ready = 0;
int xbox_ce_alias_install(void);

static void ce_tag_cache_make(void)
{
	static int made;
	void *address;

	if (made)
		return;
	made = 1;
	memory_read();
	if (memory.memory_class == _xbox_memory_class_64mb)
	{
		/* (the Xbox's own tag cache, by the page tables, as a map loads: xbox_ce_files.c) */
		if (xbox_ce_alias_install())
		{
			platform_ce_tag_cache_ready = 1;
			xbox_boot_log("custom edition: a 64 MB console, its tag cache the Xbox's, %lu KB free",
				xbox_memory_free() >> 10);
		}
		else
			xbox_boot_log("custom edition: no tag cache on this 64 MB console, its maps are left out");
		return;
	}
	address = VirtualAlloc((void *)0x40440000UL, 0x01700000UL, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (address != (void *)0x40440000UL)
	{
		xbox_boot_log("custom edition: no tag cache at 40440000 (got %p, error %lu)", address, GetLastError());
		return;
	}
	platform_ce_tag_cache_ready = 1;
	xbox_boot_log("custom edition: tag cache 23552 KB at %p, %lu KB free", address, xbox_memory_free() >> 10);
}

void xbox_memory_update(void)
{
	ce_tag_cache_make();
	/* the free memory's new lows, every few seconds: a map's load and a
	match's peak */
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* the objects in use, twice a second or so */
	if (!(memory.update_count % 15))
		objects_sample();
#endif
	if (++memory.update_count >= _low_check_interval)
	{
		unsigned long available = xbox_memory_free();

		memory.update_count = 0;
		if (available + _low_log_step <= memory.lowest_free)
		{
			memory.lowest_free = available;
			platform_log("memory: %lu KB free (a new low)", available >> 10);
		}
	}
	if (!memory.message || memory.message_shown || !main_menu_is_active())
		return;
	memory.message_shown = 1;
	display_error_text_deferred(memory.message, -1);
}

int xbox_memory_delta_class(void)
{
	/* TODO(Delta Peer): this console's platform key (struct
	delta_platform_key, docs/delta.md) takes this as its memory_class once
	Delta Peer is built for the console */
	return xbox_memory_delta_class_of(xbox_memory_class());
}

#endif
