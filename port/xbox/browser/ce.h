/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * custom edition maps from the store's archive: halomaps.org's pages (title,
 * author, description) with each one's maps from the store's mirror, every
 * map named in the store's json with its blake2b, and checked against it as
 * it comes (the json and the hashes come over https).
 * they go in <warthog>\maps_ce\, chupathingyce's folder of custom edition
 * maps on the desktop (where warthog will read them is milenko's call)
 */

#ifndef CE_H
#define CE_H

#define CE_MAX_MAPS 8

typedef struct
{
	long fid;
	char title[64];
	char author[48];
	char date[16];
	char rating[12];
	char listed_size[16];
	int maps, maps_here;
	/* the xbox's 22 MB tag space: 1 every map of it fits, 0 one won't (WON'T FIT
	ON A 64 MB CONSOLE), -1 not measured yet */
	int fits64;
} ce_entry;

typedef struct
{
	char file[64];
	unsigned long size;
	char blake2b[65];
	char checksum[12];
	int ready;
	char download[160];
	/* what it needs of the tag space (its tag data and largest bsp), and whether
	that's within the xbox's 22 MB: 1, 0, -1 not measured */
	unsigned long needs;
	int fits64;
} ce_map;

typedef struct
{
	long fid;
	char title[64];
	char author[48];
	char date[16];
	char category[40];
	/* the description, or the file notes when it has none (most of halomaps' authors
	wrote theirs there) */
	char description[600];
	/* its picture: the archive's cover (a screenshot's file name), "" for none */
	char picture[96];
	int map_count;
	ce_map maps[CE_MAX_MAPS];
	int state; /* core.h's MAP_*: every ready map here (by name and size) */
} ce_detail;

/* a page of archive entries with maps on the mirror (section 10: halo ce
maps): how many, -1 with why. total is every such entry */
int ce_fetch_page(const char *base, int section, const char *query, int offset, int count, ce_entry *out, int max,
	int *total,
	char *error, int error_size);
/* one entry's description and maps */
int ce_fetch_detail(const char *base, long fid, ce_detail *out, char *error, int error_size);
/* a map by its file's name ("ndugu.map", or without .map), from the store's map
index (the one a game being joined needs), as an entry of that one map */
int ce_fetch_map(const char *base, const char *file, ce_detail *out, char *error, int error_size);
/* its maps here? (MAP_INSTALLED, MAP_OUTDATED for some of them, MAP_MISSING) */
void ce_check(const char *game, ce_detail *detail);

/* the picture as dds from the store (640x360, as the store client's screenshots),
its padding cut off, as big as fits max_w x max_h (0xRRGGBB, the size in *w, *h):
1, or 0 with why */
int ce_fetch_picture(const char *base, long fid, const char *file, unsigned int *out, int max_w, int max_h, int *w,
	int *h, char *error, int error_size);

typedef void (*ce_progress_proc)(const char *file, unsigned long done, unsigned long total, void *context);
/* every ready map: downloaded, its blake2b the list's, into maps_ce */
int ce_install(const char *base, const char *game, ce_detail *detail, ce_progress_proc progress, void *context,
	char *error, int error_size);
int ce_remove(const char *game, ce_detail *detail, char *error, int error_size);

#endif
