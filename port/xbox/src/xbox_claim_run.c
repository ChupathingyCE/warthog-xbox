/*
XBOX_CLAIM_RUN.C

The signed platform claim on the console (xbox_claim.h): once per boot,
after the first online game has ended, on a short-lived thread (its stack
is given back when it ends). With no identity file it sends nothing. Any
failure is one line in debug.txt; there is no retry.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_claim.h"
#include "../include/xbox_game_list.h"
#include "../include/xbox_stack.h"

ULONG __cdecl DbgPrint(PCSTR format, ...);
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);
/* xbox_port.c's: safe from any thread, reaches debug.txt from the main loop */
void platform_log(const char *format, ...);
extern unsigned char transport_initialized;

#define CLAIM_TIMEOUT 15000
#define CLAIM_STACK (32 * 1024)
#define CLAIM_RESPONSE_SIZE 2048

struct claim_site
{
	unsigned long address;
	unsigned short port;
	char host[96];
	char buffer[CLAIM_RESPONSE_SIZE];
	struct game_list_response response;
};

static int claim_transport(void *context, const char *path, const char *body, int *status, const char **reply,
	unsigned long *reply_size)
{
	struct claim_site *site = context;
	unsigned long received;

	if (game_list_request(site->address, site->port, site->host, path, body, site->buffer, sizeof(site->buffer),
		&received, CLAIM_TIMEOUT) != GAME_LIST_OK)
		return 0;
	if (game_list_parse_response(site->buffer, received, &site->response) != GAME_LIST_OK)
		return 0;
	*status = site->response.status;
	*reply = site->response.body;
	*reply_size = site->response.body_size;
	return 1;
}

static unsigned long __stdcall claim_thread_body(void *parameter)
{
	struct claim_site site;
	int result;

	(void)parameter;
	if (!transport_initialized)
	{
		platform_log("claim: the network is not up; no platform claim");
		return 0;
	}
	/* (no identity file: nothing is asked of the site at all) */
	{
		FILE *file = fopen(XBOX_CLAIM_KEY_FILE, "rb");

		if (!file)
			return 0;
		fclose(file);
	}
	if (!xbox_game_list_server(&site.address, site.host, sizeof(site.host), &site.port))
	{
		platform_log("claim: the site did not resolve; no platform claim");
		return 0;
	}
	result = xbox_claim_run(XBOX_CLAIM_KEY_FILE, XBOX_CLAIM_PLATFORM, XBOX_CLAIM_BUILD, claim_transport, &site);
	if (result == XBOX_CLAIM_OK)
		platform_log("claim: the platform claim was taken");
	else if (result != XBOX_CLAIM_NO_IDENTITY)
		platform_log("claim: no platform claim (%s)", xbox_claim_result_string(result));
	return 0;
}

XBOX_STACK_THREAD(claim_thread, "claim", CLAIM_STACK, claim_thread_body)

void xbox_claim_game_ended(void)
{
	static int started;
	HANDLE thread;

	if (started)
		return;
	started = 1;
	thread = CreateThread(NULL, CLAIM_STACK, claim_thread, NULL, 0, NULL);
	if (!thread)
	{
		platform_log("claim: no thread for the platform claim");
		return;
	}
	xbox_memory_heap_allocation("xbox_claim_run.c (thread stack)", 0, CLAIM_STACK, thread);
	/* (below the game's thread: a TLS handshake, hundreds of ms of the
	processor's, would hitch the frames) */
	SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
	CloseHandle(thread);
}
