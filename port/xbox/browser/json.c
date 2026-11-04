/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * json.h over jsmn (its code is built here, once)
 */

#undef JSMN_HEADER
#include "jsmn.h"
#define JSMN_HEADER
#include "json.h"

#include <stdlib.h>
#include <string.h>

int json_parse(json_doc *d, const char *js, int length, jsmntok_t *tokens, int max)
{
	jsmn_parser p;

	jsmn_init(&p);
	d->js = js;
	d->t = tokens;
	d->n = jsmn_parse(&p, js, (size_t)length, tokens, (unsigned int)max);
	return d->n;
}

int json_next(const json_doc *d, int i)
{
	int children, k;

	if (i < 0 || i >= d->n)
		return d->n;
	children = d->t[i].size;
	/* an object's size counts its keys: each key has its value inside it */
	i++;
	for (k = 0; k < children && i < d->n; k++)
		i = json_next(d, i);
	return i;
}

static int token_is(const json_doc *d, int i, const char *s)
{
	int length = d->t[i].end - d->t[i].start;

	return d->t[i].type == JSMN_STRING && (int)strlen(s) == length && !strncmp(d->js + d->t[i].start, s, length);
}

int json_get(const json_doc *d, int i, const char *key)
{
	int k, pairs, at;

	if (i < 0 || i >= d->n || d->t[i].type != JSMN_OBJECT)
		return -1;
	pairs = d->t[i].size;
	at = i + 1;
	for (k = 0; k < pairs && at < d->n; k++)
	{
		if (token_is(d, at, key))
			return at + 1 < d->n ? at + 1 : -1;
		at = json_next(d, at + 1);
	}
	return -1;
}

int json_item(const json_doc *d, int i, int item)
{
	int k, at;

	if (i < 0 || i >= d->n || d->t[i].type != JSMN_ARRAY || item >= d->t[i].size)
		return -1;
	at = i + 1;
	for (k = 0; k < item; k++)
		at = json_next(d, at);
	return at < d->n ? at : -1;
}

void json_text(const json_doc *d, int i, char *out, int size)
{
	const char *s, *end;
	int o = 0;

	if (size <= 0)
		return;
	out[0] = 0;
	if (i < 0 || i >= d->n)
		return;
	s = d->js + d->t[i].start;
	end = d->js + d->t[i].end;
	if (d->t[i].type == JSMN_PRIMITIVE && !strncmp(s, "null", 4))
		return;
	while (s < end && o < size - 1)
	{
		char c = *s++;

		if (c == '\\' && s < end)
		{
			char e = *s++;

			switch (e)
			{
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'b': case 'f': c = ' '; break;
			case 'u':
				/* (the font is ascii: past it, a ?) */
				{
					char hex[5] = { 0 };
					long code;

					strncpy(hex, s, end - s < 4 ? (size_t)(end - s) : 4);
					s += strlen(hex);
					code = strtol(hex, NULL, 16);
					c = code >= 32 && code < 127 ? (char)code : '?';
				}
				break;
			default: c = e; break;
			}
		}
		else if ((unsigned char)c >= 0x80)
		{
			/* (a utf-8 sequence: one ? for all of it) */
			while (s < end && ((unsigned char)*s & 0xC0) == 0x80)
				s++;
			c = '?';
		}
		out[o++] = c;
	}
	out[o] = 0;
}

long json_long(const json_doc *d, int i)
{
	char text[32];

	json_text(d, i, text, sizeof(text));
	return strtol(text, NULL, 10);
}

int json_true(const json_doc *d, int i)
{
	return i >= 0 && i < d->n && d->t[i].type == JSMN_PRIMITIVE && d->js[d->t[i].start] == 't';
}
