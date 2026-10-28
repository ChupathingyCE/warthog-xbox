/*
XBOX_P2P.C

Internet play on the console: the desktop builds' tunnel (port/linux/src/
p2p.c, p2p_signal.c, p2p_crypto.c, compiled as they are: port/xbox/
build.py, port/xbox/p2p) over XNet, as a joiner. This is the platform
layer they call (port/linux/src/posix.h's sockets, threads, settings and
logging), and the stubs of what the console leaves out: hosting's UPnP,
Discord, the server browser's public games (p2p_lobby.c), invites handed
from another copy of the game.

XNet reaches the internet only when it starts insecure
(console.bypass_security in D:\config.toml); without it internet play is off
(network.online false: port/xbox/src/xbox_config.c), as on a desktop with it
turned off.

Sockets: XNet's SOCKET values are handles, not small numbers, and the tunnel
keeps sockets as ints (-1 for none), so each gets a descriptor here, an
index into a table.

Nothing here logs an address: platform_log's lines come from p2p.c, which
writes them through log_address (port/xbox/src/xbox_port.c).
*/

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../p2p/pthread.h"
#include "port/linux/src/posix.h"
#include "port/linux/src/p2p.h"
#include "port/linux/src/port_config.h"

int snprintf(char *buffer, size_t size, const char *format, ...);
int xbox_trace_enabled(void);

/* ---------- settings (port/xbox/src/xbox_config.c) */

/* the internet, only when XNet starts insecure (console.bypass_security,
which network.online follows) */
int xbox_p2p_online(void)
{
	static int known = -1;

	if (known < 0)
		known = config_boolean("network.online");
	return known;
}

const char *platform_data_root(void)
{
	return "d:";
}

/* (the desktop builds' lobby and Discord join, which the console does
without: no rendezvous lobby, no Discord) */
int p2p_lobby_open(void)
{
	return 0;
}

void p2p_discord_set_join(const char *code)
{
	(void)code;
}

/* ---------- threads */

typedef char pthread_mutex_room_assert[sizeof(((pthread_mutex_t *)0)->storage) >= sizeof(CRITICAL_SECTION) ? 1 : -1];

static CRITICAL_SECTION *mutex_section(pthread_mutex_t *mutex)
{
	return (CRITICAL_SECTION *)mutex->storage;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
	(void)attributes;
	InitializeCriticalSection(mutex_section(mutex));
	mutex->state = 2;
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	/* (made by the first to lock it; the others wait for that) */
	while (mutex->state != 2)
	{
		long expected = 0;

		if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		{
			InitializeCriticalSection(mutex_section(mutex));
			__atomic_store_n(&mutex->state, 2, __ATOMIC_SEQ_CST);
		}
		else if (mutex->state != 2)
			Sleep(0);
	}
	EnterCriticalSection(mutex_section(mutex));
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	LeaveCriticalSection(mutex_section(mutex));
	return 0;
}

int pthread_once(pthread_once_t *once, void (*function)(void))
{
	long expected = 0;

	/* (the first caller runs it; the others wait until it has) */
	if (__atomic_compare_exchange_n(once, &expected, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
	{
		function();
		__atomic_store_n(once, 2, __ATOMIC_SEQ_CST);
		return 0;
	}
	while (__atomic_load_n(once, __ATOMIC_SEQ_CST) != 2)
		Sleep(0);
	return 0;
}

struct thread_start
{
	void *(*start)(void *);
	void *argument;
};

#define THREAD_STACK_SIZE (64 * 1024)
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address);

static struct thread_start thread_starts[4];
static long thread_start_next;

static DWORD WINAPI thread_main(LPVOID parameter)
{
	struct thread_start *start = (struct thread_start *)parameter;

	start->start(start->argument);
	return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *), void *argument)
{
	/* (internet play starts its one thread once: a few slots do) */
	long index = __atomic_fetch_add(&thread_start_next, 1, __ATOMIC_SEQ_CST);
	struct thread_start *slot;
	HANDLE handle;

	(void)attributes;
	if (index < 0 || index >= (long)(sizeof(thread_starts) / sizeof(thread_starts[0])))
		return -1;
	slot = &thread_starts[index];
	slot->start = start;
	slot->argument = argument;
	/* (64 KB: the tunnel's deepest chain, from p2p_thread to a send through
	the SDK's sockets or a key exchange, is some 20 KB by the frames
	-fstack-usage gives, the largest 3.6 KB; the stack is committed memory a
	64 MB console is short of, and is given back when the thread ends) */
	handle = CreateThread(NULL, THREAD_STACK_SIZE, thread_main, slot, 0, NULL);
	if (!handle)
		return -1;
	xbox_memory_heap_allocation("xbox_p2p.c (thread stack)", index, THREAD_STACK_SIZE, handle);
	*thread = (pthread_t)handle;
	return 0;
}

int pthread_detach(pthread_t thread)
{
	CloseHandle((HANDLE)thread);
	return 0;
}

/* ---------- the threads' end (port/linux/src/thread_stop.h)

The console never asks for it: the game ends by rebooting, and the threads
run until then, as on the desktop builds. So no thread is counted, the end
is never requested, and a sleep is the whole sleep. */

int thread_stop_create(const char *name, void *(*main)(void *), void *argument)
{
	pthread_t thread;

	(void)name;
	if (pthread_create(&thread, NULL, main, argument) != 0)
		return -1;
	pthread_detach(thread);
	return 0;
}

int thread_stop_enter(const char *name, int waited)
{
	(void)name;
	(void)waited;
	return -1;
}

void thread_stop_leave(int slot)
{
	(void)slot;
}

int thread_stop_requested(void)
{
	return 0;
}

int thread_stop_sleep(unsigned long milliseconds)
{
	Sleep(milliseconds);
	return 0;
}

void thread_stop_all(unsigned long milliseconds)
{
	(void)milliseconds;
}

/* ---------- sockets */

enum
{
	MAXIMUM_DESCRIPTORS = 64,
};

static SOCKET descriptors[MAXIMUM_DESCRIPTORS];
static pthread_mutex_t descriptor_lock = PTHREAD_MUTEX_INITIALIZER;
static int descriptors_ready;

static SOCKET to_socket(int descriptor)
{
	if (descriptor < 0 || descriptor >= MAXIMUM_DESCRIPTORS)
		return INVALID_SOCKET;
	return descriptors[descriptor];
}

/* this console's own address for loopback traffic: a stand-in's traffic
from the game is the tunnel's to take (p2p.c checks it came from 127.0.0.1) */
static void loopback_source(void *address, const int *address_length)
{
	struct sockaddr_in *in = (struct sockaddr_in *)address;
	XNADDR xnaddr;

	if (!address || !address_length || *address_length < (int)sizeof(*in) || in->sin_family != AF_INET)
		return;
	if (XNetGetTitleXnAddr(&xnaddr) != XNET_GET_XNADDR_PENDING && xnaddr.ina.s_addr &&
		in->sin_addr.s_addr == xnaddr.ina.s_addr)
	{
		in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	}
}

int posix_socket_last_error(void)
{
	return WSAGetLastError();
}

void platform_log(const char *format, ...);

/* a socket call of the tunnel's that failed (not one that would block): a
few logged, then one in ten seconds */
static int socket_failed(const char *call)
{
	static unsigned long logged_time;
	static long logged, since;
	int error = WSAGetLastError();
	unsigned long now = GetTickCount();

	if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)
		return -1;
	since++;
	if (logged < 8 || now - logged_time >= 10000)
	{
		platform_log("tunnel: %s failed (WSA error %d; %ld failure(s) since the last told)", call, error, since);
		logged++;
		logged_time = now;
		since = 0;
	}
	WSASetLastError(error);
	return -1;
}

static int new_descriptor(SOCKET socket)
{
	int descriptor;

	pthread_mutex_lock(&descriptor_lock);
	if (!descriptors_ready)
	{
		for (descriptor = 0; descriptor < MAXIMUM_DESCRIPTORS; descriptor++)
			descriptors[descriptor] = INVALID_SOCKET;
		descriptors_ready = 1;
	}
	for (descriptor = 0; descriptor < MAXIMUM_DESCRIPTORS; descriptor++)
	{
		if (descriptors[descriptor] == INVALID_SOCKET)
		{
			descriptors[descriptor] = socket;
			pthread_mutex_unlock(&descriptor_lock);
			return descriptor;
		}
	}
	pthread_mutex_unlock(&descriptor_lock);
	closesocket(socket);
	WSASetLastError(WSAEMFILE);
	return -1;
}

int posix_socket(int family, int type, int protocol)
{
	SOCKET socket_handle = socket(family, type, protocol);

	if (socket_handle == INVALID_SOCKET)
		return socket_failed("socket");
	return new_descriptor(socket_handle);
}

int posix_socket_close(int descriptor)
{
	SOCKET socket_handle = to_socket(descriptor);

	if (socket_handle == INVALID_SOCKET)
		return -1;
	pthread_mutex_lock(&descriptor_lock);
	descriptors[descriptor] = INVALID_SOCKET;
	pthread_mutex_unlock(&descriptor_lock);
	return closesocket(socket_handle) == 0 ? 0 : -1;
}

int posix_socket_bind(int descriptor, const void *address, int address_length)
{
	SOCKET socket_handle = to_socket(descriptor);
	struct sockaddr_in any;

	if (bind(socket_handle, (const struct sockaddr *)address, address_length) == 0)
		return 0;
	/* (XNet may bind only to every address: a stand-in meant for 127.0.0.1
	then takes any, and p2p.c takes only the game's traffic from it) */
	if (address && address_length >= (int)sizeof(any) &&
		((const struct sockaddr_in *)address)->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
	{
		static int told;

		memcpy(&any, address, sizeof(any));
		any.sin_addr.s_addr = INADDR_ANY;
		if (bind(socket_handle, (const struct sockaddr *)&any, sizeof(any)) == 0)
		{
			if (!told)
				platform_log("Internet play: XNet binds no socket to 127.0.0.1 alone; the stand-ins take every address");
			told = 1;
			return 0;
		}
	}
	return socket_failed("bind");
}

int posix_socket_connect(int descriptor, const void *address, int address_length)
{
	return connect(to_socket(descriptor), (const struct sockaddr *)address, address_length) == 0 ? 0 :
		socket_failed("connect");
}

int posix_socket_listen(int descriptor, int backlog)
{
	return listen(to_socket(descriptor), backlog) == 0 ? 0 : socket_failed("listen");
}

int posix_socket_accept(int descriptor, void *address, int *address_length)
{
	SOCKET accepted = accept(to_socket(descriptor), (struct sockaddr *)address, address_length);

	if (accepted == INVALID_SOCKET)
		return socket_failed("accept");
	loopback_source(address, address_length);
	{
		static int logged;

		if (xbox_trace_enabled() && logged++ < 6)
			platform_log("tunnel: a stand-in took a connection from %s",
				address && ((struct sockaddr_in *)address)->sin_addr.s_addr == htonl(INADDR_LOOPBACK) ?
					"this console (the game's)" : "elsewhere (not the game's: refused)");
	}
	return new_descriptor(accepted);
}

int posix_socket_send(int descriptor, const void *buffer, int length, int flags)
{
	int result = send(to_socket(descriptor), (const char *)buffer, length, flags);

	return result == SOCKET_ERROR ? -1 : result;
}

/* xbox_winsock_hooks.c's: a stand-in's datagram for the game, in memory */
int xbox_winsock_deliver(unsigned short port, unsigned short from_port, const void *data, int size);

int posix_socket_sendto(int descriptor, const void *buffer, int length, int flags, const void *address,
	int address_length)
{
	const struct sockaddr_in *to = (const struct sockaddr_in *)address;
	int result;

	/* a stand-in's datagram to the game's socket on 127.0.0.1: handed over
	without XNet's loopback (xbox_winsock_hooks.c) */
	if (to && address_length >= (int)sizeof(*to) && to->sin_family == AF_INET &&
		to->sin_addr.s_addr == htonl(INADDR_LOOPBACK))
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);

		if (getsockname(to_socket(descriptor), (struct sockaddr *)&from, &from_length) == 0 &&
			xbox_winsock_deliver(to->sin_port, from.sin_port, buffer, length))
		{
			return length;
		}
	}
	result = sendto(to_socket(descriptor), (const char *)buffer, length, flags, (const struct sockaddr *)address,
		address_length);
	/* (XNet refuses a socket another thread is using, its select's
	included: a datagram lost so, which p2p.c's P2P_SEND_FROM_THREAD keeps
	from happening, is told) */
	if (result == SOCKET_ERROR && WSAGetLastError() == WSAEINPROGRESS)
	{
		static unsigned long told_time;
		static long since;
		unsigned long now = GetTickCount();

		since++;
		if (!told_time || now - told_time >= 10000)
		{
			platform_log("tunnel: %ld datagram(s) not sent: another thread was using the socket", since);
			told_time = now ? now : 1;
			since = 0;
		}
		WSASetLastError(WSAEINPROGRESS);
		return -1;
	}

	return result == SOCKET_ERROR ? socket_failed("sendto") : result;
}

int posix_socket_recv(int descriptor, void *buffer, int length, int flags)
{
	int result = recv(to_socket(descriptor), (char *)buffer, length, flags);

	return result == SOCKET_ERROR ? -1 : result;
}

int posix_socket_recvfrom(int descriptor, void *buffer, int length, int flags, void *address, int *address_length)
{
	int result = recvfrom(to_socket(descriptor), (char *)buffer, length, flags, (struct sockaddr *)address,
		address_length);

	if (result == SOCKET_ERROR)
		return -1;
	loopback_source(address, address_length);
	return result;
}

int posix_socket_shutdown(int descriptor, int how)
{
	return shutdown(to_socket(descriptor), how) == 0 ? 0 : -1;
}

int posix_socket_set_nonblocking(int descriptor, int nonblocking)
{
	u_long value = nonblocking ? 1 : 0;

	return ioctlsocket(to_socket(descriptor), FIONBIO, &value) == 0 ? 0 : socket_failed("ioctlsocket");
}

int posix_socket_bytes_available(int descriptor, posix_ulong *count)
{
	u_long value = 0;

	if (ioctlsocket(to_socket(descriptor), FIONREAD, &value) != 0)
		return -1;
	*count = value;
	return 0;
}

int posix_socket_set_nodelay(int descriptor)
{
	BOOL value = TRUE;

	return setsockopt(to_socket(descriptor), IPPROTO_TCP, TCP_NODELAY, (const char *)&value, sizeof(value)) == 0 ? 0 : -1;
}

int posix_socket_setsockopt(int descriptor, int level, int name, const void *value, int length)
{
	return setsockopt(to_socket(descriptor), level, name, (const char *)value, length) == 0 ? 0 : -1;
}

int posix_socket_getsockopt(int descriptor, int level, int name, void *value, int *length)
{
	return getsockopt(to_socket(descriptor), level, name, (char *)value, length) == 0 ? 0 : -1;
}

int posix_socket_getsockname(int descriptor, void *address, int *address_length)
{
	return getsockname(to_socket(descriptor), (struct sockaddr *)address, address_length) == 0 ? 0 :
		socket_failed("getsockname");
}

int posix_socket_getpeername(int descriptor, void *address, int *address_length)
{
	return getpeername(to_socket(descriptor), (struct sockaddr *)address, address_length) == 0 ? 0 : -1;
}

/* the list's descriptors that are in the set, kept in order */
static void select_keep(int *list, int *count, const fd_set *set)
{
	int kept = 0;
	int index;

	for (index = 0; index < *count; index++)
	{
		if (FD_ISSET(to_socket(list[index]), (fd_set *)set))
			list[kept++] = list[index];
	}
	*count = kept;
}

static void select_fill(fd_set *set, const int *list, int count)
{
	int index;

	FD_ZERO(set);
	for (index = 0; list && index < count && set->fd_count < FD_SETSIZE; index++)
	{
		if (to_socket(list[index]) != INVALID_SOCKET)
			FD_SET(to_socket(list[index]), set);
	}
}

int posix_socket_select(int *read, int *read_count, int *write, int *write_count, int *error_list,
	int *error_count, posix_long timeout_seconds, posix_long timeout_microseconds, int infinite)
{
	/* (large: the tunnel's lists hold up to 256: port/xbox/p2p/platform.h) */
	static fd_set read_set, write_set, error_set;
	struct timeval timeout;
	int result;

	select_fill(&read_set, read, read_count ? *read_count : 0);
	select_fill(&write_set, write, write_count ? *write_count : 0);
	select_fill(&error_set, error_list, error_count ? *error_count : 0);
	timeout.tv_sec = timeout_seconds;
	timeout.tv_usec = timeout_microseconds;
	/* (Winsock fails a select of no sockets: a plain wait) */
	if (!read_set.fd_count && !write_set.fd_count && !error_set.fd_count)
	{
		if (!infinite)
			Sleep((DWORD)(timeout_seconds * 1000 + timeout_microseconds / 1000));
		if (read_count)
			*read_count = 0;
		if (write_count)
			*write_count = 0;
		if (error_count)
			*error_count = 0;
		return 0;
	}
	result = select(0, read_set.fd_count ? &read_set : NULL, write_set.fd_count ? &write_set : NULL,
		error_set.fd_count ? &error_set : NULL, infinite ? NULL : &timeout);
	if (result == SOCKET_ERROR)
		return -1;
	if (read_count)
		select_keep(read, read_count, &read_set);
	if (write_count)
		select_keep(write, write_count, &write_set);
	if (error_count)
		select_keep(error_list, error_count, &error_set);
	return result;
}

posix_ulong posix_local_ipv4_address(void)
{
	XNADDR xnaddr;

	if (XNetGetTitleXnAddr(&xnaddr) == XNET_GET_XNADDR_PENDING)
		return 0;
	return xnaddr.ina.s_addr;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	/* (XNet's own: its keys' source) */
	if (XNetRandom((BYTE *)buffer, (UINT)size) != 0)
	{
		platform_log("Internet play: XNetRandom failed; no random bytes");
		for (;;)
			Sleep(1000);
	}
}

/* a dotted quad, in network byte order; 0 if it is not one */
static unsigned long dotted_quad(const char *host)
{
	unsigned long value = 0;
	int part;

	for (part = 0; part < 4; part++)
	{
		unsigned long number = 0;
		int digits = 0;

		while (*host >= '0' && *host <= '9' && digits < 3)
		{
			number = number * 10 + (unsigned long)(*host++ - '0');
			digits++;
		}
		if (!digits || number > 255 || (part < 3 && *host++ != '.'))
			return 0;
		value = value << 8 | number;
	}
	return *host ? 0 : htonl(value);
}

/* names looked up before: internet play's thread also carries the game's
traffic, so a lookup again (a broker's after it failed) must not hold it
for XNet DNS's seconds while a game is on */
enum
{
	RESOLVED_HOSTS = 8,
	RESOLVED_HOST_SIZE = 64,
};

static struct
{
	char host[RESOLVED_HOST_SIZE];
	unsigned long address;
} resolved[RESOLVED_HOSTS];

static unsigned long resolved_address(const char *host)
{
	int index;

	for (index = 0; index < RESOLVED_HOSTS; index++)
	{
		if (resolved[index].address && !strcmp(resolved[index].host, host))
			return resolved[index].address;
	}
	return 0;
}

static void resolved_keep(const char *host, unsigned long address)
{
	int index, free_index = -1;

	if (strlen(host) >= RESOLVED_HOST_SIZE)
		return;
	for (index = 0; index < RESOLVED_HOSTS; index++)
	{
		if (!strcmp(resolved[index].host, host))
		{
			resolved[index].address = address;
			return;
		}
		if (free_index < 0 && !resolved[index].host[0])
			free_index = index;
	}
	if (free_index >= 0)
	{
		strcpy(resolved[free_index].host, host);
		resolved[free_index].address = address;
	}
}

posix_ulong posix_resolve_ipv4(const char *host)
{
	unsigned long address = dotted_quad(host), started;
	XNDNS *dns = NULL;
	WSAEVENT event;

	if (address)
		return address;
	address = resolved_address(host);
	if (address)
		return address;
	event = WSACreateEvent();
	if (event == WSA_INVALID_EVENT)
		return 0;
	started = GetTickCount();
	if (XNetDnsLookup(host, event, &dns) == 0 && dns)
	{
		WaitForSingleObject(event, 20000);
		if (dns->iStatus == 0 && dns->cina > 0)
			address = dns->aina[0].s_addr;
		XNetDnsRelease(dns);
	}
	WSACloseEvent(event);
	if (GetTickCount() - started >= 2000)
		platform_log("Internet play: looking up %s took %lus (%s)", host, (GetTickCount() - started) / 1000,
			address ? "found" : "not found");
	if (address)
		resolved_keep(host, address);
	return address;
}

/* ---------- the rest of posix.h's that internet play calls: the console
has none of these */

int posix_upnp_forward_udp(unsigned short port, unsigned short preferred_port, posix_ulong *external_address,
	unsigned short *external_port, char *error_text, int error_size)
{
	(void)port;
	(void)preferred_port;
	(void)external_address;
	(void)external_port;
	snprintf(error_text, (size_t)error_size, "no UPnP on the console");
	return 0;
}

void posix_upnp_stop_forwarding_udp(unsigned short external_port)
{
	(void)external_port;
}

int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
}

/* (p2p.c's _WIN32 branch: what the machine is known by; none) */
int posix_hardware_id_source(char *text, int size)
{
	(void)text;
	(void)size;
	return 0;
}

/* ---------- what the console leaves out of internet play */

/* Discord's (p2p_discord.c) */
void p2p_discord_update(void)
{
}

void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = 0;
	if (name_size > 0)
		name[0] = 0;
}

void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count)
{
	(void)secret;
	(void)player_count;
	(void)maximum_player_count;
}

/* the server browser's public games (p2p_lobby.c): none listed, none browsed */
void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count)
{
	(void)token;
	(void)player_count;
	(void)maximum_player_count;
}

int p2p_lobby_listed(void)
{
	return 0;
}

int p2p_lobby_browsing(void)
{
	return 0;
}

void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	(void)hash_text;
	(void)payload;
	(void)size;
	(void)retained;
}

void p2p_lobby_query_heard(void)
{
}

void p2p_lobby_quit(void)
{
}

/* (the topic p2p_lobby.c's is: P2P_LOBBY_SLOT_PREFIX and the hash in hex) */
void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	static const char digits[] = "0123456789abcdef";
	int length = snprintf(topic, (size_t)size, "hceu/3/lobby/s/");
	int index;

	for (index = 0; index < 16 && length + 2 < size; index++)
	{
		topic[length++] = digits[key_hash[index] >> 4];
		topic[length++] = digits[key_hash[index] & 15];
	}
	if (length < size)
		topic[length] = 0;
}

/* ---------- what becomes of the peers' datagrams for the game (p2p.c's
datagram_received, P2P_TRACE_DATAGRAMS), with console.trace: a few logged,
then one in ten seconds, each outcome on its own */

void p2p_trace_datagram(unsigned short source_port, unsigned short port, int size, int result)
{
	static const char *const outcomes[] =
	{
		"no stand-in for it",
		"not a port of the game's",
		"not sent to the game",
		"sent to the game",
	};
	static struct
	{
		long count;
		long logged;
		unsigned long time;
	} traced[4];
	int outcome = result + 2;
	unsigned long now = GetTickCount();

	if (outcome < 0 || outcome > 3 || !xbox_trace_enabled())
		return;
	traced[outcome].count++;
	if (traced[outcome].logged < 4 || now - traced[outcome].time >= 10000)
	{
		platform_log("tunnel: %ld datagram(s) from the peer's port %u for the game's port %u (%d bytes): %s",
			traced[outcome].count, (unsigned)ntohs(source_port), (unsigned)ntohs(port), size, outcomes[outcome]);
		traced[outcome].count = 0;
		traced[outcome].logged++;
		traced[outcome].time = now;
	}
}

/* each packet the tunnel opened from a peer: counted by type, logged every
10 seconds with console.trace (types: 1 ping, 2 pong, 3 datagram, 4 stream,
others by number) */
void p2p_trace_packet(int type, int size)
{
	static long counts[8];
	static long bytes;
	static unsigned long time;
	unsigned long now = GetTickCount();

	if (!xbox_trace_enabled())
		return;
	counts[type >= 0 && type < 8 ? type : 7]++;
	bytes += size;
	if (!time)
		time = now;
	if (now - time >= 10000)
	{
		platform_log("tunnel: in the last %lu s, from peers: types 0-7 %ld %ld %ld %ld %ld %ld %ld %ld (%ld bytes)",
			(now - time) / 1000, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
			bytes);
		memset(counts, 0, sizeof(counts));
		bytes = 0;
		time = now;
	}
}

