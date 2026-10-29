/*
XBOX_GAME_LIST.H

The game list on the console: the site's list for consoles (GET
/v1/console/games), over HTTPS from stats.chupathingy.org (xbox_https.c),
else over plain HTTP/1.0 from warthog.milenko.org, on a thread of its own
(xbox_game_list.c), the response and the
list parsed with bounds on everything (xbox_game_list_parse.c). The site's
text is untrusted: no length or count from it is believed past these
limits.

The parsers and the fetch are plain C, so port/xbox/tests builds them on
the host too.
*/

#ifndef __XBOX_GAME_LIST_H
#define __XBOX_GAME_LIST_H

#define GAME_LIST_HOST "warthog.milenko.org"
/* a build of more than 16 players (configure.py --console-players) asks
for the games set for up to its own count; a stock build's plain request
gets those of up to 16 */
#if defined(HALO_CONSOLE_PLAYERS) && HALO_CONSOLE_PLAYERS > 16
#define GAME_LIST_STRING_OF(x) #x
#define GAME_LIST_STRING(x) GAME_LIST_STRING_OF(x)
#define GAME_LIST_PATH "/v1/console/games?packs=1&players=" GAME_LIST_STRING(HALO_CONSOLE_PLAYERS)
#else
#define GAME_LIST_PATH "/v1/console/games?packs=1"
#endif
/* (packs=1: this build plays Xbox map packs' maps, <file>@<pack> in
maps_<pack>, so the list sends their games too, each with its map's name
and its pack's tag in the line's 14th and 15th fields; a build before map
packs asks without it and gets none of them) */
#define GAME_LIST_PORT 80
/* console.game_list_server in D:\config.toml (once D:\game_list.txt, still
read for one release): a host or an IPv4 address, ":port" optional; used
when the list's host does not resolve */
#define GAME_LIST_FALLBACK_SETTING "console.game_list_server"
#define GAME_LIST_FALLBACK_FILE "D:\\game_list.txt"

/* the whole response, headers and all, at most: the console's list is
filtered to the games it can join (about 130 bytes a line, 41 lines in
5.3 KB), and the buffer is on the fetch thread's stack, not the 64 KB
malloc that a 64 MB console had no room for */
#define GAME_LIST_RESPONSE_SIZE 10240
/* the response's header lines, at most this many of this many bytes */
#define GAME_LIST_HEADER_LINE 1024
#define GAME_LIST_HEADERS 64
/* the list: games and a line's bytes (its line break apart), as the server's
CONSOLE_GAMES and CONSOLE_LINE */
#define GAME_LIST_MAXIMUM_GAMES 64
#define GAME_LIST_LINE 255
#define GAME_LIST_FORMAT 1
/* the fields' characters, at most (the server's caps) */
#define GAME_LIST_INVITE_LENGTH 64
#define GAME_LIST_NAME_LENGTH 32
#define GAME_LIST_MAP_LENGTH 64
#define GAME_LIST_GAMETYPE_LENGTH 24
#define GAME_LIST_REGION_LENGTH 16
#define GAME_LIST_STATE_LENGTH 8
/* an Xbox map pack's map's name and its pack's tag ("Gothra", "CE+") */
#define GAME_LIST_MAP_TITLE_LENGTH 32
#define GAME_LIST_MAP_TAG_LENGTH 4

enum
{
	GAME_LIST_OK = 0,
	/* the HTTP response */
	GAME_LIST_ERROR_STATUS_LINE,
	GAME_LIST_ERROR_HEADER,
	GAME_LIST_ERROR_HEADERS_TRUNCATED,
	GAME_LIST_ERROR_CONTENT_LENGTH,
	GAME_LIST_ERROR_BODY_TRUNCATED,
	GAME_LIST_ERROR_ENCODING,
	/* the list */
	GAME_LIST_ERROR_LIST_HEADER,
	GAME_LIST_ERROR_FORMAT,
	GAME_LIST_ERROR_LIST_TRUNCATED,
	GAME_LIST_ERROR_COUNT,
	/* the fetch */
	GAME_LIST_ERROR_RESOLVE,
	GAME_LIST_ERROR_CONNECT,
	GAME_LIST_ERROR_SEND,
	GAME_LIST_ERROR_RECEIVE,
	GAME_LIST_ERROR_TIMEOUT,
	GAME_LIST_ERROR_TOO_LARGE,
	GAME_LIST_ERROR_HTTP,
	NUMBER_OF_GAME_LIST_ERRORS
};

struct game_list_response
{
	int status;
	/* -1: none given */
	long content_length;
	const char *body;
	unsigned long body_size;
};

struct game_list_game
{
	char invite[GAME_LIST_INVITE_LENGTH + 1];
	char name[GAME_LIST_NAME_LENGTH + 1];
	char map[GAME_LIST_MAP_LENGTH + 1];
	char gametype[GAME_LIST_GAMETYPE_LENGTH + 1];
	char region[GAME_LIST_REGION_LENGTH + 1];
	char state[GAME_LIST_STATE_LENGTH + 1];
	/* an Xbox map pack's map's name and its pack's tag, as the list says
	(fields 14 and 15; empty when it doesn't) */
	char map_title[GAME_LIST_MAP_TITLE_LENGTH + 1];
	char map_tag[GAME_LIST_MAP_TAG_LENGTH + 1];
	short engine;
	short players;
	short maximum_players;
	unsigned short version;
};

struct game_list
{
	int format;
	int count;
	/* the lines left out (a field missing or out of bounds) */
	int skipped;
	struct game_list_game games[GAME_LIST_MAXIMUM_GAMES];
};

const char *game_list_error_string(int error);

/* an HTTP/1.x response, size bytes (no terminator needed): its status, and
its body (pointing into data) as Content-Length says, else to the end */
int game_list_parse_response(const char *data, unsigned long size, struct game_list_response *response);

/* the list's text, size bytes (no terminator needed) */
int game_list_parse(const char *text, unsigned long size, struct game_list *list);

/* "host[:port]" or "a.b.c.d[:port]" (console.game_list_server's): the host
(at most host_size - 1 characters) and the port; 0 if it is neither */
int game_list_parse_server(const char *text, unsigned long size, char *host, int host_size, unsigned short *port);

/* ---------- the screen's words (xbox_game_list_text.c) */

/* a map as the menus name it ("Blood Gulch" for bloodgulch, or for its
scenario's path; else the file's name, "timberland (PC)" for a Halo PC map,
"(MD)" for HaloMD's) in text, at most size - 1 characters: 1 if it is one of
the console's own multiplayer maps */
int game_list_map_name(const char *map, char *text, unsigned long size);
/* a game's map in words: an Xbox map pack's by the name and tag the list
gives it ("Gothra (CE+)"), any other as game_list_map_name says (its
result) */
int game_list_game_map_name(const struct game_list_game *game, char *text, unsigned long size);
/* a game's type: its gametype, else its engine's name, else "Unknown" */
const char *game_list_type_name(const struct game_list_game *game);
/* a game's state ("open", ...) in words */
const char *game_list_state_name(const char *state);
/* whether a game is network co-op (its gametype "Co-op", or no engine on a
campaign level), which the console does not play */
int game_list_is_coop(const struct game_list_game *game);
/* the games in the screen's order (most players first, then by name), co-op
games left out, as indices into list->games in order; their count */
int game_list_order(const struct game_list *list, unsigned char *order);

/* ---------- the fetch (xbox_game_list_fetch.c) */

/* the host's IPv4 address in network order: a dotted address as it is,
else looked up (XNet's DNS on the console) */
int game_list_resolve(const char *host, unsigned long *address);

/* GET path from address:port, with Host: host, into buffer (at most size
bytes, the whole response); within timeout_ms */
int game_list_fetch(unsigned long address, unsigned short port, const char *host, const char *path,
	char *buffer, unsigned long size, unsigned long *received, unsigned long timeout_ms);

/* the same, as a POST of body (JSON) when body is not NULL (the signed
platform claim's, xbox_claim.h); the whole request is at most 1 KB */
int game_list_request(unsigned long address, unsigned short port, const char *host, const char *path,
	const char *body, char *buffer, unsigned long size, unsigned long *received, unsigned long timeout_ms);

/* ---------- the console (xbox_game_list.c) */

enum
{
	XBOX_GAME_LIST_NONE,
	XBOX_GAME_LIST_FETCHING,
	XBOX_GAME_LIST_READY,
	XBOX_GAME_LIST_FAILED
};

/* the list fetched once the network is up, on a thread of its own; again
on a later call, once that one's list is taken (xbox_game_list_update) */
void xbox_game_list_request(void);
/* the main thread's part, each loop: a list that came taken (and logged
to debug.txt) */
void xbox_game_list_update(void);
/* the main thread's: the last list taken (NULL before one came; kept while
a refresh fails), and how many have come */
const struct game_list *xbox_game_list_get(int *lists);
/* the site to ask (the list's host, else console.game_list_server's): 1 with
its address, the Host: name and the port; the signed claim asks it too */
int xbox_game_list_server(unsigned long *address, char *host, int host_size, unsigned short *port);
/* the latest request's state (XBOX_GAME_LIST_...), and its error's words */
int xbox_game_list_state(void);
const char *xbox_game_list_error(void);

#endif
