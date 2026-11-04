/*
 * a little over jsmn (jsmn.h, MIT): find a key in an object, a value as text
 * or a number, an array's items. the archive api answers in json
 */

#ifndef JSON_H
#define JSON_H

#define JSMN_HEADER
#include "jsmn.h"

typedef struct
{
	const char *js;
	jsmntok_t *t;
	int n;
} json_doc;

/* the tokens of js: their count, or < 0 if it isn't json (or has more than max) */
int json_parse(json_doc *d, const char *js, int length, jsmntok_t *tokens, int max);
/* the token after i and everything inside it */
int json_next(const json_doc *d, int i);
/* the value of key in the object at i: its token, or -1 */
int json_get(const json_doc *d, int i, const char *key);
/* the item'th item of the array at i: its token, or -1 */
int json_item(const json_doc *d, int i, int item);
/* a string unescaped (ascii only: others as '?'), a number or true/false as written */
void json_text(const json_doc *d, int i, char *out, int size);
long json_long(const json_doc *d, int i);
int json_true(const json_doc *d, int i);

#endif
