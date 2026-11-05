/*
GAME_LIST_TEST.C

The console's game list parsers (port/xbox/src/xbox_game_list_parse.c) on
the host: known responses and lists, every truncation of them, and a
mutation fuzzer, each input in a heap block of its own exact size so
AddressSanitizer catches a read one byte past it. port/xbox/tests/run.sh.

    game_list_test [iterations]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_game_list.h"

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); } } while (0)

#define INVITE_A "368ac913df9298fdac8b2a2835735a0025f933cc8287957d6a5bcd4387dfc25d"
#define INVITE_B "ff247f02a545e9b1efd26f61cee3f44b250b3ce20e9299776cf470a7f66a84be"

static const char good_list[] =
	"warthog-list 1 2\n"
	INVITE_A "\tClassic CTF\tbloodgulch\t1\tCTF\t61\t128\t11\t\tplaying\n"
	INVITE_B "\t[D] Bloodgulch\tlevels\\test\\bloodgulch\\bloodgulch\t2\tslayer\t0\t16\t11\tNA-CA\topen\n"
	"end\n";

/* the input in a block of its exact size */
static char *exact(const char *data, size_t size)
{
	char *copy = malloc(size ? size : 1);

	if (!copy)
		abort();
	memcpy(copy, data, size);
	return copy;
}

static int parse_list(const char *data, size_t size, struct game_list *list)
{
	char *copy = exact(data, size);
	int result = game_list_parse(copy, (unsigned long)size, list);

	free(copy);
	return result;
}

/* a response parsed, and its body as a list: the list's result, or the
response's error */
static int parse_all(const char *data, size_t size, struct game_list *list, struct game_list_response *response)
{
	char *copy = exact(data, size);
	int result = game_list_parse_response(copy, (unsigned long)size, response);

	memset(list, 0, sizeof(*list));
	if (result == GAME_LIST_OK)
	{
		/* (the body within the input) */
		CHECK(response->body >= copy && response->body + response->body_size <= copy + size);
		result = game_list_parse(response->body, response->body_size, list);
	}
	free(copy);
	return result;
}

static int terminated(const char *text, size_t size)
{
	return memchr(text, 0, size) != NULL;
}

/* what holds for any list, whatever went in */
static void check_invariants(const struct game_list *list, int result)
{
	int index;

	CHECK(list->count >= 0 && list->count <= GAME_LIST_MAXIMUM_GAMES);
	if (result != GAME_LIST_OK)
		CHECK(list->count == 0);
	for (index = 0; index < list->count; index++)
	{
		const struct game_list_game *game = &list->games[index];
		size_t length;
		const char *c;

		CHECK(terminated(game->invite, sizeof(game->invite)));
		length = strlen(game->invite);
		CHECK(length == 64 || length == 44);
		CHECK(terminated(game->name, sizeof(game->name)) && game->name[0]);
		CHECK(terminated(game->map, sizeof(game->map)) && game->map[0]);
		CHECK(terminated(game->gametype, sizeof(game->gametype)));
		CHECK(terminated(game->region, sizeof(game->region)));
		CHECK(terminated(game->state, sizeof(game->state)) && game->state[0]);
		for (c = game->name; *c; c++)
			CHECK(*c >= 0x20 && *c <= 0x7e);
		for (c = game->map; *c; c++)
			CHECK(*c >= 0x20 && *c <= 0x7e);
		CHECK(game->engine >= 0 && game->engine <= 15);
		CHECK(game->players >= 0 && game->players <= 255);
		CHECK(game->maximum_players >= 0 && game->maximum_players <= 255);
	}
}

static char *response_of(const char *head, const char *body, size_t *size)
{
	size_t head_size = strlen(head), body_size = strlen(body);
	char *data = malloc(head_size + body_size + 1);

	memcpy(data, head, head_size);
	memcpy(data + head_size, body, body_size + 1);
	*size = head_size + body_size;
	return data;
}

/* ---------- known inputs */

static void test_list(void)
{
	struct game_list list;
	char text[4096];
	int index;

	CHECK(parse_list(good_list, strlen(good_list), &list) == GAME_LIST_OK);
	CHECK(list.format == 1 && list.count == 2 && list.skipped == 0);
	CHECK(!strcmp(list.games[0].invite, INVITE_A));
	CHECK(!strcmp(list.games[0].name, "Classic CTF"));
	CHECK(!strcmp(list.games[0].gametype, "CTF"));
	CHECK(list.games[0].engine == 1 && list.games[0].players == 61 && list.games[0].maximum_players == 128);
	CHECK(list.games[0].version == 11 && !list.games[0].region[0] && !strcmp(list.games[0].state, "playing"));
	CHECK(!strcmp(list.games[1].map, "levels\\test\\bloodgulch\\bloodgulch"));
	CHECK(!strcmp(list.games[1].region, "NA-CA") && !strcmp(list.games[1].state, "open"));

	/* empty; "end" without its line break; CR LF lines */
	CHECK(parse_list("warthog-list 1 0\nend\n", 21, &list) == GAME_LIST_OK && list.count == 0);
	CHECK(parse_list("warthog-list 1 0\nend", 20, &list) == GAME_LIST_OK);
	CHECK(parse_list("warthog-list 1 0\r\nend\r\n", 23, &list) == GAME_LIST_OK);

	/* every cut short of the good list fails, and leaves nothing */
	for (index = 0; index < (int)strlen(good_list) - 1; index++)
	{
		int result = parse_list(good_list, (size_t)index, &list);

		/* ("...\nend" without its last line break is whole) */
		if (index == (int)strlen(good_list) - 1)
			continue;
		CHECK(result != GAME_LIST_OK);
		CHECK(list.count == 0);
	}

	/* the header */
	CHECK(parse_list("warthog-list 0 0\nend\n", 21, &list) == GAME_LIST_ERROR_FORMAT);
	CHECK(parse_list("halo-list 1 0\nend\n", 18, &list) == GAME_LIST_ERROR_LIST_HEADER);
	CHECK(parse_list("warthog-list 1\nend\n", 19, &list) == GAME_LIST_ERROR_LIST_HEADER);
	CHECK(parse_list("warthog-list 1 -1\nend\n", 22, &list) == GAME_LIST_ERROR_LIST_HEADER);
	CHECK(parse_list("warthog-list 1 99999\nend\n", 25, &list) == GAME_LIST_ERROR_LIST_HEADER);
	CHECK(parse_list("warthog-list 1 65\nend\n", 22, &list) == GAME_LIST_ERROR_COUNT);
	CHECK(parse_list("warthog-list 1 1 \nend\n", 22, &list) == GAME_LIST_ERROR_LIST_HEADER);
	CHECK(parse_list("", 0, &list) == GAME_LIST_ERROR_LIST_TRUNCATED);

	/* the count must match the lines */
	CHECK(parse_list("warthog-list 1 1\nend\n", 21, &list) == GAME_LIST_ERROR_COUNT);
	snprintf(text, sizeof(text), "warthog-list 1 1\n%s", good_list + 17);
	CHECK(parse_list(text, strlen(text), &list) == GAME_LIST_ERROR_COUNT && list.count == 0);

	/* a later format: fields after the tenth left alone */
	snprintf(text, sizeof(text), "warthog-list 2 1\n" INVITE_A "\tA\tb\t2\tslayer\t1\t16\t11\tEU-DE\topen\tnew\tmore\nend\n");
	CHECK(parse_list(text, strlen(text), &list) == GAME_LIST_OK && list.count == 1 && list.format == 2);
	CHECK(!strcmp(list.games[0].state, "open"));

	/* bad lines are left out, the rest kept */
	snprintf(text, sizeof(text), "warthog-list 1 8\n"
		"zz" INVITE_A "\tA\tb\t2\tslayer\t1\t16\t11\t\topen\n"            /* invite too long */
		"%.63s\tA\tb\t2\tslayer\t1\t16\t11\t\topen\n"                    /* too short */
		"%.62sxz\tA\tb\t2\tslayer\t1\t16\t11\t\topen\n"                  /* not hex */
		INVITE_A "\t\tb\t2\tslayer\t1\t16\t11\t\topen\n"                  /* no name */
		INVITE_A "\tA\tb\t2\tslayer\t256\t16\t11\t\topen\n"               /* players */
		INVITE_A "\tA\tb\t2\tslayer\t1\t16\t11\t\n"                       /* no state */
		INVITE_A "\tA\tb\t2\tslayer\t1\t16\t11\topen\n"                   /* a field missing */
		INVITE_A "\tA\xc3\xa9\x01\tb\t2\tslayer\t1\t16\t65535\t\tfull\n"  /* kept, plain letters */
		"end\n", INVITE_A, INVITE_A);
	CHECK(parse_list(text, strlen(text), &list) == GAME_LIST_OK);
	CHECK(list.count == 1 && list.skipped == 7);
	CHECK(!strcmp(list.games[0].name, "Ae") && list.games[0].version == 65535);

	/* names in UTF-8: accented Latin letters plain, the rest left out, a
	name of nothing else "?"; a name may take more bytes than characters */
	snprintf(text, sizeof(text), "warthog-list 1 4\n"
		INVITE_A "\t\xc2\xa1R\xc3\xado \xc5\x81\xc3\xb3" "d\xc5\xba!\xe2\x84\xa2\tb\t2\t\t1\t16\t11\t\topen\n"
		INVITE_A "\t\xe6\x97\xa5\xe6\x9c\xac\tb\t2\t\t1\t16\t11\t\topen\n"
		INVITE_A "\t\xc3\x86on \xc3\x9f\xf0\x9f\x98\x80\xc3\tb\t2\t\t1\t16\t11\t\topen\n"
		INVITE_A "\t\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
			"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
			"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
			"\xc3\xa9\xc3\xa9\tb\t2\t\t1\t16\t11\t\topen\n"
		"end\n");
	CHECK(parse_list(text, strlen(text), &list) == GAME_LIST_OK && list.count == 4);
	CHECK(!strcmp(list.games[0].name, "!Rio Lodz!"));
	CHECK(!strcmp(list.games[1].name, "?"));
	CHECK(!strcmp(list.games[2].name, "AEon ss"));
	CHECK(!strcmp(list.games[3].name, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"));

	/* a line past the limit is left out; one at it is kept */
	{
		char name[GAME_LIST_NAME_LENGTH + 1], map[300];

		memset(name, 'n', GAME_LIST_NAME_LENGTH);
		name[GAME_LIST_NAME_LENGTH] = 0;
		memset(map, 'm', sizeof(map) - 1);
		map[sizeof(map) - 1] = 0;
		snprintf(text, sizeof(text), "warthog-list 1 2\n" INVITE_A "\t%s\t%s\t2\t\t1\t16\t11\t\topen\n"
			INVITE_A "\t%s\t%.64s\t2\t\t1\t16\t11\t\topen\nend\n", name, map, name, map);
		CHECK(parse_list(text, strlen(text), &list) == GAME_LIST_OK && list.count == 1 && list.skipped == 1);
		CHECK(strlen(list.games[0].map) == 64);
	}

	/* NUL bytes within the text */
	{
		static const char with_nul[] = "warthog-list 1 1\n" INVITE_A "\tA\0B\tb\t2\t\t1\t16\t11\t\topen\nend\n";

		CHECK(parse_list(with_nul, sizeof(with_nul) - 1, &list) == GAME_LIST_OK && list.count == 1);
		CHECK(!strcmp(list.games[0].name, "AB"));
	}

	/* 64 games, the most */
	{
		size_t used = (size_t)snprintf(text, sizeof(text), "warthog-list 1 64\n");
		static char big[32768];

		memcpy(big, text, used);
		for (index = 0; index < 64; index++)
			used += (size_t)snprintf(big + used, sizeof(big) - used, INVITE_A "\tGame %d\tb\t2\t\t1\t16\t11\t\topen\n", index);
		used += (size_t)snprintf(big + used, sizeof(big) - used, "end\n");
		CHECK(parse_list(big, used, &list) == GAME_LIST_OK && list.count == 64);
		CHECK(!strcmp(list.games[63].name, "Game 63"));
	}
}

static void test_response(void)
{
	struct game_list_response response;
	struct game_list list;
	char head[256];
	char *data;
	size_t size, index;

	/* Content-Length, CR LF */
	snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nServer: halo-list/1\r\nContent-Type: text/plain; charset=us-ascii\r\n"
		"Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)strlen(good_list));
	data = response_of(head, good_list, &size);
	CHECK(parse_all(data, size, &list, &response) == GAME_LIST_OK);
	CHECK(response.status == 200 && response.content_length == (long)strlen(good_list) && list.count == 2);
	/* every cut short: an error, never a list */
	for (index = 0; index < size; index++)
	{
		CHECK(parse_all(data, index, &list, &response) != GAME_LIST_OK);
		check_invariants(&list, 1);
	}
	free(data);

	/* to the close, LF only, an empty reason */
	data = response_of("HTTP/1.0 200 \nContent-Type: text/plain\n\n", good_list, &size);
	CHECK(parse_all(data, size, &list, &response) == GAME_LIST_OK && list.count == 2);
	CHECK(response.content_length == -1);
	for (index = 0; index < size - 1; index++)
		CHECK(parse_all(data, index, &list, &response) != GAME_LIST_OK);
	free(data);

	/* no reason at all */
	data = response_of("HTTP/1.1 404\r\n\r\n", "not found", &size);
	CHECK(parse_all(data, size, &list, &response) != GAME_LIST_OK && response.status == 404);
	free(data);

	/* more than Content-Length: only that much is the body */
	snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", (unsigned)strlen(good_list));
	{
		char body[1024];

		snprintf(body, sizeof(body), "%sgarbage after", good_list);
		data = response_of(head, body, &size);
		CHECK(parse_all(data, size, &list, &response) == GAME_LIST_OK && response.body_size == strlen(good_list));
		free(data);
	}

	/* bad status lines */
	{
		static const char *const bad[] = {
			"HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 20 OK\r\n\r\n", "HTTP/1.1 2000 OK\r\n\r\n", "HTTP/1.1 099 OK\r\n\r\n",
			"HTTP/1.1  200 OK\r\n\r\n", "http/1.1 200 OK\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n", "HTTP/1.1 600 OK\r\n\r\n",
			"HTTP/1.1 200OK\r\n\r\n", "\r\n\r\n", "HTTP/1.1 200 OK",
		};

		for (index = 0; index < sizeof(bad) / sizeof(bad[0]); index++)
			CHECK(parse_all(bad[index], strlen(bad[index]), &list, &response) != GAME_LIST_OK);
	}

	/* bad headers */
	{
		static const char *const bad[] = {
			"HTTP/1.1 200 OK\r\nContent-Length: 10x\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Length: 70000\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nabcdef",
			"HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n",
			"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
			"HTTP/1.1 200 OK\r\nNo colon\r\n\r\n",
			"HTTP/1.1 200 OK\r\n: no name\r\n\r\n",
			"HTTP/1.1 200 OK\r\nName : space\r\n\r\n",
			"HTTP/1.1 200 OK\r\nA: b\r\n folded\r\n\r\n",
			"HTTP/1.1 200 OK\r\nA: b\x01\r\n\r\n",
			"HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort",
			"HTTP/1.1 200 OK\r\nA: b\r\n",
		};

		for (index = 0; index < sizeof(bad) / sizeof(bad[0]); index++)
		{
			CHECK(parse_all(bad[index], strlen(bad[index]), &list, &response) != GAME_LIST_OK);
			check_invariants(&list, 1);
		}
	}

	/* a header line past the limit, and too many headers */
	{
		static char big[GAME_LIST_RESPONSE_SIZE];
		size_t used = (size_t)snprintf(big, sizeof(big), "HTTP/1.1 200 OK\r\nX-Long: ");

		memset(big + used, 'a', GAME_LIST_HEADER_LINE);
		used += GAME_LIST_HEADER_LINE;
		used += (size_t)snprintf(big + used, sizeof(big) - used, "\r\n\r\n%s", good_list);
		CHECK(parse_all(big, used, &list, &response) == GAME_LIST_ERROR_HEADER);
		used = (size_t)snprintf(big, sizeof(big), "HTTP/1.1 200 OK\r\n");
		for (index = 0; index <= GAME_LIST_HEADERS; index++)
			used += (size_t)snprintf(big + used, sizeof(big) - used, "X-%u: a\r\n", (unsigned)index);
		used += (size_t)snprintf(big + used, sizeof(big) - used, "\r\n%s", good_list);
		CHECK(parse_all(big, used, &list, &response) == GAME_LIST_ERROR_HEADER);
		/* a status line with no line break, the whole buffer long */
		memset(big, 'H', sizeof(big));
		CHECK(parse_all(big, sizeof(big), &list, &response) != GAME_LIST_OK);
	}
}

static void test_server_line(void)
{
	char host[64];
	unsigned short port;

	CHECK(game_list_parse_server("warthog.milenko.org\r\n", 21, host, sizeof(host), &port) && port == 80 &&
		!strcmp(host, "warthog.milenko.org"));
	CHECK(game_list_parse_server(" 192.0.2.7:8080 \n", 17, host, sizeof(host), &port) && port == 8080 &&
		!strcmp(host, "192.0.2.7"));
	CHECK(!game_list_parse_server("host:0", 6, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("host:65536", 10, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("host:", 5, host, sizeof(host), &port));
	CHECK(!game_list_parse_server(":80", 3, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("", 0, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("bad host", 8, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("a/b", 3, host, sizeof(host), &port));
	CHECK(!game_list_parse_server("abcdefgh", 8, host, 8, &port));
	CHECK(game_list_parse_server("abcdefg", 7, host, 8, &port));
}

/* ---------- the mutation fuzzer */

static unsigned long long random_state = 0x9e3779b97f4a7c15ull;

static unsigned long next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (unsigned long)(random_state >> 11);
}

/* the screen's words (xbox_game_list_text.c) */
static void test_text(void)
{
	struct game_list list;
	struct game_list_game game;
	unsigned char order[GAME_LIST_MAXIMUM_GAMES];
	char text[64], small[4];
	int index;

	CHECK(game_list_map_name("bloodgulch", text, sizeof(text)) && !strcmp(text, "Blood Gulch"));
	CHECK(game_list_map_name("levels\\test\\carousel\\carousel", text, sizeof(text)) && !strcmp(text, "Derelict"));
	CHECK(game_list_map_name("levels/test/putput/putput", text, sizeof(text)) && !strcmp(text, "Chiron TL-34"));
	CHECK(game_list_map_name("HangEmHigh", text, sizeof(text)) && !strcmp(text, "Hang 'Em High"));
	CHECK(!game_list_map_name("timberland@ce", text, sizeof(text)) && !strcmp(text, "timberland (CE)"));
	CHECK(!game_list_map_name("levels\\test\\gephyrophobia\\gephyrophobia@md", text, sizeof(text)) &&
		!strcmp(text, "gephyrophobia (MD)"));
	CHECK(!game_list_map_name("bloodgulch@xx", text, sizeof(text)) && !strcmp(text, "bloodgulch (?)"));
	CHECK(!game_list_map_name("levels\\b30\\b30", text, sizeof(text)) && !strcmp(text, "b30"));
	CHECK(!game_list_map_name("bloodgulc", text, sizeof(text)) && !strcmp(text, "bloodgulc"));
	/* (an Xbox map pack's map: the list's name and tag for it, fields 14 and
	15 after the host's three; a list without them, or with them on another
	map, as before) */
	{
		static const char pack_list[] =
			"warthog-list 1 4\n"
			INVITE_A "\t[D] CE+\twizard@cex\t2\tSlayer\t0\t16\t32\t\topen\tpc_linux\tdedicated\tdelta\tGothra\tCE+\n"
			INVITE_B "\tNo pack fields\tbloodgulch@cex\t2\tSlayer\t0\t16\t32\t\topen\n"
			INVITE_A "\tNot a pack\tbloodgulch\t2\tSlayer\t0\t16\t32\t\topen\t\t\t\tAbyss\tCE+\n"
			INVITE_B "\tToo long\twizard@cex\t2\tSlayer\t0\t16\t32\t\topen\t\t\t\tGothra\tTOOLONG\tmore\n"
			"end\n";

		CHECK(parse_list(pack_list, sizeof(pack_list) - 1, &list) == GAME_LIST_OK && list.count == 4);
		CHECK(!strcmp(list.games[0].map_title, "Gothra") && !strcmp(list.games[0].map_tag, "CE+"));
		CHECK(!game_list_game_map_name(&list.games[0], text, sizeof(text)) && !strcmp(text, "Gothra (CE+)"));
		CHECK(!list.games[1].map_title[0]);
		CHECK(!game_list_game_map_name(&list.games[1], text, sizeof(text)) && !strcmp(text, "bloodgulch (?)"));
		CHECK(!list.games[2].map_title[0]);
		CHECK(game_list_game_map_name(&list.games[2], text, sizeof(text)) && !strcmp(text, "Blood Gulch"));
		CHECK(!list.games[3].map_title[0] && !list.games[3].map_tag[0]);
		CHECK(!game_list_game_map_name(&list.games[0], small, sizeof(small)) && !strcmp(small, "Got"));
	}
	CHECK(!game_list_map_name("", text, sizeof(text)) && !strcmp(text, ""));
	/* (cut to the room, always terminated) */
	CHECK(game_list_map_name("bloodgulch", small, sizeof(small)) && !strcmp(small, "Blo"));
	CHECK(!game_list_map_name("timberland@ce", small, sizeof(small)) && !strcmp(small, "tim"));

	memset(&game, 0, sizeof(game));
	game.engine = 2;
	CHECK(!strcmp(game_list_type_name(&game), "Slayer"));
	strcpy(game.gametype, "??? ?");
	CHECK(!strcmp(game_list_type_name(&game), "Slayer"));
	strcpy(game.gametype, "TS 50");
	CHECK(!strcmp(game_list_type_name(&game), "TS 50"));
	game.gametype[0] = 0;
	game.engine = 0;
	CHECK(!strcmp(game_list_type_name(&game), "Unknown"));
	game.engine = 15;
	CHECK(!strcmp(game_list_type_name(&game), "Unknown"));

	CHECK(!strcmp(game_list_state_name("playing"), "In Progress"));
	CHECK(!strcmp(game_list_state_name("open"), "Open"));
	CHECK(!strcmp(game_list_state_name("later"), "Unknown"));

	/* most players first, then by name, then as listed */
	memset(&list, 0, sizeof(list));
	list.count = 5;
	strcpy(list.games[0].name, "bravo");
	strcpy(list.games[1].name, "Alpha");
	strcpy(list.games[2].name, "charlie");
	list.games[2].players = 8;
	strcpy(list.games[3].name, "alpha");
	strcpy(list.games[4].name, "delta");
	list.games[4].players = 3;
	CHECK(game_list_order(&list, order) == 5);
	CHECK(order[0] == 2 && order[1] == 4 && order[2] == 1 && order[3] == 3 && order[4] == 0);
	list.count = GAME_LIST_MAXIMUM_GAMES;
	for (index = 0; index < list.count; index++)
		list.games[index].players = (short)(index % 7);
	CHECK(game_list_order(&list, order) == GAME_LIST_MAXIMUM_GAMES);
	for (index = 1; index < list.count; index++)
		CHECK(list.games[order[index - 1]].players >= list.games[order[index]].players);
	list.count = 1000;
	CHECK(game_list_order(&list, order) == GAME_LIST_MAXIMUM_GAMES);

	/* co-op games left out: the "Co-op" gametype, or no engine on a campaign
	level; a multiplayer map's engine-less game stays */
	memset(&list, 0, sizeof(list));
	list.count = 4;
	strcpy(list.games[0].name, "slayer");
	strcpy(list.games[0].map, "bloodgulch");
	list.games[0].engine = 2;
	strcpy(list.games[1].name, "coop");
	strcpy(list.games[1].map, "a30");
	strcpy(list.games[1].gametype, "Co-op");
	strcpy(list.games[2].name, "campaign");
	strcpy(list.games[2].map, "levels\\b30\\b30");
	strcpy(list.games[3].name, "unknown");
	strcpy(list.games[3].map, "dangercanyon");
	CHECK(game_list_is_coop(&list.games[1]) && game_list_is_coop(&list.games[2]));
	CHECK(!game_list_is_coop(&list.games[0]) && !game_list_is_coop(&list.games[3]));
	CHECK(game_list_order(&list, order) == 2);
	CHECK((order[0] == 0 && order[1] == 3) || (order[0] == 3 && order[1] == 0));
}

static void fuzz(long iterations)
{
	static const char *const pieces[] = {
		"\r\n", "\n", "\t", "\r", " ", ":", "end\n", "warthog-list 1 64\n", "Content-Length: 0\r\n",
		"Content-Length: 65536\r\n", "\r\n\r\n", INVITE_A, "\xff\xfe", "\0", "99999", "-1", "HTTP/1.1 200 OK\r\n",
	};
	static char buffer[GAME_LIST_RESPONSE_SIZE];
	char seed[4096];
	size_t seed_size;
	long iteration;

	seed_size = (size_t)snprintf(seed, sizeof(seed), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n%s",
		(unsigned)strlen(good_list), good_list);
	for (iteration = 0; iteration < iterations; iteration++)
	{
		struct game_list_response response;
		struct game_list list;
		size_t size = seed_size;
		int mutations = 1 + (int)(next_random() % 8);
		int result;

		memcpy(buffer, seed, seed_size);
		while (mutations--)
		{
			size_t at = size ? next_random() % size : 0;

			switch (next_random() % 6)
			{
			case 0: /* a byte changed */
				if (size)
					buffer[at] = (char)next_random();
				break;
			case 1: /* a byte removed */
				if (size)
				{
					memmove(buffer + at, buffer + at + 1, size - at - 1);
					size--;
				}
				break;
			case 2: /* cut short */
				size = at;
				break;
			case 3: /* a piece put in */
			{
				const char *piece = pieces[next_random() % (sizeof(pieces) / sizeof(pieces[0]))];
				size_t length = piece[0] ? strlen(piece) : 1;

				if (size + length <= sizeof(buffer))
				{
					memmove(buffer + at + length, buffer + at, size - at);
					memcpy(buffer + at, piece, length);
					size += length;
				}
				break;
			}
			case 4: /* a run of one byte, up to the whole buffer */
			{
				size_t length = next_random() % (sizeof(buffer) - size + 1);
				int c = "a\t\n\r\0 9"[next_random() % 7];

				memmove(buffer + at + length, buffer + at, size - at);
				memset(buffer + at, c, length);
				size += length;
				break;
			}
			default: /* a part repeated */
			{
				size_t length = size - at;

				if (size + length <= sizeof(buffer))
				{
					memcpy(buffer + size, buffer + at, length);
					size += length;
				}
				break;
			}
			}
		}
		result = parse_all(buffer, size, &list, &response);
		check_invariants(&list, result);
		/* the body alone, as a list */
		result = parse_list(buffer, size, &list);
		check_invariants(&list, result);
		if (failures)
		{
			fprintf(stderr, "fuzz: failed at iteration %ld\n", iteration);
			return;
		}
	}
}

/* the public DNS lookup's query and answers (game_list_resolve) */
static void test_dns(void)
{
	unsigned char query[300], reply[512];
	unsigned long address = 0;
	int length, size, index;
	static const unsigned char name[] = "\x05stats\x0b" "chupathingy\x03org";

	length = game_list_dns_query("stats.chupathingy.org", 0x1234, query, (int)sizeof(query));
	CHECK(length == 12 + (int)sizeof(name) + 4);
	CHECK(query[0] == 0x12 && query[1] == 0x34 && query[2] == 0x01 && query[5] == 1);
	CHECK(!memcmp(query + 12, name, sizeof(name)));
	CHECK(query[length - 3] == 1 && query[length - 1] == 1);
	CHECK(game_list_dns_query("stats.chupathingy.org.", 1, query, (int)sizeof(query)) == length);
	CHECK(!game_list_dns_query("", 1, query, (int)sizeof(query)));
	CHECK(!game_list_dns_query("a..b", 1, query, (int)sizeof(query)));
	CHECK(!game_list_dns_query("stats.chupathingy.org", 1, query, 20));

	/* (an answer: the question, an alias, then the address, by pointers) */
	length = game_list_dns_query("stats.chupathingy.org", 0x1234, query, (int)sizeof(query));
	memcpy(reply, query, (size_t)length);
	reply[2] = 0x81;
	reply[3] = 0x80;
	reply[7] = 2;
	size = length;
	{
		static const unsigned char alias[] = { 0xC0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 6, 3, 'w', 'w', 'w', 0xC0, 12 };
		static const unsigned char record[] = { 0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 10, 20, 30, 40 };

		memcpy(reply + size, alias, sizeof(alias));
		size += (int)sizeof(alias);
		memcpy(reply + size, record, sizeof(record));
		size += (int)sizeof(record);
	}
	CHECK(game_list_dns_answer(reply, size, 0x1234, &address));
	CHECK(!memcmp(&address, "\x0a\x14\x1e\x28", 4));
	/* (another query's, an error, cut short) */
	CHECK(!game_list_dns_answer(reply, size, 0x1235, &address));
	reply[3] = 0x83;
	CHECK(!game_list_dns_answer(reply, size, 0x1234, &address));
	reply[3] = 0x80;
	for (index = 0; index < size; index++)
		CHECK(!game_list_dns_answer(reply, index, 0x1234, &address));
	/* (any bytes: never past them) */
	for (index = 0; index < 20000; index++)
	{
		int byte, count = 12 + rand() % 120;

		for (byte = 0; byte < count; byte++)
			reply[byte] = (unsigned char)rand();
		reply[0] = 0x12;
		reply[1] = 0x34;
		reply[2] |= 0x80;
		reply[3] &= 0xF0;
		game_list_dns_answer(reply, count, 0x1234, &address);
	}
}

int main(int argc, char **argv)
{
	long iterations = argc > 1 ? atol(argv[1]) : 200000;

	test_list();
	test_response();
	test_server_line();
	test_text();
	test_dns();
	fuzz(iterations);
	if (failures)
	{
		fprintf(stderr, "game_list_test: %d failed\n", failures);
		return 1;
	}
	printf("game_list_test: ok (%ld fuzz iterations)\n", iterations);
	return 0;
}
