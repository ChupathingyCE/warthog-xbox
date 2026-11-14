/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * the content browser's core: the store's list of warthog maps
 * (/api/warthog/maps.txt), each map's state on this machine by warthog's own
 * fingerprint, installing one (download, check, maplist.txt) and removing it
 */

#ifndef CORE_H
#define CORE_H

// plenty for now, the store has like 2 lol
#define CORE_MAX_MAPS 256

enum
{
	MAP_MISSING,
	MAP_INSTALLED,
	MAP_OUTDATED, /* a file of that name with another fingerprint */
};

// one line of maps.txt plus whats on disk for it
typedef struct
{
	char pack[16];
	char file[40];
	char name[64];
	char type[16];
	// the lists fingerprint vs the one we read off disk (local_fingerprint)
	unsigned long fingerprint;
	unsigned long size;
	char download[192];
	char version[16];
	char item[40];
	int state;
	unsigned long local_fingerprint;
} map_entry;

/* progress: done of total bytes (total 0 when the server didn't say) */
typedef void (*core_progress_proc)(unsigned long done, unsigned long total, void *context);

/* the list from base (http://host[:port]), test pack included: the count, or
-1 with why in error */
int core_fetch_list(const char *base, map_entry *maps, int max, char *error, int error_size);

/* warthog's map_pack_fingerprint of a file: FNV-1a over its size and 16
samples of up to 64 KB; 0 if it can't be read */
unsigned long core_fingerprint(const char *path);

/* warthog's folder on this machine: a default.xbe with a config.toml and a
maps folder beside it, at most two levels into a root; else the fallback
folder. 1 if it is warthog's */
int core_find_warthog(char *out, int size);

/* a path and a name joined with the platform's separator */
void core_join(char *out, int size, const char *a, const char *b);

/* <game>\maps_<pack>\<file>.map */
void core_map_path(const char *game, const map_entry *map, char *out, int size);

/* the map's state here (and its local fingerprint) */
void core_check(const char *game, map_entry *map);

/* download, check the fingerprint, merge maplist.txt: 1, or 0 with why */
int core_install(const char *base, const char *game, map_entry *map, core_progress_proc progress, void *context,
	char *error, int error_size);

/* the file, its maplist.txt line, the folder once empty: 1, or 0 with why */
int core_remove(const char *game, map_entry *map, char *error, int error_size);

#endif
