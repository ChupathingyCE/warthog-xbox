/*
 * core.h: the list, warthog's fingerprint, installing and removing
 */

#include "core.h"
#include "http.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* warthog's (chupathingyce port/linux/game/map_packs.c) */
// these 4 have to match warthog exactly or the fingerprints wont line up. dont touch
/* b's stop (xbox_maps.c) */
extern volatile long xbox_maps_cancel;

#define FINGERPRINT_SAMPLES 16
#define FINGERPRINT_SAMPLE_SIZE 0x10000
#define FINGERPRINT_CHUNK 2048
#define MAPLIST_SIZE 8192

/* ---------- the list */

typedef struct
{
	char *text;
	int length, size;
} grow_buffer;

// grows the buffer as the list comes in. lists are tiny, a meg cap is overkill tbh
static int grow_body(const char *data, int length, void *context)
{
	grow_buffer *b = (grow_buffer *)context;

	if (b->length + length + 1 > b->size)
	{
		int size = (b->length + length + 1) * 2;
		char *text;

		if (size > 1024 * 1024)
			return 0; /* (a list past a megabyte isn't one) */
		text = (char *)realloc(b->text, size);
		if (!text)
			return 0;
		b->text = text;
		b->size = size;
	}
	memcpy(b->text + b->length, data, length);
	b->length += length;
	b->text[b->length] = 0;
	return 1;
}

/* the next tab-separated field of *line, into out */
// one tab seperated field off the front of the line. moves the line along
static void field(char **line, char *out, int size)
{
	char *tab = strchr(*line, '\t');
	int length = tab ? (int)(tab - *line) : (int)strlen(*line);

	snprintf(out, size, "%.*s", length, *line);
	*line = tab ? tab + 1 : *line + length;
}

// grab maps.txt from the store, test=1 so the xhbtest pack shows up too
// returns how many maps or -1 w the error filled in
int core_fetch_list(const char *base, map_entry *maps, int max, char *error, int error_size)
{
	char url[256], number[24];
	grow_buffer b = { NULL, 0, 0 };
	int status, count = 0;
	char *line, *next;

	snprintf(url, sizeof(url), "%s/api/warthog/maps.txt?test=1", base);
	status = http_get(url, NULL, grow_body, &b, error, error_size);
	if (status != 200)
	{
		free(b.text);
		return -1;
	}
	/* pack, file, name, type, fingerprint, size, download, version, item */
	for (line = b.text; line && *line && count < max; line = next)
	{
		map_entry *m = &maps[count];

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		// comments n blank lines, skip em
		if (line[0] == '#' || !line[0])
			continue;
		memset(m, 0, sizeof(*m));
		field(&line, m->pack, sizeof(m->pack));
		field(&line, m->file, sizeof(m->file));
		field(&line, m->name, sizeof(m->name));
		field(&line, m->type, sizeof(m->type));
		field(&line, number, sizeof(number));
		m->fingerprint = strtoul(number, NULL, 16);
		field(&line, number, sizeof(number));
		m->size = strtoul(number, NULL, 10);
		field(&line, m->download, sizeof(m->download));
		field(&line, m->version, sizeof(m->version));
		field(&line, m->item, sizeof(m->item));
		if (m->pack[0] && m->file[0] && m->download[0])
			count++;
	}
	free(b.text);
	return count;
}

/* ---------- the fingerprint */

// plain fnv1a. the & 0xFFFFFFFF is so it stays 32 bit on 64 bit linux too, the xbox doesnt care
static unsigned long fnv1a(unsigned long value, const unsigned char *bytes, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++)
		value = ((value ^ bytes[i]) * 16777619UL) & 0xFFFFFFFFUL;
	return value;
}

// warthogs map_pack_fingerprint, ported straight across
// size first (4 bytes little endian) then 16 samples spread over the file, 64k max each
// same as the server computes so if these match the file is the one the list means
unsigned long core_fingerprint(const char *path)
{
	unsigned char bytes[FINGERPRINT_CHUNK];
	unsigned long value = 2166136261UL, size;
	long sample;
	FILE *f = fopen(path, "rb");

	if (!f)
		return 0;
	if (fseek(f, 0, SEEK_END) || (long)(size = (unsigned long)ftell(f)) < 0)
	{
		fclose(f);
		return 0;
	}
	bytes[0] = (unsigned char)(size & 0xFF);
	bytes[1] = (unsigned char)((size >> 8) & 0xFF);
	bytes[2] = (unsigned char)((size >> 16) & 0xFF);
	bytes[3] = (unsigned char)((size >> 24) & 0xFF);
	value = fnv1a(value, bytes, 4);
	// the 16 samples. size / 16 * i, in that order on purpose (integer math, matches the c on the console)
	for (sample = 0; sample < FINGERPRINT_SAMPLES; sample++)
	{
		unsigned long offset = size / FINGERPRINT_SAMPLES * (unsigned long)sample;
		unsigned long left = size - offset < FINGERPRINT_SAMPLE_SIZE ? size - offset : FINGERPRINT_SAMPLE_SIZE;

		if (fseek(f, (long)offset, SEEK_SET))
		{
			fclose(f);
			return 0;
		}
		while (left)
		{
			unsigned long count = left < FINGERPRINT_CHUNK ? left : FINGERPRINT_CHUNK;

			if (fread(bytes, 1, count, f) != count)
			{
				fclose(f);
				return 0;
			}
			value = fnv1a(value, bytes, count);
			left -= count;
		}
	}
	fclose(f);
	// 0 means "any version" in a game record so warthog never hands out 0. neither do we
	return value ? value : 1;
}

/* ---------- warthog's folder */

// path + name w the right slash for the platform. backslash on xbox
static void join(char *out, int size, const char *a, const char *b)
{
	size_t n = strlen(a);

	snprintf(out, size, "%s%s%s", a, n && a[n - 1] != PLAT_SEP ? (char[]){ PLAT_SEP, 0 } : "", b);
}

// is this folder warthog? default.xbe + config.toml + a maps folder
// a disc rip of retail halo has no config.toml so it doesnt get picked by mistake
void core_join(char *out, int size, const char *a, const char *b)
{
	join(out, size, a, b);
}

static int is_warthog(const char *dir)
{
	char path[512];

	join(path, sizeof(path), dir, "default.xbe");
	if (!plat_is_file(path))
		return 0;
	/* xbox_package.py lays it out with a config.toml; a disc copy of halo has none */
	join(path, sizeof(path), dir, "config.toml");
	if (!plat_is_file(path))
		return 0;
	join(path, sizeof(path), dir, "maps");
	return plat_is_dir(path);
}

typedef struct
{
	const char *parent;
	char *out;
	int size, depth, found;
} find_context;

// called for each thing in a folder while looking for warthog
static int find_in(const char *name, int is_dir, void *context)
{
	find_context *c = (find_context *)context;
	char dir[512];

	if (!is_dir)
		return 1;
	join(dir, sizeof(dir), c->parent, name);
	if (is_warthog(dir))
	{
		snprintf(c->out, c->size, "%s", dir);
		c->found = 1;
		return 0;
	}
	// only one level deeper, E:\Games\Warthog style. dont go crawling the whole drive
	if (c->depth < 1)
	{
		/* (one level more: E:\Games\Warthog) */
		find_context inner = { dir, c->out, c->size, c->depth + 1, 0 };
		char xbe[512];

		join(xbe, sizeof(xbe), dir, "default.xbe");
		if (plat_is_file(xbe))
			return 1; /* (not inside another game's folders) */
		plat_list(dir, find_in, &inner);
		if (inner.found)
		{
			c->found = 1;
			return 0;
		}
	}
	return 1;
}

// look on every root for warthog, first one wins
// none found = the test folder so it still works on a blank xemu hdd
int core_find_warthog(char *out, int size)
{
	const char *const *roots = plat_roots();
	int i;

	for (i = 0; roots[i]; i++)
	{
		find_context c = { roots[i], out, size, 0, 0 };

		plat_list(roots[i], find_in, &c);
		if (c.found)
			return 1;
	}
	snprintf(out, size, "%s", plat_fallback_dir());
	plat_mkdir(out);
	return 0;
}

/* ---------- a map here */

// where a map lives: <game>\maps_<pack>\<file>.map
void core_map_path(const char *game, const map_entry *map, char *out, int size)
{
	char folder[512], name[64];

	snprintf(name, sizeof(name), "maps_%s", map->pack);
	join(folder, sizeof(folder), game, name);
	snprintf(name, sizeof(name), "%s.map", map->file);
	join(out, size, folder, name);
}

// installed / missing / outdated, by fingerprint. outdated = same name diffrent file
void core_check(const char *game, map_entry *map)
{
	char path[512];

	core_map_path(game, map, path, sizeof(path));
	map->local_fingerprint = plat_is_file(path) ? core_fingerprint(path) : 0;
	map->state = !map->local_fingerprint ? MAP_MISSING
		: map->local_fingerprint == map->fingerprint ? MAP_INSTALLED : MAP_OUTDATED;
}

/* (strcasecmp isn't in every libc) */
// case insensitive compare cuz fatx doesnt care about case and neither does warthog
static int same_name(const char *a, const char *b)
{
	while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

/* maplist.txt: this map's line set to name (or taken out when name is NULL),
everyone else's lines as they were */
// rewrites maplist.txt w our line in (or out). keeps everybody elses lines alone
// warthog reads it as "file - Name" one a line, \r\n endings cuz thats what the packs ship with
static int maplist_update(const char *folder, const char *file, const char *name)
{
	char path[512], *text, *out, *line, *next;
	int length = 0, out_length = 0, done = 0;
	FILE *f;

	join(path, sizeof(path), folder, "maplist.txt");
	text = (char *)calloc(1, MAPLIST_SIZE + 1);
	out = (char *)calloc(1, MAPLIST_SIZE * 2 + 256);
	if (!text || !out)
	{
		free(text);
		free(out);
		return 0;
	}
	f = fopen(path, "rb");
	if (f)
	{
		length = (int)fread(text, 1, MAPLIST_SIZE, f);
		fclose(f);
	}
	text[length] = 0;
	for (line = text; *line; line = next)
	{
		char base[64];
		const char *dash;
		int n;

		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		else
			next = line + strlen(line);
		n = (int)strlen(line);
		if (n && line[n - 1] == '\r')
			line[--n] = 0;
		if (!n)
			continue;
		/* "file - Name": the first '-' splits them, spaces around each dropped */
		dash = strchr(line, '-');
		snprintf(base, sizeof(base), "%.*s", dash ? (int)(dash - line) : n, line);
		for (n = (int)strlen(base); n && base[n - 1] == ' '; n--)
			base[n - 1] = 0;
		// our line. swap the name in, or drop it when removing
		if (same_name(base, file))
		{
			if (name && !done)
				out_length += sprintf(out + out_length, "%s - %s\r\n", file, name);
			done = 1;
			continue;
		}
		out_length += sprintf(out + out_length, "%s\r\n", line);
	}
	if (name && !done)
		out_length += sprintf(out + out_length, "%s - %s\r\n", file, name);
	free(text);
	// warthog only reads 8k of it. bail instead of writing one it cant read all of
	if (out_length > MAPLIST_SIZE - 1)
	{
		free(out);
		return 0;
	}
	if (!out_length)
	{
		free(out);
		plat_remove(path);
		return 1;
	}
	f = fopen(path, "wb");
	if (!f)
	{
		free(out);
		return 0;
	}
	length = (int)fwrite(out, 1, out_length, f);
	fclose(f);
	free(out);
	return length == out_length;
}

typedef struct
{
	FILE *file;
	unsigned long done, total;
	core_progress_proc progress;
	void *context;
} download_context;

// the http side tells us the size once it knows, for the progress bar
static void download_size(unsigned long size, void *context)
{
	((download_context *)context)->total = size;
}

// each chunk off the wire straight to the file. never holds the whole map in memory, 64mb box
static int download_body(const char *data, int length, void *context)
{
	download_context *d = (download_context *)context;

	if (xbox_maps_cancel)
		return 0;
	if (fwrite(data, 1, length, d->file) != (size_t)length)
		return 0;
	d->done += length;
	if (d->progress)
		d->progress(d->done, d->total, d->context);
	return 1;
}

// download, check the fingerprint, then maplist.txt. in that order
// a bad download gets deleted so theres never a half map sitting in maps_<pack>
int core_install(const char *base, const char *game, map_entry *map, core_progress_proc progress, void *context,
	char *error, int error_size)
{
	char folder[512], path[512], url[384], name[64];
	download_context d;
	int status;

	snprintf(name, sizeof(name), "maps_%s", map->pack);
	join(folder, sizeof(folder), game, name);
	plat_mkdir(folder);
	core_map_path(game, map, path, sizeof(path));
	/* (the file over plain http: its fingerprint's list came over https) */
	http_plain_url(url, sizeof(url), base, map->download);

	d.file = fopen(path, "wb");
	if (!d.file)
	{
		snprintf(error, error_size, "can't write %s", path);
		return 0;
	}
	d.done = d.total = 0;
	d.progress = progress;
	d.context = context;
	status = http_get(url, download_size, download_body, &d, error, error_size);
	fclose(d.file);
	if (status != 200)
	{
		plat_remove(path);
		return 0;
	}

	/* the file must be the list's, by warthog's own fingerprint */
	// the important bit. if this doesnt match the list the file is junk, delete it
	core_check(game, map);
	if (map->state != MAP_INSTALLED)
	{
		snprintf(error, error_size, "fingerprint %08lx, the list says %08lx: deleted", map->local_fingerprint,
			map->fingerprint);
		plat_remove(path);
		map->state = MAP_MISSING;
		return 0;
	}
	if (map->name[0] && !maplist_update(folder, map->file, map->name))
	{
		snprintf(error, error_size, "installed, but maplist.txt couldn't be written");
		return 0;
	}
	return 1;
}

// just counts whats in a folder
static int count_entry(const char *name, int is_dir, void *context)
{
	(void)name;
	(void)is_dir;
	(*(int *)context)++;
	return 1;
}

// takes out the map, its maplist line, and the folder if nothing else is left in it
// doesnt touch anything else in there, other packs or the players own maps
int core_remove(const char *game, map_entry *map, char *error, int error_size)
{
	char folder[512], path[512], name[64];
	int left = 0;

	snprintf(name, sizeof(name), "maps_%s", map->pack);
	join(folder, sizeof(folder), game, name);
	core_map_path(game, map, path, sizeof(path));
	if (plat_is_file(path) && !plat_remove(path))
	{
		snprintf(error, error_size, "can't delete %s", path);
		return 0;
	}
	maplist_update(folder, map->file, NULL);
	plat_list(folder, count_entry, &left);
	if (!left)
		plat_rmdir(folder);
	map->state = MAP_MISSING;
	map->local_fingerprint = 0;
	return 1;
}
