/*
XBOX_REPORTS.C

The reports of the games the console joins, on the original Xbox (the
game's port/console/reports/console_reports.h): the hooks it asks of the
console. A report goes to stats.chupathingy.org over HTTPS (xbox_https.c,
the site's key pinned), never over plain HTTP, on a short-lived thread of
its own whose stack holds the TLS session and is given back when it ends.

Who the player is: the identity file (D:\game_list_player.key) first, as
Delta Peer's PROFILE has it (xbox_console_link.c's console_profile_id),
else the console's own link (D:\delta_device.key and D:\delta_link.txt).
network.report_joined_games = false in D:\config.toml sends nothing.

Memory: the thread's stack (REPORT_STACK) and the report (a few KB) are
taken only when a game ends, and only with REPORT_RESERVE left free beside
them (a 64 MB console in a full game keeps its margin); otherwise the
game is not reported, and debug.txt says so.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "port/console/https/console_https.h"
#include "port/console/reports/console_reports.h"

#include "../include/xbox_claim.h"
#include "../include/xbox_https.h"
#include "../include/xbox_memory.h"
#include "../include/xbox_player_key.h"

/* xbox_port.c's: safe from any thread, reaches debug.txt from the main loop */
void platform_log(const char *format, ...);
/* xbox_console_link.c's: the link read from D:\delta_link.txt */
void xbox_console_link_start(void);
extern unsigned char transport_initialized;

#define REPORT_TIMEOUT 15000
/* the TLS session's arena and Mbed TLS's frames, and the sender's own
(its reply buffer, the challenge and signature) */
#define REPORT_STACK (CONSOLE_HTTPS_STACK_SIZE + 16 * 1024)
/* what must stay free beside the thread and the report */
#define REPORT_RESERVE (256ul * 1024)

static int report_request(const char *path, const char *body, int *status, char *reply, int reply_size)
{
	int result;

	if (!transport_initialized)
		return 0;
	xbox_https_start();
	result = console_https_call(CONSOLE_HTTPS_SITE, path, body, status, reply, reply_size, REPORT_TIMEOUT);
	if (result != CONSOLE_HTTPS_OK)
	{
		platform_log("Game list: HTTPS: %s", console_https_error_string(result));
		return 0;
	}
	return 1;
}

struct report_start
{
	void (*function)(void *context);
	void *context;
};

static struct report_start report_start;

static unsigned long __stdcall report_thread(void *parameter)
{
	struct report_start *start = parameter;

	start->function(start->context);
	return 0;
}

static int report_start_thread(void (*function)(void *context), void *context)
{
	HANDLE thread;

	/* (console_reports.c sends one at a time, so one start's place is enough) */
	report_start.function = function;
	report_start.context = context;
	thread = CreateThread(NULL, REPORT_STACK, report_thread, &report_start, 0, NULL);
	if (!thread)
		return 0;
	xbox_memory_heap_allocation("xbox_reports.c (thread stack)", 0, REPORT_STACK, thread);
	CloseHandle(thread);
	return 1;
}

static void report_sleep(unsigned long milliseconds)
{
	Sleep(milliseconds);
}

static int report_memory_ok(unsigned long bytes)
{
	return xbox_memory_free() >= bytes + REPORT_STACK + REPORT_RESERVE;
}

static int report_player_key(unsigned char *key)
{
	return xbox_player_key_from_file(XBOX_CLAIM_KEY_FILE, key);
}

static void report_log(const char *text)
{
	platform_log("%s", text);
}

static const struct console_reports_port xbox_reports_port =
{
	XBOX_CLAIM_PLATFORM,
	"Warthog " HALO_VERSION,
	report_request,
	report_start_thread,
	report_sleep,
	report_memory_ok,
	report_player_key,
	report_log,
};

void console_reports_port_start(void)
{
	/* (the link's state, which a report signed by the console's key needs) */
	xbox_console_link_start();
	console_reports_initialize(&xbox_reports_port);
}
