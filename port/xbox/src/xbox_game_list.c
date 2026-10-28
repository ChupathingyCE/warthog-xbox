/*
XBOX_GAME_LIST.C

The game list on the console (xbox_game_list.h): once the game's network is
up (transport_initialize), a thread of its own fetches GET
/v1/console/games over HTTPS from stats.chupathingy.org (xbox_https.c, the
site's key pinned), or, when that fails, over plain HTTP from the list's
host, and parses it; the main loop then takes it (xbox_game_list_get) for the
Multiplayer menu's ONLINE GAMES (port/xbox/game/xbox_browser_screen.c),
which asks again on X, and logs it to debug.txt. Nothing waits on it: the
game goes on as if there were no list. Joining comes later (the top level
README, "Online Games on the Xbox").

No address is ever logged: the host's name, or "console.game_list_server's"
(D:\config.toml), only.

The same thread then fetches the signed legacy table when it is due
(xbox_https_table_refresh), so its stack has room for HTTPS.
*/

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_config.h"
#include "../include/xbox_game_list.h"
#include "../include/xbox_https.h"
#include "port/console/https/console_https.h"
#include "port/linux/include/delta.h"
#include "port/linux/src/port_config.h"

ULONG __cdecl DbgPrint(PCSTR format, ...);
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);
int snprintf(char *buffer, size_t size, const char *format, ...);
/* the game's (cseries/errors.c, bungie_net/network/transport_endpoint_set_winsock.c) */
void error(short priority, const char *format, ...);
extern unsigned char transport_initialized;

#define ERROR_LOG 3 /* _error_log: debug.txt only */
#define NETWORK_WAIT 60000
#define FETCH_TIMEOUT 15000
#define ATTEMPTS 3
#define RETRY_WAIT 10000
#define LIST_THREAD_STACK (48 * 1024 + CONSOLE_HTTPS_STACK_SIZE + DELTA_REFRESH_STACK_SIZE)

enum
{
	_list_idle,
	_list_fetching,
	_list_done,
	_list_failed
};

/* the fetch's thread writes fetched, then the state (an interlocked store,
so the list is whole before it: set_state); the main thread takes it into shown once
the state is done, and only then may a new fetch start */
static volatile LONG list_state = _list_idle;
static int list_taken = 1;
static struct game_list fetched;
static int list_error;
static int list_status;
/* the list came over HTTPS; HTTPS was refused for good (a key or a name) */
static int list_secure;
static int https_refused;
/* the main thread's (xbox_game_list_get): the last list that came, kept
while a refresh fails */
static struct game_list shown;
static int shown_lists;
static int shown_error;
static int shown_state = XBOX_GAME_LIST_NONE;
/* (whether the list being taken came over HTTPS: the fetch's thread writes
it before the state) */
static int shown_secure_next;

/* (a locked store: what was written before it is seen before it) */
static void set_state(LONG state)
{
	__atomic_store_n(&list_state, state, __ATOMIC_SEQ_CST);
}

/* the server to ask (xbox_game_list.h): the list's host, else
console.game_list_server's (or the old D:\game_list.txt's) */
int xbox_game_list_server(unsigned long *address, char *host, int host_size, unsigned short *port)
{
	char line[128];
	unsigned long size;

	snprintf(host, host_size, "%s", GAME_LIST_HOST);
	*port = GAME_LIST_PORT;
	if (game_list_resolve(GAME_LIST_HOST, address) == GAME_LIST_OK)
		return 1;
	size = xbox_config_line(GAME_LIST_FALLBACK_SETTING, GAME_LIST_FALLBACK_FILE, line, sizeof(line));
	if (!size)
	{
		DbgPrint("halo: game list: %s did not resolve, and there is no %s\n", GAME_LIST_HOST,
			GAME_LIST_FALLBACK_SETTING);
		return 0;
	}
	if (!game_list_parse_server(line, size, host, host_size, port))
	{
		DbgPrint("halo: game list: %s did not resolve, and %s is not a host or address\n", GAME_LIST_HOST,
			GAME_LIST_FALLBACK_SETTING);
		return 0;
	}
	if (game_list_resolve(host, address) != GAME_LIST_OK)
	{
		DbgPrint("halo: game list: neither %s nor %s's host resolved\n", GAME_LIST_HOST, GAME_LIST_FALLBACK_SETTING);
		return 0;
	}
	DbgPrint("halo: game list: %s did not resolve; using %s's server\n", GAME_LIST_HOST, GAME_LIST_FALLBACK_SETTING);
	/* (a dotted address: the request still names the list's host, which the
	server's virtual host answers to) */
	if (host[0] >= '0' && host[0] <= '9')
		snprintf(host, host_size, "%s", GAME_LIST_HOST);
	return 1;
}

/* the list over HTTPS from the site (its key pinned: docs/console-https.md):
GAME_LIST_OK and the list, or another result and the plain HTTP list is
asked instead */
static int fetch_https(char *buffer)
{
	struct game_list_response response;
	unsigned long received = 0;
	int result;

	result = console_https_request(CONSOLE_HTTPS_SITE, GAME_LIST_PATH, NULL, NULL, buffer, GAME_LIST_RESPONSE_SIZE,
		&received, FETCH_TIMEOUT);
	if (result != CONSOLE_HTTPS_OK)
	{
		DbgPrint("halo: game list: HTTPS: %s; asking over plain HTTP\n", console_https_error_string(result));
		/* (a key or a name refused is refused again: plain HTTP for the rest
		of this run) */
		if (result == CONSOLE_HTTPS_ERROR_KEY || result == CONSOLE_HTTPS_ERROR_NAME)
			https_refused = 1;
		return GAME_LIST_ERROR_CONNECT;
	}
	result = game_list_parse_response(buffer, received, &response);
	if (result != GAME_LIST_OK)
		return result;
	list_status = response.status;
	if (response.status != 200)
	{
		DbgPrint("halo: game list: HTTPS: status %d; asking over plain HTTP\n", response.status);
		return GAME_LIST_ERROR_HTTP;
	}
	result = game_list_parse(response.body, response.body_size, &fetched);
	if (result == GAME_LIST_OK)
		list_secure = 1;
	return result;
}

static int fetch_once(char *buffer, int attempt)
{
	struct game_list_response response;
	unsigned long address, received;
	unsigned short port;
	char host[96];
	int result;

	list_status = 0;
	list_secure = 0;
	/* (HTTPS first, on each refresh's first try; a list that came over it
	ends the fetch, anything else falls back) */
	if (attempt == 1 && !https_refused && console_https_available())
	{
		result = fetch_https(buffer);
		if (result == GAME_LIST_OK || result == GAME_LIST_ERROR_FORMAT)
			return result;
		list_status = 0;
	}
	if (!xbox_game_list_server(&address, host, sizeof(host), &port))
		return GAME_LIST_ERROR_RESOLVE;
	result = game_list_fetch(address, port, host, GAME_LIST_PATH, buffer, GAME_LIST_RESPONSE_SIZE, &received,
		FETCH_TIMEOUT);
	if (result != GAME_LIST_OK)
		return result;
	result = game_list_parse_response(buffer, received, &response);
	if (result != GAME_LIST_OK)
		return result;
	list_status = response.status;
	if (response.status != 200)
		return GAME_LIST_ERROR_HTTP;
	return game_list_parse(response.body, response.body_size, &fetched);
}

static unsigned long __stdcall game_list_thread(void *parameter)
{
	/* (the stack's: the thread has no other use for it, and the heap has
	no 10 KB to spare on a 64 MB console while a game is open) */
	char buffer[GAME_LIST_RESPONSE_SIZE];
	unsigned long waited = 0;
	int attempt;
	int result = GAME_LIST_ERROR_CONNECT;

	(void)parameter;
	while (!transport_initialized && waited < NETWORK_WAIT)
	{
		Sleep(500);
		waited += 500;
	}
	if (!transport_initialized)
	{
		DbgPrint("halo: game list: the network did not start; no list\n");
		list_error = GAME_LIST_ERROR_CONNECT;
		set_state(_list_failed);
		return 0;
	}
	xbox_https_start();
	DbgPrint("halo: game list: getting %s%s\n", https_refused ? "http://" GAME_LIST_HOST : "https://" CONSOLE_HTTPS_SITE,
		GAME_LIST_PATH);
	for (attempt = 1; attempt <= ATTEMPTS; attempt++)
	{
		result = fetch_once(buffer, attempt);
		if (result == GAME_LIST_OK || result == GAME_LIST_ERROR_FORMAT)
			break;
		DbgPrint("halo: game list: try %d of %d: %s%s\n", attempt, ATTEMPTS, game_list_error_string(result),
			result == GAME_LIST_ERROR_HTTP ? " (an HTTP error status)" : "");
		if (attempt < ATTEMPTS)
			Sleep(RETRY_WAIT);
	}
	list_error = result;
	if (result == GAME_LIST_OK)
	{
		DbgPrint("halo: game list: %d games (format %d) over %s%s\n", fetched.count, fetched.format,
			list_secure ? "HTTPS" : "plain HTTP", fetched.skipped ? ", some lines left out" : "");
	}
	else if (result == GAME_LIST_ERROR_CONNECT || result == GAME_LIST_ERROR_RESOLVE || result == GAME_LIST_ERROR_TIMEOUT)
	{
		if (!config_boolean("console.bypass_security"))
			DbgPrint("halo: game list: (the internet needs XNet's insecure mode: console.bypass_security = true "
				"in D:\\config.toml)\n");
	}
	shown_secure_next = list_secure;
	set_state(result == GAME_LIST_OK ? _list_done : _list_failed);
	/* (the signed legacy table, when it is due: after the list, so that the
	list is never held up by it) */
	xbox_https_table_refresh();
	return 0;
}

void xbox_game_list_request(void)
{
	HANDLE thread;

	/* (the main thread's only; not while a fetch runs, or before its list
	is taken) */
	if (list_state == _list_fetching || !list_taken)
		return;
	list_taken = 0;
	shown_state = XBOX_GAME_LIST_FETCHING;
	set_state(_list_fetching);
	/* (the signed table's cache, and the network rules told of it: once) */
	xbox_https_table_start();
	/* (a stack of its own size: 0 is the main thread's, the XBE's 1 MB, all
	of it committed memory on the Xbox; 48 KB holds the response's buffer
	and the fetch's frames, CONSOLE_HTTPS_STACK_SIZE more the TLS
	session's arena and Mbed TLS's frames, and DELTA_REFRESH_STACK_SIZE the
	signed table's document; all of it given back when the thread ends) */
	thread = CreateThread(NULL, LIST_THREAD_STACK, game_list_thread, NULL, 0, NULL);
	if (thread)
		xbox_memory_heap_allocation("xbox_game_list.c (thread stack)", 0, LIST_THREAD_STACK, thread);
	if (!thread)
	{
		list_error = GAME_LIST_ERROR_CONNECT;
		set_state(_list_failed);
		return;
	}
	CloseHandle(thread);
}

void xbox_game_list_update(void)
{
	LONG state = list_state;
	int index;

	if (list_taken || (state != _list_done && state != _list_failed))
		return;
	list_taken = 1;
	if (state == _list_failed)
	{
		shown_error = list_error;
		shown_state = XBOX_GAME_LIST_FAILED;
		error(ERROR_LOG, "game list: could not get the list from %s (%s)", GAME_LIST_HOST,
			game_list_error_string(list_error));
		return;
	}
	memcpy(&shown, &fetched, sizeof(shown));
	shown_error = GAME_LIST_OK;
	shown_state = XBOX_GAME_LIST_READY;
	error(ERROR_LOG, "game list: %d games from %s", shown.count,
		shown_secure_next ? CONSOLE_HTTPS_SITE " (HTTPS)" : GAME_LIST_HOST " (plain HTTP)");
	/* (each game the first time; a refresh's count alone) */
	if (!shown_lists++)
	{
		for (index = 0; index < shown.count; index++)
		{
			const struct game_list_game *game = &shown.games[index];

			char line[256];

			snprintf(line, sizeof(line), "game list: %s | %s | %s | %d/%d | v%u | %s | %s", game->name, game->map,
				game->gametype[0] ? game->gametype : "-", game->players, game->maximum_players, game->version,
				game->region[0] ? game->region : "-", game->state);
			error(ERROR_LOG, "%s", line);
			DbgPrint("halo: %s\n", line);
		}
	}
}

const struct game_list *xbox_game_list_get(int *lists)
{
	if (lists)
		*lists = shown_lists;
	return shown_lists ? &shown : NULL;
}

int xbox_game_list_state(void)
{
	return shown_state;
}

const char *xbox_game_list_error(void)
{
	return game_list_error_string(shown_error);
}
