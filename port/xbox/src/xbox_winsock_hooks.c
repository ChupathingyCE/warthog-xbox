/*
XBOX_WINSOCK_HOOKS.C

The game's Winsock calls on the console, through internet play's tunnel
(port/xbox/src/xbox_p2p.c) as the desktop builds' go through theirs
(port/linux/src/xnet.c, whose logic this follows): halo_xbox_prefix.h
renames the calls the game makes to these, which call XNet's own.

- A peer of the tunnel has a virtual address in 100.64.0.0/10, which
  XNetXnAddrToInAddr gives for its XNADDR (whose abEnet is its identifier).
- The game's datagrams to a peer go onto the tunnel at once
  (p2p_send_datagram); its broadcasts (the system link search) go to every
  peer too. Its connections to a peer go to the tunnel's stand-in for it
  (p2p_outgoing), and what comes from a stand-in comes from the peer
  (p2p_incoming).
- The tunnel is told the game's sockets' ports, which alone peers reach
  (p2p_socket_port), and when one closes.
- A peer's datagram for the game goes from its stand-in (a UDP socket of
  the tunnel's) to the game's socket. On the desktop that is a datagram
  over 127.0.0.1; here the stand-in's send to a game socket's port on
  127.0.0.1 is handed over in memory (xbox_winsock_deliver, from
  xbox_p2p.c's posix_socket_sendto), and the game's select and recvfrom
  see it as that datagram, from the stand-in's port on 127.0.0.1 (which
  p2p_incoming turns into the peer's). XNet's loopback is then not needed
  for the game's datagrams (it is for its connections, as the game's own
  host-joins-itself connection always used).
- With console.trace (xbox_port.c's xbox_trace_enabled), every hop is
  logged, a few times then once in a while ("tunnel:" lines): the system
  link search going to the peers, the peers' datagrams handed to the game,
  the game reading them. Without it, only failures are.

Without internet play (console.bypass_security off, or the tunnel), every
p2p_ call answers "not a peer's", and these are XNet's calls as they were.
Nothing here logs an address.
*/

/* (the game's fd_set is the SDK's, FD_SETSIZE 64, as here: the console's
prefix does not set it) */
#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "../p2p/pthread.h"
#include "port/linux/src/p2p.h"

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
int xbox_trace_enabled(void);
/* (xbox_port.c's: another machine's Ethernet address as a tag) */
const char *log_ethernet(const unsigned char *bytes, char *text, int size);

/* (the socket types the game made, by socket: XNet's SO_TYPE is not relied on) */
enum
{
	MAXIMUM_TRACKED_SOCKETS = 64,
};

static struct
{
	SOCKET socket;
	int type;
	/* its local port (network byte order), once it has one */
	unsigned short port;
} tracked[MAXIMUM_TRACKED_SOCKETS];

/* ---------- the log: each kind of hop logged its first few times, then
every 10 seconds with how many since */

enum
{
	_hop_search,
	_hop_search_unbound,
	_hop_delivered,
	_hop_dropped,
	_hop_read,
	_hop_select,
	_hop_not_game,
	NUMBER_OF_HOPS
};

static struct
{
	long count;
	long logged;
	unsigned long time;
} hops[NUMBER_OF_HOPS];

/* whether to log this hop now (only with console.trace); *since: how many
there were since the last */
static int hop_log(int hop, long *since)
{
	unsigned long now = GetTickCount();

	hops[hop].count++;
	if (!xbox_trace_enabled())
		return 0;
	if (hops[hop].logged < 3 || now - hops[hop].time >= 10000)
	{
		*since = hops[hop].count;
		hops[hop].count = 0;
		hops[hop].logged++;
		hops[hop].time = now;
		return 1;
	}
	return 0;
}

static int is_virtual(unsigned long network_address)
{
	return (ntohl(network_address) & 0xFFC00000UL) == 0x64400000UL;
}

static void track(SOCKET socket, int type)
{
	int index;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (!tracked[index].socket || tracked[index].socket == INVALID_SOCKET)
		{
			tracked[index].socket = socket;
			tracked[index].type = type;
			return;
		}
	}
}

static int socket_type(SOCKET socket)
{
	int index;
	int type = SOCK_DGRAM;
	int length = sizeof(type);

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			return tracked[index].type;
	}
	getsockopt(socket, SOL_SOCKET, SO_TYPE, (char *)&type, &length);
	return type;
}

static void untrack(SOCKET socket)
{
	int index;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			tracked[index].socket = 0;
	}
}

/* the local port the socket is bound to (network byte order), or 0 */
static unsigned short socket_port(SOCKET socket)
{
	struct sockaddr_in bound;
	int length = sizeof(bound);

	if (getsockname(socket, (struct sockaddr *)&bound, &length) != 0 || bound.sin_family != AF_INET)
		return 0;
	return bound.sin_port;
}

/* tells internet play the local port the game's socket has */
static void note_socket_port(SOCKET socket, int listening)
{
	unsigned short port = socket_port(socket);
	int index;

	if (!port)
		return;
	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			tracked[index].port = port;
	}
	p2p_socket_port((int)socket, socket_type(socket) == SOCK_STREAM, listening, port);
}

/* ---------- datagrams handed to the game in memory */

enum
{
	/* (the game reads its sockets every frame, at some 30 datagrams a second
	from a host: 16 hold half a second of them, and a full inbox drops the
	next as a full socket does) */
	INBOX_DATAGRAMS = 16,
	INBOX_DATAGRAM_SIZE = 1400,
};

static pthread_mutex_t inbox_lock = PTHREAD_MUTEX_INITIALIZER;
static struct
{
	/* network byte order: the game socket's port, and the stand-in's */
	unsigned short port;
	unsigned short from_port;
	short size;
	char data[INBOX_DATAGRAM_SIZE];
} inbox[INBOX_DATAGRAMS];
static int inbox_first, inbox_count;

/* a game datagram socket's, of this local port (network byte order) */
static int game_datagram_port(unsigned short port)
{
	int index;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket && tracked[index].socket != INVALID_SOCKET && tracked[index].port == port &&
			tracked[index].type == SOCK_DGRAM)
		{
			return 1;
		}
	}
	return 0;
}

/* the tunnel's stand-in (from_port) sends the game's socket of port a
datagram on 127.0.0.1 (xbox_p2p.c): 1 if it is a game socket's port and
the datagram is queued for it (or dropped, the queue full, as a datagram
may be); 0 if it is not one (the send goes to XNet) */
int xbox_winsock_deliver(unsigned short port, unsigned short from_port, const void *data, int size)
{
	long since;

	if (!game_datagram_port(port))
	{
		/* (the game's datagram sockets' ports, for the log) */
		char ports[96];
		int index, used = 0;

		ports[0] = 0;
		for (index = 0; index < MAXIMUM_TRACKED_SOCKETS && used < (int)sizeof(ports) - 8; index++)
		{
			if (tracked[index].socket && tracked[index].socket != INVALID_SOCKET && tracked[index].type == SOCK_DGRAM)
				used += _snprintf(ports + used, sizeof(ports) - used, " %u", (unsigned)ntohs(tracked[index].port));
		}
		ports[sizeof(ports) - 1] = 0;
		if (hop_log(_hop_not_game, &since))
			platform_log("tunnel: %ld stand-in datagram(s) to 127.0.0.1:%u, not a game datagram socket's port (they:%s); "
				"sent through XNet", since, (unsigned)ntohs(port), ports);
		return 0;
	}
	pthread_mutex_lock(&inbox_lock);
	if (inbox_count < INBOX_DATAGRAMS && size >= 0 && size <= INBOX_DATAGRAM_SIZE)
	{
		int slot = (inbox_first + inbox_count++) % INBOX_DATAGRAMS;

		inbox[slot].port = port;
		inbox[slot].from_port = from_port;
		inbox[slot].size = (short)size;
		memcpy(inbox[slot].data, data, (size_t)size);
		pthread_mutex_unlock(&inbox_lock);
		if (hop_log(_hop_delivered, &since))
			platform_log("tunnel: %ld peer datagram(s) handed to the game's port %u (this one %d bytes)", since,
				(unsigned)ntohs(port), size);
		return 1;
	}
	pthread_mutex_unlock(&inbox_lock);
	if (hop_log(_hop_dropped, &since))
		platform_log("tunnel: %ld peer datagram(s) for the game's port %u dropped (the game is not reading them)",
			since, (unsigned)ntohs(port));
	return 1;
}

/* the oldest datagram queued for the socket's port, taken: its size, or -1;
*truncated set when it was longer than the buffer (what fits is copied, the
rest dropped, as Winsock does before it fails with WSAEMSGSIZE) */
static int inbox_take(SOCKET socket, char *buffer, int length, unsigned short *from_port, int *truncated)
{
	unsigned short port = 0;
	int index, result = -1;

	*truncated = 0;

	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			port = tracked[index].port;
	}
	if (!port || !inbox_count)
		return -1;
	pthread_mutex_lock(&inbox_lock);
	for (index = 0; index < inbox_count; index++)
	{
		int slot = (inbox_first + index) % INBOX_DATAGRAMS;
		int later;

		if (inbox[slot].port != port)
			continue;
		result = inbox[slot].size < length ? inbox[slot].size : length;
		*truncated = inbox[slot].size > length;
		memcpy(buffer, inbox[slot].data, (size_t)result);
		*from_port = inbox[slot].from_port;
		/* (the front of the ring is taken by moving its start; for one behind
		the front, the ones after it move up one) */
		if (!index)
			inbox_first = (inbox_first + 1) % INBOX_DATAGRAMS;
		else
		{
			for (later = index; later + 1 < inbox_count; later++)
				inbox[(inbox_first + later) % INBOX_DATAGRAMS] = inbox[(inbox_first + later + 1) % INBOX_DATAGRAMS];
		}
		inbox_count--;
		break;
	}
	pthread_mutex_unlock(&inbox_lock);
	return result;
}

/* whether a datagram is queued for the socket */
static int inbox_has(SOCKET socket)
{
	unsigned short port = 0;
	int index, result = 0;

	if (!inbox_count)
		return 0;
	for (index = 0; index < MAXIMUM_TRACKED_SOCKETS; index++)
	{
		if (tracked[index].socket == socket)
			port = tracked[index].port;
	}
	if (!port)
		return 0;
	pthread_mutex_lock(&inbox_lock);
	for (index = 0; index < inbox_count && !result; index++)
		result = inbox[(inbox_first + index) % INBOX_DATAGRAMS].port == port;
	pthread_mutex_unlock(&inbox_lock);
	return result;
}

/* a destination of a peer's goes to its stand-in (1); -1 (WSAEHOSTUNREACH)
if it is a peer's that cannot be reached now; 0 if it is not a peer's */
static int outgoing(int stream, int connecting, const struct sockaddr **address, int address_length,
	struct sockaddr_in *storage)
{
	unsigned long ip;
	unsigned short port;
	int result;

	if (!*address || (*address)->sa_family != AF_INET || address_length < (int)sizeof(*storage))
		return 0;
	ip = ((const struct sockaddr_in *)*address)->sin_addr.s_addr;
	port = ((const struct sockaddr_in *)*address)->sin_port;
	result = p2p_outgoing(stream, connecting, &ip, &port);
	if (result < 0)
		WSASetLastError(WSAEHOSTUNREACH);
	if (result <= 0)
		return result;
	memcpy(storage, *address, sizeof(*storage));
	storage->sin_addr.s_addr = ip;
	storage->sin_port = port;
	*address = (const struct sockaddr *)storage;
	return 1;
}

/* traffic from a stand-in comes from its peer */
static void incoming(int stream, struct sockaddr *address, const int *address_length)
{
	if (address && address_length && *address_length >= (int)sizeof(struct sockaddr_in) &&
		address->sa_family == AF_INET)
	{
		struct sockaddr_in *in = (struct sockaddr_in *)address;
		unsigned long ip = in->sin_addr.s_addr;
		unsigned short port = in->sin_port;
		XNADDR xnaddr;

		/* (XNet may give a loopback datagram this console's own address) */
		if (XNetGetTitleXnAddr(&xnaddr) != XNET_GET_XNADDR_PENDING && xnaddr.ina.s_addr && ip == xnaddr.ina.s_addr)
			ip = htonl(INADDR_LOOPBACK);
		if (p2p_incoming(stream, &ip, &port))
		{
			in->sin_addr.s_addr = ip;
			in->sin_port = port;
		}
	}
}

SOCKET WSAAPI halo_xbox_socket(int family, int type, int protocol)
{
	SOCKET result = socket(family, type, protocol);

	if (result != INVALID_SOCKET)
		track(result, type);
	return result;
}

int WSAAPI halo_xbox_closesocket(SOCKET socket)
{
	int stream = socket_type(socket) == SOCK_STREAM;

	p2p_socket_closed((int)socket, stream ? 0 : socket_port(socket));
	untrack(socket);
	return closesocket(socket);
}

int WSAAPI halo_xbox_bind(SOCKET socket, const struct sockaddr *address, int address_length)
{
	int result = bind(socket, address, address_length);

	if (result == 0)
		note_socket_port(socket, 0);
	return result;
}

int WSAAPI halo_xbox_connect(SOCKET socket, const struct sockaddr *address, int address_length)
{
	struct sockaddr_in target;
	int result;
	int error;

	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target))
	{
		static int logged;
		int virtual_address = is_virtual(((const struct sockaddr_in *)address)->sin_addr.s_addr);
		int redirect = virtual_address ?
			outgoing(socket_type(socket) == SOCK_STREAM, (int)socket, &address, address_length, &target) : 0;

		if (xbox_trace_enabled() && logged++ < 8)
			platform_log("tunnel: the game connects (%s) to port %u: %s",
				socket_type(socket) == SOCK_STREAM ? "stream" : "datagram",
				(unsigned)ntohs(((const struct sockaddr_in *)address)->sin_port),
				!virtual_address ? "not a tunnel peer's address, through XNet" :
					redirect > 0 ? "a tunnel peer's, to its stand-in" :
					redirect < 0 ? "a tunnel peer's, not reachable now" : "a tunnel peer's, but no stand-in");
		if (redirect < 0)
			return SOCKET_ERROR;
	}
	result = connect(socket, address, address_length);
	error = result != 0 ? WSAGetLastError() : 0;
	note_socket_port(socket, 0);
	if (address == (const struct sockaddr *)&target)
	{
		static int logged;

		if (xbox_trace_enabled() && logged++ < 6)
			platform_log("tunnel: the game connects to the host's stand-in (port %u): %s (%d)",
				(unsigned)ntohs(target.sin_port), result == 0 ? "connected" : "under way or failed", error);
	}
	if (result != 0)
		WSASetLastError(error);
	return result;
}

int WSAAPI halo_xbox_listen(SOCKET socket, int backlog)
{
	int result = listen(socket, backlog);

	if (result == 0)
		note_socket_port(socket, 1);
	return result;
}

SOCKET WSAAPI halo_xbox_accept(SOCKET socket, struct sockaddr *address, int *address_length)
{
	SOCKET result = accept(socket, address, address_length);

	if (result != INVALID_SOCKET)
	{
		track(result, SOCK_STREAM);
		incoming(1, address, address_length);
	}
	return result;
}

int WSAAPI halo_xbox_recvfrom(SOCKET socket, char *buffer, int length, int flags, struct sockaddr *address,
	int *address_length)
{
	unsigned short from_port;
	int truncated;
	int result = inbox_take(socket, buffer, length, &from_port, &truncated);

	/* (a peer's datagram, from its stand-in on 127.0.0.1) */
	if (result >= 0)
	{
		long since;

		if (address && address_length && *address_length >= (int)sizeof(struct sockaddr_in))
		{
			struct sockaddr_in *in = (struct sockaddr_in *)address;

			memset(in, 0, sizeof(*in));
			in->sin_family = AF_INET;
			in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			in->sin_port = from_port;
			*address_length = sizeof(*in);
			incoming(0, address, address_length);
			if (hop_log(_hop_read, &since))
				platform_log("tunnel: the game read %ld peer datagram(s) (this one %d bytes, %s a peer's)", since,
					result, is_virtual(in->sin_addr.s_addr) ? "seen as" : "NOT seen as");
		}
		/* (longer than the buffer: Winsock's error, the sender filled in) */
		if (truncated)
		{
			WSASetLastError(WSAEMSGSIZE);
			return SOCKET_ERROR;
		}
		return result;
	}
	result = recvfrom(socket, buffer, length, flags, address, address_length);

	if (result != SOCKET_ERROR)
		incoming(0, address, address_length);
	return result;
}

/* a connected datagram socket's read (the game's endpoints once a game runs:
transport_endpoint_winsock.c reads them with recv, not recvfrom): a peer's
datagram queued for its port is taken first, as recvfrom does; a stream
socket's reads are never the inbox's (a stream can share a datagram's port
number) */
int WSAAPI halo_xbox_recv(SOCKET socket, char *buffer, int length, int flags)
{
	if (socket_type(socket) == SOCK_DGRAM)
	{
		unsigned short from_port;
		int truncated;
		int result = inbox_take(socket, buffer, length, &from_port, &truncated);

		if (result >= 0)
		{
			long since;

			if (hop_log(_hop_read, &since))
				platform_log("tunnel: the game read %ld peer datagram(s) (this one %d bytes, on its connected socket)",
					since, result);
			if (truncated)
			{
				WSASetLastError(WSAEMSGSIZE);
				return SOCKET_ERROR;
			}
			return result;
		}
	}
	return recv(socket, buffer, length, flags);
}

int WSAAPI halo_xbox_getpeername(SOCKET socket, struct sockaddr *address, int *address_length)
{
	int result = getpeername(socket, address, address_length);

	if (result == 0)
		incoming(socket_type(socket) == SOCK_STREAM, address, address_length);
	return result;
}

int WSAAPI halo_xbox_sendto(SOCKET socket, const char *buffer, int length, int flags, const struct sockaddr *address,
	int address_length)
{
	struct sockaddr_in target;
	unsigned short source_port = socket_port(socket);
	int result;
	int error;

	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target) &&
		((const struct sockaddr_in *)address)->sin_addr.s_addr == INADDR_BROADCAST)
	{
		unsigned long targets[16];
		unsigned short ports[16];
		int count;
		int index;

		/* the LAN's broadcast, then every peer's: onto the tunnel at once,
		else through their stand-ins */
		result = sendto(socket, buffer, length, flags, address, address_length);
		error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
		if (!source_port)
		{
			source_port = socket_port(socket);
			if (source_port)
				note_socket_port(socket, 0);
		}
		if (source_port)
		{
			int peers = p2p_broadcast_datagram(source_port, ((const struct sockaddr_in *)address)->sin_port, buffer,
				length);
			long since;

			if (peers > 0 && result == SOCKET_ERROR)
				result = length;
			if (hop_log(_hop_search, &since))
				platform_log("tunnel: %ld broadcast(s) from the game's port %u to port %u: %d peer(s) through the tunnel",
					since, (unsigned)ntohs(source_port), (unsigned)ntohs(((const struct sockaddr_in *)address)->sin_port),
					peers);
		}
		else
		{
			long since;

			if (hop_log(_hop_search_unbound, &since))
				platform_log("tunnel: %ld broadcast(s) from an unbound socket: through the stand-ins", since);
			memcpy(&target, address, sizeof(target));
			count = p2p_broadcast_targets(((const struct sockaddr_in *)address)->sin_port, targets, ports, 16);
			for (index = 0; index < count; index++)
			{
				target.sin_addr.s_addr = targets[index];
				target.sin_port = ports[index];
				if (sendto(socket, buffer, length, flags, (const struct sockaddr *)&target, sizeof(target)) !=
					SOCKET_ERROR && result == SOCKET_ERROR)
				{
					result = length;
				}
			}
		}
		if (result == SOCKET_ERROR)
			WSASetLastError(error);
		return result;
	}
	if (address && address->sa_family == AF_INET && address_length >= (int)sizeof(target) &&
		is_virtual(((const struct sockaddr_in *)address)->sin_addr.s_addr))
	{
		switch (source_port ? p2p_send_datagram(source_port, ((const struct sockaddr_in *)address)->sin_addr.s_addr,
			((const struct sockaddr_in *)address)->sin_port, buffer, length) : 0)
		{
		case 1:
			return length;
		case -1:
			WSASetLastError(WSAEHOSTUNREACH);
			return SOCKET_ERROR;
		}
		if (outgoing(0, -1, &address, address_length, &target) < 0)
			return SOCKET_ERROR;
	}
	result = sendto(socket, buffer, length, flags, address, address_length);
	error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
	if (!source_port)
		note_socket_port(socket, 0);
	if (result == SOCKET_ERROR)
		WSASetLastError(error);
	return result;
}

/* ---------- XNADDR: the game's and the SDK's

The game (port/include/xdk, from its 2002 SDK) has a 12-byte XNADDR:
bSizeOfStruct, bFlags, abEnet[6], ina. It is on the wire, in every
advertisement, as the PC builds have it. The 5933 SDK the console links has
a 36-byte one (ina, inaOnline, wPortOnline, abEnet, abOnline). So the game's
calls that take an XNADDR come here (halo_xbox_prefix.h) and are turned
into the SDK's and back; without this XNetGetTitleXnAddr wrote 36 bytes
into the game's 12 (global_address, and the globals after it), and the
abEnet the game advertised and the one this file compared were others'. */

struct game_xnaddr
{
	unsigned char size;
	unsigned char flags;
	unsigned char enet[6];
	IN_ADDR ina;
};

typedef char game_xnaddr_size_assert[sizeof(struct game_xnaddr) == 12 ? 1 : -1];

DWORD WSAAPI halo_xbox_XNetGetTitleXnAddr(struct game_xnaddr *address)
{
	XNADDR sdk;
	DWORD result;

	memset(&sdk, 0, sizeof(sdk));
	result = XNetGetTitleXnAddr(&sdk);
	if (address)
	{
		memset(address, 0, sizeof(*address));
		address->size = (unsigned char)sizeof(*address);
		memcpy(address->enet, sdk.abEnet, sizeof(address->enet));
		address->ina = sdk.ina;
	}
	return result;
}

/* (below: the key ids of hosts XNet doesn't key, the tunnel's and PCs') */
static int is_tunnel_key(const XNKID *key_identifier);

/* a peer's XNADDR (its abEnet the tunnel's identifier) gives its virtual
address; any other, XNet's */
INT WSAAPI halo_xbox_XNetXnAddrToInAddr(const struct game_xnaddr *address, const XNKID *key_identifier,
	IN_ADDR *result)
{
	static int logged;
	unsigned long peer;
	XNADDR sdk;
	INT error;

	if (!address || !result)
		return XNetXnAddrToInAddr(NULL, key_identifier, result);
	if (p2p_peer_address(address->enet, &peer))
	{
		if (xbox_trace_enabled() && logged++ < 6)
		{
			char tag[24];

			platform_log("tunnel: the host %s is a tunnel peer: reached at its virtual address",
				log_ethernet(address->enet, tag, sizeof(tag)));
		}
		result->s_addr = peer;
		return 0;
	}
	/* (a PC host on the LAN, its key not XNet's (halo_xbox_XNetRegisterKey):
	XNet would give an address of its key exchange, which the PC has none
	of; the PC advertises its own address in ina, and is reached there) */
	if (address->ina.s_addr && is_tunnel_key(key_identifier))
	{
		if (logged++ < 6)
			platform_log("system link: the PC host is reached at the address it advertised");
		*result = address->ina;
		return 0;
	}
	memset(&sdk, 0, sizeof(sdk));
	sdk.ina = address->ina;
	memcpy(sdk.abEnet, address->enet, sizeof(sdk.abEnet));
	error = XNetXnAddrToInAddr(&sdk, key_identifier, result);
	if (xbox_trace_enabled() && logged++ < 6)
	{
		char tag[24];

		platform_log("tunnel: the host %s is not a tunnel peer: XNet's address for it (%d)",
			log_ethernet(address->enet, tag, sizeof(tag)), (int)error);
	}
	return error;
}

/* Winsock started (after XNet: transport_endpoint_set_winsock.c): internet
play starts too, before the game makes any socket, so that it knows them
all (on with console.bypass_security: xbox_p2p.c's network.online). Its
stand-ins are this console's own, at 127.0.0.1 */
int WSAAPI halo_xbox_WSAStartup(WORD version_requested, LPWSADATA data)
{
	int result = WSAStartup(version_requested, data);

	if (result == 0)
		p2p_initialize(htonl(INADDR_LOOPBACK));
	return result;
}

/* the game's select: a socket with a peer's datagram queued for it
(xbox_winsock_deliver) is readable, whatever XNet says */
int WSAAPI halo_xbox_select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
	const struct timeval *timeout)
{
	static fd_set queued;
	struct timeval now = { 0, 0 };
	unsigned int index;
	int result;

	FD_ZERO(&queued);
	for (index = 0; read_set && index < read_set->fd_count; index++)
	{
		if (inbox_has(read_set->fd_array[index]))
			FD_SET(read_set->fd_array[index], &queued);
	}
	if (!queued.fd_count)
		return select(count, read_set, write_set, error_set, timeout);
	{
		long since;

		if (hop_log(_hop_select, &since))
			platform_log("tunnel: the game's select found %ld time(s) a peer datagram waiting", since);
	}
	/* (the rest as they are now, not waited for) */
	result = select(count, read_set, write_set, error_set, &now);
	if (result < 0)
	{
		if (read_set)
			FD_ZERO(read_set);
		if (write_set)
			FD_ZERO(write_set);
		if (error_set)
			FD_ZERO(error_set);
		result = 0;
	}
	for (index = 0; index < queued.fd_count && read_set->fd_count < FD_SETSIZE; index++)
	{
		if (!FD_ISSET(queued.fd_array[index], read_set))
		{
			FD_SET(queued.fd_array[index], read_set);
			result++;
		}
	}
	return result;
}

/* (the game's units' fd_set: the SDK's default, as this unit's) */
typedef char xbox_winsock_fd_setsize_assert[FD_SETSIZE == 64 ? 1 : -1];

/* ---------- the keys of tunnel hosts

A system link host's advertisement carries its XNet key (XNKID, XNKEY),
which a client registers before it reaches the host (transport_client_start,
XNetRegisterKey). A PC host's are random bytes (the desktop's xnet.c has no
keys), which XNet refuses as a key id; the tunnel's host is reached through
its virtual address, without XNet's key exchange. So the key ids of games
advertised through the tunnel are remembered (network_client_message_
handler.c), and neither registered with XNet nor unregistered; a LAN
host's key goes to XNet as it always did. */

enum
{
	MAXIMUM_TUNNEL_KEYS = 8,
};

static XNKID tunnel_keys[MAXIMUM_TUNNEL_KEYS];
static int tunnel_key_next;

static int is_tunnel_key(const XNKID *key_identifier)
{
	int index;

	for (index = 0; key_identifier && index < MAXIMUM_TUNNEL_KEYS; index++)
	{
		if (!memcmp(&tunnel_keys[index], key_identifier, sizeof(*key_identifier)))
			return 1;
	}
	return 0;
}

/* a game advertised through the tunnel has this key id */
void xbox_winsock_tunnel_key(const void *key_identifier)
{
	static const XNKID none;

	if (!key_identifier || !memcmp(key_identifier, &none, sizeof(none)) || is_tunnel_key((const XNKID *)key_identifier))
		return;
	memcpy(&tunnel_keys[tunnel_key_next++ % MAXIMUM_TUNNEL_KEYS], key_identifier, sizeof(XNKID));
}

/* (D:\config.toml's, port_config.h) */
int config_boolean(const char *name);

INT WSAAPI halo_xbox_XNetRegisterKey(const XNKID *key_identifier, const XNKEY *key)
{
	INT result;

	if (is_tunnel_key(key_identifier))
	{
		static int told;

		if (!told)
			platform_log("tunnel: the host's key is the tunnel's, not registered with XNet");
		told = 1;
		return 0;
	}
	result = XNetRegisterKey(key_identifier, key);
	if (result != 0)
	{
		/* (a PC host's key on a LAN: random bytes too, which XNet refuses
		as a key id. With XNet's security bypassed (console.bypass_security),
		the PC is reached at its address as a tunnel host is, so its key
		is remembered as one, neither registered nor unregistered, and the
		join goes on; with security on, the refusal stands) */
		if (config_boolean("console.bypass_security"))
		{
			platform_log("system link: the host's key is a PC's, not XNet's (%d); reached at its address", (int)result);
			xbox_winsock_tunnel_key(key_identifier);
			return 0;
		}
		platform_log("tunnel: XNetRegisterKey refused a LAN host's key (%d)", (int)result);
	}
	return result;
}

INT WSAAPI halo_xbox_XNetUnregisterKey(const XNKID *key_identifier)
{
	if (is_tunnel_key(key_identifier))
		return 0;
	return XNetUnregisterKey(key_identifier);
}
