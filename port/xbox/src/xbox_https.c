/*
XBOX_HTTPS.C

HTTPS on the console (xbox_https.h): the game's shared client
(chupathingyce/port/console/https, Mbed TLS) over XNet's Winsock, the hosts
looked up with XNet's DNS (game_list_resolve), random numbers from
XNetRandom, and a critical section so that one request runs at a time. The
game list (xbox_game_list.c), the signed legacy table (the game's delta.c)
and the console link call through here, each on a thread of its own.

The log names the host and what went wrong, never an address.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_game_list.h"
#include "../include/xbox_https.h"
#include "port/console/https/console_https.h"

void platform_log(const char *format, ...);
/* the game's (port/linux/src/delta.c, HALO_CONSOLE_HTTPS) */
void delta_legacy_start(void);
int delta_legacy_refresh(void);

/* the signed table again this often, and this long after a failure, as the
desktop builds (delta.c's DELTA_REFRESH_INTERVAL and DELTA_RETRY_INTERVAL) */
#define TABLE_REFRESH (4ul * 60 * 60 * 1000)
#define TABLE_RETRY (30ul * 60 * 1000)

static CRITICAL_SECTION https_lock;
static volatile long started;
static volatile long table_busy;
static unsigned long table_next;
static int table_asked;

/* until socket is ready to read (or write), within timeout_ms: 1 ready */
static int wait_ready(SOCKET socket_value, int write, unsigned long timeout_ms)
{
	fd_set set;
	struct timeval wait;

	FD_ZERO(&set);
	FD_SET(socket_value, &set);
	wait.tv_sec = (long)(timeout_ms / 1000);
	wait.tv_usec = (long)(timeout_ms % 1000) * 1000;
	return select(0, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

static int https_connect(const char *host, unsigned short port, unsigned long timeout_ms,
	console_https_socket *out)
{
	struct sockaddr_in target;
	unsigned long address;
	SOCKET socket_value;

	(void)timeout_ms;
	if (game_list_resolve(host, &address) != GAME_LIST_OK)
	{
		platform_log("https: %s did not resolve", host);
		return 0;
	}
	socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_value == INVALID_SOCKET)
		return 0;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(port);
	memcpy(&target.sin_addr, &address, 4);
	/* (a blocking connect, as the plain list's: the stack's own retries end
	it) */
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0)
	{
		platform_log("https: could not connect to %s (%d)", host, WSAGetLastError());
		closesocket(socket_value);
		return 0;
	}
	*out = (console_https_socket)socket_value;
	return 1;
}

static int https_send(console_https_socket handle, const unsigned char *data, int size, unsigned long timeout_ms)
{
	SOCKET socket_value = (SOCKET)handle;
	int count;

	if (!wait_ready(socket_value, 1, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = send(socket_value, (const char *)data, size, 0);
	return count > 0 ? count : -1;
}

static int https_receive(console_https_socket handle, unsigned char *buffer, int size, unsigned long timeout_ms)
{
	SOCKET socket_value = (SOCKET)handle;
	int count;

	if (!wait_ready(socket_value, 0, timeout_ms))
		return CONSOLE_HTTPS_IO_TIMEOUT;
	count = recv(socket_value, (char *)buffer, size, 0);
	if (count == SOCKET_ERROR)
		return WSAGetLastError() == WSAECONNRESET ? 0 : -1;
	return count;
}

static void https_close(console_https_socket handle)
{
	closesocket((SOCKET)handle);
}

/* XNet's random numbers (the console's own cryptographic source) */
static int https_random(unsigned char *buffer, int size)
{
	return XNetRandom(buffer, (UINT)size) == 0;
}

static unsigned long https_milliseconds(void)
{
	return GetTickCount();
}

static void https_lock_enter(void)
{
	EnterCriticalSection(&https_lock);
}

static void https_lock_leave(void)
{
	LeaveCriticalSection(&https_lock);
}

static void https_log(const char *text)
{
	platform_log("%s", text);
}

static const struct console_https_port xbox_https_port =
{
	https_connect, https_send, https_receive, https_close, https_random, https_milliseconds,
	https_lock_enter, https_lock_leave, https_log, "Warthog/1",
};

void xbox_https_start(void)
{
	/* (the first caller sets it up: the game list's thread, once the
	network is up) */
	long expected = 0;

	if (!__atomic_compare_exchange_n(&started, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return;
	InitializeCriticalSection(&https_lock);
	console_https_initialize(&xbox_https_port);
}

void xbox_https_table_start(void)
{
	static int done;

	/* (the main thread's, once: the cached table, and the game's network
	rules told of its layouts) */
	if (done)
		return;
	done = 1;
	delta_legacy_start();
}

void xbox_https_table_refresh(void)
{
	unsigned long now = GetTickCount();
	long expected = 0;

	if (!console_https_available() || (table_asked && (long)(now - table_next) < 0))
		return;
	/* (one thread at a time: another list thread may be here already) */
	if (!__atomic_compare_exchange_n(&table_busy, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return;
	table_asked = 1;
	table_next = GetTickCount() + (delta_legacy_refresh() ? TABLE_REFRESH : TABLE_RETRY);
	__atomic_store_n(&table_busy, 0, __ATOMIC_SEQ_CST);
}
