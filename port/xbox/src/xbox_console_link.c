/*
XBOX_CONSOLE_LINK.C

Link Profile on the original Xbox (the game's port/console/link/
console_link.h): the hooks it asks of the console. Its requests go to the
console list's host over plain HTTP (game_list_request, as the list and the
signed claim do), on a short-lived thread of their own (its stack given
back when it ends). The device key and the linked profile are kept beside
config.toml, on the utility drive: D:\delta_device.key and D:\delta_link.txt.

Delta Peer's PROFILE carries console_profile_id(): an identity file
(D:\game_list_player.key, xbox_player_key.c) first, as before; else the
console's own link.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "console_link.h"

#include "../include/xbox_claim.h"
#include "../include/xbox_game_list.h"
#include "../include/xbox_player_key.h"

void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);
/* xbox_port.c's: safe from any thread, reaches debug.txt from the main loop */
void platform_log(const char *format, ...);
extern unsigned char transport_initialized;

#define LINK_TIMEOUT 15000
#define LINK_STACK (64 * 1024)
#define LINK_RESPONSE_SIZE 2048

static CRITICAL_SECTION link_lock;
static int link_started;

static int link_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	static char buffer[LINK_RESPONSE_SIZE];
	struct game_list_response response;
	unsigned long address, received;
	unsigned short port;
	char host[96];
	unsigned long size;

	if (!transport_initialized || !xbox_game_list_server(&address, host, sizeof(host), &port))
		return 0;
	/* (one request at a time: console_link.c's) */
	if (game_list_request(address, port, host, path, body, buffer, sizeof(buffer), &received, LINK_TIMEOUT) !=
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

struct link_thread
{
	void (*function)(void *context);
	void *context;
};

static struct link_thread link_thread_start;

static unsigned long __stdcall link_thread(void *parameter)
{
	struct link_thread *start = parameter;

	start->function(start->context);
	return 0;
}

static int link_start_thread(void (*function)(void *context), void *context)
{
	HANDLE thread;

	/* (console_link.c runs one at a time, so one start's place is enough) */
	link_thread_start.function = function;
	link_thread_start.context = context;
	thread = CreateThread(NULL, LINK_STACK, link_thread, &link_thread_start, 0, NULL);
	if (!thread)
		return 0;
	xbox_memory_heap_allocation("xbox_console_link.c (thread stack)", 0, LINK_STACK, thread);
	CloseHandle(thread);
	return 1;
}

static void link_enter(void)
{
	EnterCriticalSection(&link_lock);
}

static void link_leave(void)
{
	LeaveCriticalSection(&link_lock);
}

static void link_random(void *buffer, int size)
{
	/* (XNet's own: its keys' source, as internet play's) */
	if (XNetRandom((BYTE *)buffer, (UINT)size) != 0)
		memset(buffer, 0, (size_t)size);
}

static unsigned long link_milliseconds(void)
{
	return GetTickCount();
}

static void link_log(const char *text)
{
	platform_log("%s", text);
}

static const struct console_link_port xbox_link_port =
{
	XBOX_CLAIM_PLATFORM,
	XBOX_CLAIM_BUILD,
	"D:\\delta_device.key",
	"D:\\delta_link.txt",
	link_request,
	/* (no TLS on the console yet) */
	NULL,
	link_start_thread,
	link_enter,
	link_leave,
	link_random,
	link_milliseconds,
	link_log,
};

/* the main thread's, before the panel or Delta Peer asks */
void xbox_console_link_start(void)
{
	if (link_started)
		return;
	link_started = 1;
	InitializeCriticalSection(&link_lock);
	console_link_initialize(&xbox_link_port);
}

int console_profile_id(unsigned char *id)
{
	if (xbox_player_id(id))
		return 1;
	xbox_console_link_start();
	return console_link_player_id(id);
}
