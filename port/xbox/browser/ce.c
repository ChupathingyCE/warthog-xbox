/*
 * ce.h: the archive's json (/api/archive/halomaps/hce/files), each map from
 * the mirror (/api/mapindex/file/<blake2b>) checked as it comes
 */

#include "ce.h"
#include "core.h"
#include "blake2b.h"
#include "dxt.h"
#include "http.h"
#include "json.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CE_FOLDER "maps_ce"

/* b's stop (xbox_maps.c) */
extern volatile long xbox_maps_cancel;
// a page is tiny, a detail has the notes in it (a few k). 256k is loads
#define JSON_MAX (96 * 1024)
#define TOKENS_MAX 2048

// a map index lookup's answer is a single map (about 1.5k): a 64 MB console in a game's
// menus has no 128k to spare for a page's buffers
#define JSON_SMALL_MAX (16 * 1024)
#define TOKENS_SMALL_MAX 384

typedef struct
{
	char *text;
	int length;
	int size;
} json_buffer;

static int json_body(const char *data, int length, void *context)
{
	json_buffer *b = (json_buffer *)context;

	if (b->length + length >= b->size)
		return 0;
	memcpy(b->text + b->length, data, length);
	b->length += length;
	b->text[b->length] = 0;
	return 1;
}

/* GET url as json into d (the caller frees *text and *tokens) */
static int fetch_json_sized(const char *url, json_doc *d, char **text, jsmntok_t **tokens, int text_max,
	int tokens_max, char *error, int error_size)
{
	json_buffer b;
	int status;

	*text = (char *)malloc(text_max);
	*tokens = (jsmntok_t *)malloc(sizeof(jsmntok_t) * tokens_max);
	if (!*text || !*tokens)
	{
		snprintf(error, error_size, "out of memory");
		return 0;
	}
	b.text = *text;
	b.size = text_max;
	b.length = 0;
	b.text[0] = 0;
	status = http_get(url, NULL, json_body, &b, error, error_size);
	if (status != 200)
		return 0;
	if (json_parse(d, b.text, b.length, *tokens, tokens_max) < 1)
	{
		snprintf(error, error_size, "the server's answer wasn't json");
		return 0;
	}
	return 1;
}

static int fetch_json(const char *url, json_doc *d, char **text, jsmntok_t **tokens, char *error, int error_size)
{
	return fetch_json_sized(url, d, text, tokens, JSON_MAX, TOKENS_MAX, error, error_size);
}

// true / false / null as 1 / 0 / -1
static int fits_value(const json_doc *d, int i)
{
	if (i < 0 || d->t[i].type != JSMN_PRIMITIVE)
		return -1;
	return d->js[d->t[i].start] == 't' ? 1 : d->js[d->t[i].start] == 'f' ? 0 : -1;
}

static void url_encode(const char *s, char *out, int size);

int ce_fetch_page(const char *base, int section, const char *query, int offset, int count, ce_entry *out, int max,
	int *total, char *error, int error_size)
{
	char url[384], q[128], *text = NULL;
	jsmntok_t *tokens = NULL;
	json_doc d;
	int items, n = 0, i;

	// onlyMaps: entries w a .map on the mirror, in any section (a store from before it
	// takes onlyFiles: entries w anything to download). section 0: all of them
	// query: the store's search, of titles and authors ("" or NULL: none)
	url_encode(query ? query : "", q, sizeof(q));
	if (section > 0)
		snprintf(url, sizeof(url), "%s/api/archive/halomaps/hce/files?onlyMaps=1&onlyFiles=1&section=%d&sort=newest&offset=%d&count=%d%s%s",
			base, section, offset, count, q[0] ? "&q=" : "", q);
	else
		snprintf(url, sizeof(url), "%s/api/archive/halomaps/hce/files?onlyMaps=1&onlyFiles=1&sort=newest&offset=%d&count=%d%s%s",
			base, offset, count, q[0] ? "&q=" : "", q);
	if (!fetch_json(url, &d, &text, &tokens, error, error_size))
	{
		free(text);
		free(tokens);
		return -1;
	}
	*total = (int)json_long(&d, json_get(&d, 0, "total"));
	items = json_get(&d, 0, "items");
	for (i = 0; n < max; i++)
	{
		int item = json_item(&d, items, i);
		ce_entry *e = &out[n];

		if (item < 0)
			break;
		memset(e, 0, sizeof(*e));
		e->fid = json_long(&d, json_get(&d, item, "fid"));
		json_text(&d, json_get(&d, item, "title"), e->title, sizeof(e->title));
		json_text(&d, json_get(&d, item, "author"), e->author, sizeof(e->author));
		json_text(&d, json_get(&d, item, "date"), e->date, sizeof(e->date));
		json_text(&d, json_get(&d, item, "rating"), e->rating, sizeof(e->rating));
		json_text(&d, json_get(&d, item, "listedSize"), e->listed_size, sizeof(e->listed_size));
		e->maps = (int)json_long(&d, json_get(&d, item, "maps"));
		e->maps_here = (int)json_long(&d, json_get(&d, item, "mapsHere"));
		e->fits64 = fits_value(&d, json_get(&d, item, "fits64"));
		n++;
	}
	free(text);
	free(tokens);
	return n;
}

int ce_fetch_detail(const char *base, long fid, ce_detail *out, char *error, int error_size)
{
	char url[256], *text = NULL;
	jsmntok_t *tokens = NULL;
	json_doc d;
	int maps, i;

	snprintf(url, sizeof(url), "%s/api/archive/halomaps/hce/files/%ld", base, fid);
	memset(out, 0, sizeof(*out));
	if (!fetch_json(url, &d, &text, &tokens, error, error_size))
	{
		free(text);
		free(tokens);
		return 0;
	}
	out->fid = fid;
	json_text(&d, json_get(&d, 0, "title"), out->title, sizeof(out->title));
	json_text(&d, json_get(&d, 0, "author"), out->author, sizeof(out->author));
	json_text(&d, json_get(&d, 0, "date"), out->date, sizeof(out->date));
	json_text(&d, json_get(&d, 0, "category"), out->category, sizeof(out->category));
	json_text(&d, json_get(&d, 0, "description"), out->description, sizeof(out->description));
	// most authors on halomaps put their words in the file notes, not the description
	if (!out->description[0])
		json_text(&d, json_get(&d, 0, "notes"), out->description, sizeof(out->description));
	json_text(&d, json_get(&d, 0, "cover"), out->picture, sizeof(out->picture));
	maps = json_get(&d, 0, "maps");
	for (i = 0; out->map_count < CE_MAX_MAPS; i++)
	{
		int item = json_item(&d, maps, i);
		ce_map *m = &out->maps[out->map_count];

		if (item < 0)
			break;
		json_text(&d, json_get(&d, item, "file"), m->file, sizeof(m->file));
		m->size = (unsigned long)json_long(&d, json_get(&d, item, "size"));
		json_text(&d, json_get(&d, item, "blake2b"), m->blake2b, sizeof(m->blake2b));
		json_text(&d, json_get(&d, item, "checksum"), m->checksum, sizeof(m->checksum));
		m->ready = json_true(&d, json_get(&d, item, "ready"));
		json_text(&d, json_get(&d, item, "download"), m->download, sizeof(m->download));
		m->fits64 = fits_value(&d, json_get(&d, item, "fits64"));
		m->needs = (unsigned long)json_long(&d, json_get(&d, item, "needs"));
		// a name w a slash in it isnt a file name, skip it
		if (m->file[0] && !strpbrk(m->file, "\\/:"))
			out->map_count++;
	}
	free(text);
	free(tokens);
	return 1;
}

int ce_fetch_map(const char *base, const char *file, ce_detail *out, char *error, int error_size)
{
	char url[256], name[96], *text = NULL;
	jsmntok_t *tokens = NULL;
	json_doc d;
	ce_map *m = &out->maps[0];
	int fit, info;

	memset(out, 0, sizeof(*out));
	url_encode(file, name, sizeof(name));
	snprintf(url, sizeof(url), "%s/api/mapindex/maps?name=%s", base, name);
	if (!fetch_json_sized(url, &d, &text, &tokens, JSON_SMALL_MAX, TOKENS_SMALL_MAX, error, error_size))
	{
		free(text);
		free(tokens);
		return 0;
	}
	json_text(&d, json_get(&d, 0, "file"), m->file, sizeof(m->file));
	m->size = (unsigned long)json_long(&d, json_get(&d, 0, "size"));
	json_text(&d, json_get(&d, 0, "blake2b"), m->blake2b, sizeof(m->blake2b));
	json_text(&d, json_get(&d, 0, "checksum"), m->checksum, sizeof(m->checksum));
	// (the map index's: a copy on the mirror to download)
	m->ready = json_true(&d, json_get(&d, 0, "seededHere")) == 1;
	json_text(&d, json_get(&d, 0, "download"), m->download, sizeof(m->download));
	fit = json_get(&d, 0, "fit");
	m->fits64 = fit >= 0 ? fits_value(&d, json_get(&d, fit, "fits64")) : -1;
	m->needs = fit >= 0 ? (unsigned long)json_long(&d, json_get(&d, fit, "needs")) : 0;
	info = json_get(&d, 0, "info");
	if (info >= 0)
	{
		out->fid = json_long(&d, json_get(&d, info, "fid"));
		json_text(&d, json_get(&d, info, "title"), out->title, sizeof(out->title));
		json_text(&d, json_get(&d, info, "author"), out->author, sizeof(out->author));
	}
	free(text);
	free(tokens);
	if (!m->file[0] || strpbrk(m->file, "\\/:") || strlen(m->blake2b) != 64)
	{
		snprintf(error, error_size, "the store's answer for %s isn't a map", file);
		return 0;
	}
	out->map_count = 1;
	return 1;
}

// a 1024x1024 dxt1 is 512k and its header, a bit of room on top
#define PICTURE_MAX (600 * 1024)

typedef struct
{
	unsigned char *data;
	unsigned long length;
} picture_buffer;

static int picture_body(const char *data, int length, void *context)
{
	picture_buffer *b = (picture_buffer *)context;

	if (b->length + length > PICTURE_MAX)
		return 0;
	memcpy(b->data + b->length, data, length);
	b->length += length;
	return 1;
}

// a file name in a url: anything but letters, digits and - _ . ~ as %xx
static void url_encode(const char *s, char *out, int size)
{
	int o = 0;

	for (; *s && o < size - 4; s++)
	{
		unsigned char c = (unsigned char)*s;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c))
			out[o++] = (char)c;
		else
			o += snprintf(out + o, size - o, "%%%02X", c);
	}
	out[o] = 0;
}

int ce_fetch_picture(const char *base, long fid, const char *file, unsigned int *out, int max_w, int max_h, int *w,
	int *h, char *error, int error_size)
{
	char url[384], name[200];
	picture_buffer b;
	int status, ok;

	if (!file[0])
	{
		snprintf(error, error_size, "no picture");
		return 0;
	}
	b.data = (unsigned char *)malloc(PICTURE_MAX);
	b.length = 0;
	if (!b.data)
	{
		snprintf(error, error_size, "out of memory");
		return 0;
	}
	url_encode(file, name, sizeof(name));
	// the store's fixed sizes: 640x360 is the client's screenshot one
	snprintf(url, sizeof(url), "%s/api/archive/halomaps/hce/files/%ld/screenshots/%s?width=640&height=360&format=dds",
		base, fid, name);
	status = http_get(url, NULL, picture_body, &b, error, error_size);
	ok = status == 200 && dxt_picture(b.data, b.length, (uint32_t *)out, max_w, max_h, w, h);
	if (status == 200 && !ok)
		snprintf(error, error_size, "the picture wasn't a dxt1 dds");
	free(b.data);
	return ok;
}

static void map_path(const char *game, const ce_map *m, char *out, int size)
{
	char folder[512];

	char name[64];
	int from, to = 0;

	/* the xbox's disk takes only these in a name, and 42 of them (=ac=timberland.map:
	actimberland.map). the store's names are halo pc's */
	for (from = 0; m->file[from] && to < (int)sizeof(name) - 1; from++)
	{
		char c = m->file[from];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" !#$%&'()-.@[]^_`{}~", c))
			name[to++] = c;
	}
	name[to] = 0;
	if (to > 42)
	{
		memmove(name + 38, name + to - 4, 5);
		to = 42;
	}
	core_join(folder, sizeof(folder), game, CE_FOLDER);
	core_join(out, size, folder, name);
}

static unsigned long file_size(const char *path)
{
	FILE *f = fopen(path, "rb");
	long size;

	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fclose(f);
	return size < 0 ? 0 : (unsigned long)size;
}

void ce_check(const char *game, ce_detail *detail)
{
	int i, ready = 0, here = 0;

	// by name n size, cheap enough to do for every row. the hash gets checked when it downloads
	for (i = 0; i < detail->map_count; i++)
	{
		char path[512];

		if (!detail->maps[i].ready)
			continue;
		ready++;
		map_path(game, &detail->maps[i], path, sizeof(path));
		if (plat_is_file(path) && file_size(path) == detail->maps[i].size)
			here++;
	}
	detail->state = !here ? MAP_MISSING : here == ready ? MAP_INSTALLED : MAP_OUTDATED;
}

typedef struct
{
	FILE *file;
	blake2b_ctx hash;
	unsigned long done, total;
	const char *name;
	ce_progress_proc progress;
	void *context;
} ce_download;

static void download_size(unsigned long size, void *context)
{
	((ce_download *)context)->total = size;
}

static int download_body(const char *data, int length, void *context)
{
	ce_download *d = (ce_download *)context;

	if (xbox_maps_cancel)
		return 0;
	if (fwrite(data, 1, length, d->file) != (size_t)length)
		return 0;
	// hash it as it comes, no second pass over a 300mb map
	blake2b_update(&d->hash, data, length);
	d->done += length;
	if (d->progress)
		d->progress(d->name, d->done, d->total, d->context);
	return 1;
}

int ce_install(const char *base, const char *game, ce_detail *detail, ce_progress_proc progress, void *context,
	char *error, int error_size)
{
	char folder[512];
	int i, installed = 0;

	core_join(folder, sizeof(folder), game, CE_FOLDER);
	plat_mkdir(folder);
	for (i = 0; i < detail->map_count; i++)
	{
		ce_map *m = &detail->maps[i];
		char path[512], url[384], hex[65];
		unsigned char digest[32];
		ce_download d;
		int status;

		if (!m->ready || !m->download[0])
			continue;
		map_path(game, m, path, sizeof(path));
		/* (the file over plain http: the hash it must have came over https) */
		http_plain_url(url, sizeof(url), base, m->download);
		d.file = fopen(path, "wb");
		if (!d.file)
		{
			snprintf(error, error_size, "can't write %s", path);
			return 0;
		}
		blake2b_init(&d.hash, 32);
		d.done = d.total = 0;
		d.name = m->file;
		d.progress = progress;
		d.context = context;
		status = http_get(url, download_size, download_body, &d, error, error_size);
		fclose(d.file);
		if (status != 200)
		{
			plat_remove(path);
			return 0;
		}
		// the important bit: the whole file has to be the signed lists blake2b, else its gone
		blake2b_final(&d.hash, digest);
		blake2b_hex(digest, 32, hex);
		if (strcmp(hex, m->blake2b) || d.done != m->size)
		{
			snprintf(error, error_size, "%s isn't the list's file (blake2b %.16s...): deleted", m->file, hex);
			plat_remove(path);
			return 0;
		}
		installed++;
	}
	if (!installed)
	{
		snprintf(error, error_size, "none of its maps are on the mirror yet");
		return 0;
	}
	ce_check(game, detail);
	return 1;
}

int ce_remove(const char *game, ce_detail *detail, char *error, int error_size)
{
	char folder[512];
	int i;

	for (i = 0; i < detail->map_count; i++)
	{
		char path[512];

		map_path(game, &detail->maps[i], path, sizeof(path));
		if (plat_is_file(path) && !plat_remove(path))
		{
			snprintf(error, error_size, "can't delete %s", path);
			return 0;
		}
	}
	// the folder too once its empty (rmdir fails if it isnt, thats fine)
	core_join(folder, sizeof(folder), game, CE_FOLDER);
	plat_rmdir(folder);
	ce_check(game, detail);
	return 1;
}
