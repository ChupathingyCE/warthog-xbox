/*
XBOX_MAPS.C

xbox_maps.h. The store's plain HTTP host, as the store's own client: every Custom
Edition map on it is checked against the signed list's BLAKE2b as it comes (ce.c),
every pack's map against Warthog's fingerprint (core.c), and a bad one deleted.
Maps go beside default.xbe: D:\maps_ce, D:\maps_<pack>.
*/

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xbox_maps.h"

void platform_log(const char *format, ...);
int console_memory_extras(void);

#define MAPS_BASE "http://api.xboxhomebrew.store"
#define MAPS_GAME "D:"
/* every section's maps (ce.h: 0), the archive's multiplayer, AI, modified and
single player maps alike */
#define MAPS_SECTION 0
/* (http's read buffer is on it, and a detail's url and paths) */
#define MAPS_STACK_SIZE (128 * 1024)

enum
{
	JOB_NONE,
	JOB_PAGE,
	JOB_DETAIL,
	JOB_PACKS,
	JOB_INSTALL_DETAIL,
	JOB_REMOVE_DETAIL,
	JOB_INSTALL_PACK,
	JOB_REMOVE_PACK,
	JOB_PICTURE
};

volatile long xbox_maps_cancel;

static struct
{
	ce_entry rows[XBOX_MAPS_ROWS];
	int row_count, total;
	ce_detail detail;
	map_entry *packs;
	int pack_count;
	/* the job's, then read when it is done */
	int job, offset, index;
	long fid;
	char status[128];
	int failed;
	/* counts what's said, so the screen can show each for a while */
	int status_serial;
	/* the selected entry's picture: 0xRRGGBB, as ce.c decodes the store's dxt1 */
	unsigned int *picture;
	long picture_fid;
	int picture_width, picture_height;
	int picture_serial;
	/* as it goes, read by the main thread */
	char progress_name[64];
	volatile unsigned long done, total_bytes;
} maps;

static volatile LONG maps_busy;

static void maps_ce_progress(const char *file, unsigned long done, unsigned long total, void *context)
{
	(void)context;
	if (strcmp(maps.progress_name, file))
		strncpy(maps.progress_name, file, sizeof(maps.progress_name) - 1);
	maps.done = done;
	maps.total_bytes = total;
}

static void maps_pack_progress(unsigned long done, unsigned long total, void *context)
{
	(void)context;
	maps.done = done;
	maps.total_bytes = total;
}

static DWORD WINAPI maps_thread(LPVOID parameter)
{
	char error[128];
	int result = 1;

	(void)parameter;
	error[0] = 0;
	switch (maps.job)
	{
	case JOB_PAGE:
		maps.row_count = ce_fetch_page(MAPS_BASE, MAPS_SECTION, maps.offset, XBOX_MAPS_ROWS, maps.rows, XBOX_MAPS_ROWS,
			&maps.total, error, sizeof(error));
		result = maps.row_count >= 0;
		if (!result)
			maps.row_count = 0;
		break;
	case JOB_DETAIL:
		memset(&maps.detail, 0, sizeof(maps.detail));
		result = ce_fetch_detail(MAPS_BASE, maps.fid, &maps.detail, error, sizeof(error));
		if (result)
			ce_check(MAPS_GAME, &maps.detail);
		else
			maps.detail.fid = 0;
		break;
	case JOB_PACKS:
	{
		int index;

		maps.pack_count = core_fetch_list(MAPS_BASE, maps.packs, XBOX_MAPS_PACKS, error, sizeof(error));
		result = maps.pack_count >= 0;
		if (!result)
			maps.pack_count = 0;
		for (index = 0; index < maps.pack_count; index++)
			core_check(MAPS_GAME, &maps.packs[index]);
		break;
	}
	case JOB_INSTALL_DETAIL:
		result = ce_install(MAPS_BASE, MAPS_GAME, &maps.detail, maps_ce_progress, NULL, error, sizeof(error));
		if (result)
			snprintf(maps.status, sizeof(maps.status), "Installed. It's in the map list now.");
		break;
	case JOB_REMOVE_DETAIL:
		result = ce_remove(MAPS_GAME, &maps.detail, error, sizeof(error));
		if (result)
			snprintf(maps.status, sizeof(maps.status), "Removed.");
		break;
	case JOB_INSTALL_PACK:
		strncpy(maps.progress_name, maps.packs[maps.index].file, sizeof(maps.progress_name) - 1);
		result = core_install(MAPS_BASE, MAPS_GAME, &maps.packs[maps.index], maps_pack_progress, NULL, error, sizeof(error));
		if (result)
			snprintf(maps.status, sizeof(maps.status), "Installed. It's in the map list now.");
		break;
	case JOB_REMOVE_PACK:
		result = core_remove(MAPS_GAME, &maps.packs[maps.index], error, sizeof(error));
		if (result)
			snprintf(maps.status, sizeof(maps.status), "Removed.");
		break;
	case JOB_PICTURE:
		/* (a map with no picture is no error: its box says so) */
		if (ce_fetch_picture(MAPS_BASE, maps.detail.fid, maps.detail.picture, maps.picture, XBOX_MAPS_PICTURE_WIDTH,
			XBOX_MAPS_PICTURE_HEIGHT, &maps.picture_width, &maps.picture_height, error, sizeof(error)))
		{
			maps.picture_fid = maps.detail.fid;
			maps.picture_serial++;
		}
		else
		{
			maps.picture_fid = -maps.detail.fid;
			platform_log("map browser: no picture for %ld (%s): %s", maps.detail.fid, maps.detail.picture, error);
		}
		break;
	}
	if (result && (maps.job == JOB_INSTALL_DETAIL || maps.job == JOB_REMOVE_DETAIL || maps.job == JOB_INSTALL_PACK ||
		maps.job == JOB_REMOVE_PACK))
	{
		maps.failed = 0;
		maps.status_serial++;
	}
	if (!result)
	{
		maps.failed = 1;
		maps.status_serial++;
		if (xbox_maps_cancel)
			snprintf(maps.status, sizeof(maps.status), "Stopped.");
		else
			snprintf(maps.status, sizeof(maps.status), "%s", error[0] ? error : "That didn't work.");
		platform_log("map browser: %s", maps.status);
	}
	__atomic_store_n(&maps_busy, 0, __ATOMIC_SEQ_CST);
	return 0;
}

static int maps_start(int job)
{
	HANDLE thread;

	if (maps_busy)
		return 0;
	maps.job = job;
	maps.done = maps.total_bytes = 0;
	maps.progress_name[0] = 0;
	xbox_maps_cancel = 0;
	__atomic_store_n(&maps_busy, 1, __ATOMIC_SEQ_CST);
	thread = CreateThread(NULL, MAPS_STACK_SIZE, maps_thread, NULL, 0, NULL);
	if (!thread)
	{
		__atomic_store_n(&maps_busy, 0, __ATOMIC_SEQ_CST);
		snprintf(maps.status, sizeof(maps.status), "No memory to start that.");
		maps.failed = 1;
		return 0;
	}
	CloseHandle(thread);
	return 1;
}

int xbox_maps_open(void)
{
	if (!maps.packs)
		maps.packs = (map_entry *)malloc(XBOX_MAPS_PACKS * sizeof(map_entry));
	/* (a console that has the room: the picture's download alone is 512 KB) */
	if (!maps.picture && console_memory_extras())
		maps.picture = (unsigned int *)malloc(XBOX_MAPS_PICTURE_WIDTH * XBOX_MAPS_PICTURE_HEIGHT * sizeof(unsigned int));
	maps.pack_count = 0;
	maps.picture_fid = 0;
	maps.status[0] = 0;
	return maps.packs != NULL;
}

void xbox_maps_close(void)
{
	/* (a thread still running has what it works on: its memory stays until the
	next time, and it stops at its next chance) */
	xbox_maps_cancel = 1;
	if (!maps_busy && maps.packs)
	{
		free(maps.packs);
		maps.packs = NULL;
	}
}

int xbox_maps_busy(void)
{
	return maps_busy != 0;
}

char const *xbox_maps_status(int *failed, int *serial)
{
	if (failed)
		*failed = maps.failed;
	if (serial)
		*serial = maps.status_serial;
	return maps.status;
}

int xbox_maps_progress(char *name, int name_size, unsigned long *done, unsigned long *total)
{
	if (!maps_busy || (maps.job != JOB_INSTALL_DETAIL && maps.job != JOB_INSTALL_PACK))
		return 0;
	snprintf(name, name_size, "%s", maps.progress_name);
	*done = maps.done;
	*total = maps.total_bytes;
	return 1;
}

int xbox_maps_load_page(int offset)
{
	if (maps_busy)
		return 0;
	maps.offset = offset;
	return maps_start(JOB_PAGE);
}

int xbox_maps_load_detail(long fid)
{
	if (maps_busy)
		return 0;
	maps.fid = fid;
	return maps_start(JOB_DETAIL);
}

int xbox_maps_load_packs(void)
{
	return maps.packs ? maps_start(JOB_PACKS) : 0;
}

int xbox_maps_load_picture(void)
{
	if (maps_busy || !maps.picture || !maps.detail.fid || !maps.detail.picture[0])
		return 0;
	return maps_start(JOB_PICTURE);
}

long xbox_maps_picture(unsigned int const **pixels, int *width, int *height, int *serial)
{
	*pixels = maps.picture;
	*width = maps.picture_width;
	*height = maps.picture_height;
	*serial = maps.picture_serial;
	return maps.picture_fid;
}

int xbox_maps_install_detail(void)
{
	return maps_start(JOB_INSTALL_DETAIL);
}

int xbox_maps_remove_detail(void)
{
	return maps_start(JOB_REMOVE_DETAIL);
}

int xbox_maps_install_pack(int index)
{
	if (maps_busy || index < 0 || index >= maps.pack_count)
		return 0;
	maps.index = index;
	return maps_start(JOB_INSTALL_PACK);
}

int xbox_maps_remove_pack(int index)
{
	if (maps_busy || index < 0 || index >= maps.pack_count)
		return 0;
	maps.index = index;
	return maps_start(JOB_REMOVE_PACK);
}

void xbox_maps_stop(void)
{
	xbox_maps_cancel = 1;
}

ce_entry const *xbox_maps_rows(int *count, int *total)
{
	*count = maps.row_count;
	*total = maps.total;
	return maps.rows;
}

ce_detail const *xbox_maps_detail(void)
{
	return maps.detail.fid ? &maps.detail : NULL;
}

map_entry const *xbox_maps_packs(int *count)
{
	*count = maps.pack_count;
	return maps.packs;
}
