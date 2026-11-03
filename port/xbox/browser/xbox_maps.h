/*
XBOX_MAPS.H

The map browser's work (xbox_maps.c), on a thread of its own, one job at a time,
for its screen (port/xbox/game/xbox_maps_screen.c): nothing the store answers
holds up the menus.
*/

#ifndef __XBOX_MAPS_H
#define __XBOX_MAPS_H

#include "ce.h"
#include "core.h"

#define XBOX_MAPS_ROWS 9
#define XBOX_MAPS_PACKS 64
/* the picture's largest, as the store's client shows it */
#define XBOX_MAPS_PICTURE_WIDTH 118
#define XBOX_MAPS_PICTURE_HEIGHT 90

enum
{
	XBOX_MAPS_TAB_CE,
	XBOX_MAPS_TAB_PACKS,
	XBOX_MAPS_TAB_COUNT
};

/* its memory (and the thread's work given up): the screen's opening and closing */
int xbox_maps_open(void);
void xbox_maps_close(void);

/* a job under way */
int xbox_maps_busy(void);
/* what the jobs last said (an error, or what it did), and a count of them */
char const *xbox_maps_status(int *failed, int *serial);
/* installing: the file's name, and done of total bytes (total 0: unknown) */
int xbox_maps_progress(char *name, int name_size, unsigned long *done, unsigned long *total);

/* each starts its job, or 0 if one is under way */
int xbox_maps_load_page(int offset);
int xbox_maps_load_detail(long fid);
int xbox_maps_load_packs(void);
/* the detail's picture, on a console with the memory for it */
int xbox_maps_load_picture(void);
int xbox_maps_install_detail(void);
int xbox_maps_remove_detail(void);
int xbox_maps_install_pack(int index);
int xbox_maps_remove_pack(int index);
/* a transfer under way stopped */
void xbox_maps_stop(void);

/* what the jobs have read (not to be read while one is under way) */
ce_entry const *xbox_maps_rows(int *count, int *total);
ce_detail const *xbox_maps_detail(void);
map_entry const *xbox_maps_packs(int *count);
/* the entry whose picture is held (negative: that entry has none), with its pixels,
their size and a count of the pictures that have come */
long xbox_maps_picture(unsigned int const **pixels, int *width, int *height, int *serial);

#endif
