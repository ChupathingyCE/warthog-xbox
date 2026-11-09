/*
XBOX_CLAIMS.C

The claim of the console's own line in each online game it hosted or
joined, on the original Xbox (the game's port/console/reports/
console_claims.h): the hooks it asks of the console. The claim goes to the
console list's host over plain HTTP (game_list_request, as the list, the
signed platform claim and Link Profile do), signed, on a short-lived
thread of its own whose stack is given back when it ends.

Who signs: the identity file (D:\game_list_player.key) first, as the
reports, else the console's own link (D:\delta_device.key and
D:\delta_link.txt). A console with neither sends nothing.
network.report_joined_games = false in D:\config.toml sends nothing either.

Memory: no heap at all. The claim's fields are a few hundred bytes of the
game's; the thread's stack (CLAIM_STACK) holds the request and its answer
and is taken only while a claim is under way.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "port/console/reports/console_claims.h"

#include "../include/xbox_claim.h"
#include "../include/xbox_game_list.h"
#include "../include/xbox_player_key.h"

void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);
/* xbox_port.c's: safe from any thread, reaches debug.txt from the main loop */
void platform_log(const char *format, ...);
/* xbox_console_link.c's: the link read from D:\delta_link.txt */
void xbox_console_link_start(void);
extern unsigned char transport_initialized;

#define CLAIM_TIMEOUT 15000
/* the request (1 KB), its answer (CLAIM_RESPONSE_SIZE), the claim's message
and body (about 1.3 KB) and the signature's frames */
#define CLAIM_STACK (32 * 1024)
#define CLAIM_RESPONSE_SIZE 2048

static int claims_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	char buffer[CLAIM_RESPONSE_SIZE];
	struct game_list_response response;
	unsigned long address, received, size;
	unsigned short port;
	char host[96];

	if (!transport_initialized || !xbox_game_list_server(&address, host, sizeof(host), &port))
		return 0;
	if (game_list_request(address, port, host, path, body, buffer, sizeof(buffer), &received, CLAIM_TIMEOUT) !=
		GAME_LIST_OK || game_list_parse_response(buffer, received, &response) != GAME_LIST_OK)
	{
		return 0;
	}
	*status = response.status;
	size = response.body_size < (unsigned long)reply_size - 1 ? response.body_size : (unsigned long)reply_size - 1;
	memcpy(reply, response.body, size);
	reply[size] = 0;
	return 1;
}

struct claims_start
{
	void (*function)(void *context);
	void *context;
};

static struct claims_start claims_start;

static unsigned long __stdcall claims_thread(void *parameter)
{
	struct claims_start *start = parameter;

	start->function(start->context);
	return 0;
}

static int claims_start_thread(void (*function)(void *context), void *context)
{
	HANDLE thread;

	/* (console_claims.c runs one claim at a time, so one start's place is enough) */
	claims_start.function = function;
	claims_start.context = context;
	thread = CreateThread(NULL, CLAIM_STACK, claims_thread, &claims_start, 0, NULL);
	if (!thread)
		return 0;
	xbox_memory_heap_allocation("xbox_claims.c (thread stack)", 0, CLAIM_STACK, thread);
	/* (below the game's thread: the signature would hitch the frames) */
	SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
	CloseHandle(thread);
	return 1;
}

static void claims_sleep(unsigned long milliseconds)
{
	Sleep(milliseconds);
}

static unsigned long claims_milliseconds(void)
{
	return GetTickCount();
}

static int claims_player_key(unsigned char *key)
{
	return xbox_player_key_from_file(XBOX_CLAIM_KEY_FILE, key);
}

static void claims_log(const char *text)
{
	platform_log("%s", text);
}

static const struct console_claims_port xbox_claims_port =
{
	XBOX_CLAIM_PLATFORM,
	claims_request,
	claims_start_thread,
	claims_sleep,
	claims_milliseconds,
	claims_player_key,
	claims_log,
};

void console_claims_port_start(void)
{
	/* (the link's state, which a claim signed by the console's key needs) */
	xbox_console_link_start();
	console_claims_initialize(&xbox_claims_port);
}
