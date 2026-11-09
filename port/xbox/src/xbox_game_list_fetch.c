/*
XBOX_GAME_LIST_FETCH.C

The game list's HTTP/1.0 request (xbox_game_list.h): a TCP connection, the
request, and the response read to the server's close (or Content-Length)
into the caller's buffer, never past it, each wait bounded by select. On
the console, through XNet's Winsock, the host looked up with XNet's DNS;
on the host (port/xbox/tests), BSD sockets and getaddrinfo, for the tests.
Blocking calls: the caller runs it on a thread of its own.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_game_list.h"

#ifdef _XBOX
#include <xtl.h>

/* (the console's C runtime has _snprintf; xbox_port.c gives snprintf) */
int snprintf(char *buffer, size_t size, const char *format, ...);
ULONG __cdecl DbgPrint(PCSTR format, ...);

typedef SOCKET socket_type;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define close_socket closesocket

static unsigned long now_ms(void)
{
	return GetTickCount();
}
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int socket_type;
#define INVALID_SOCKET_VALUE (-1)
#define close_socket close

static unsigned long now_ms(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (unsigned long)now.tv_sec * 1000ul + (unsigned long)now.tv_usec / 1000ul;
}
#endif

/* a dotted IPv4 address, exactly: 1 and the address in network order */
static int dotted_address(const char *host, unsigned long *address)
{
	unsigned long parts[4];
	int count = 0;
	const char *cursor = host;

	while (count < 4)
	{
		unsigned long value = 0;
		int digits = 0;

		while (*cursor >= '0' && *cursor <= '9' && digits < 3)
		{
			value = value * 10 + (unsigned long)(*cursor++ - '0');
			digits++;
		}
		if (!digits || value > 255)
			return 0;
		parts[count++] = value;
		if (count < 4 && *cursor++ != '.')
			return 0;
	}
	if (*cursor)
		return 0;
	{
		unsigned char bytes[4];

		bytes[0] = (unsigned char)parts[0];
		bytes[1] = (unsigned char)parts[1];
		bytes[2] = (unsigned char)parts[2];
		bytes[3] = (unsigned char)parts[3];
		memcpy(address, bytes, 4);
	}
	return 1;
}

static int wait_ready(socket_type socket_value, int write, unsigned long deadline);

#ifdef _XBOX
/* xbox_port.c's: safe from any thread, reaches debug.txt from the main loop */
void platform_log(const char *format, ...);

/* the public resolvers asked when XNet's lookup finds nothing: a console
whose dashboard has no DNS server, or one that answers only some names
(a DNS server for Xbox Live revivals). Cloudflare's, then Google's */
static const unsigned char fallback_resolvers[][4] = { { 1, 1, 1, 1 }, { 8, 8, 8, 8 } };
static const char *const fallback_names[] = { "Cloudflare's", "Google's" };

/* once XNet's lookup has found nothing and the public DNS has, the public
DNS first: XNet's lookup takes its seconds each time */
static volatile int fallback_first;

static int resolve_fallback(const char *host, unsigned long *address)
{
	unsigned char query[300], reply[512];
	unsigned short id = (unsigned short)(GetTickCount() * 2654435761ul >> 16);
	int length = game_list_dns_query(host, id, query, (int)sizeof(query));
	int index;

	if (!length)
		return 0;
	for (index = 0; index < (int)(sizeof(fallback_resolvers) / sizeof(fallback_resolvers[0])); index++)
	{
		SOCKET socket_value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		struct sockaddr_in target;
		int attempt, found = 0;

		if (socket_value == INVALID_SOCKET)
			return 0;
		memset(&target, 0, sizeof(target));
		target.sin_family = AF_INET;
		target.sin_port = htons(53);
		memcpy(&target.sin_addr, fallback_resolvers[index], 4);
		for (attempt = 0; attempt < 2 && !found; attempt++)
		{
			int received;

			if (sendto(socket_value, (const char *)query, length, 0, (struct sockaddr *)&target, sizeof(target)) !=
				length || !wait_ready(socket_value, 0, now_ms() + 3000))
				continue;
			received = recv(socket_value, (char *)reply, (int)sizeof(reply), 0);
			found = received > 0 && game_list_dns_answer(reply, received, id, address);
		}
		closesocket(socket_value);
		if (found)
		{
			if (!fallback_first)
				platform_log("dns: %s found through %s public DNS (the console's DNS servers did not answer for it)",
					host, fallback_names[index]);
			return 1;
		}
	}
	platform_log("dns: %s not found through the public DNS either", host);
	return 0;
}
#endif

int game_list_resolve(const char *host, unsigned long *address)
{
	if (dotted_address(host, address))
		return GAME_LIST_OK;
#ifdef _XBOX
	{
		XNDNS *dns = NULL;
		WSAEVENT event = WSACreateEvent();
		int result = GAME_LIST_ERROR_RESOLVE;

		if (event == WSA_INVALID_EVENT)
			return GAME_LIST_ERROR_RESOLVE;
		XNADDR xnaddr;
		DWORD xnstatus = XNetGetTitleXnAddr(&xnaddr);
		int lookup, xnet_status = -1;

		if (fallback_first && resolve_fallback(host, address))
		{
			WSACloseEvent(event);
			return GAME_LIST_OK;
		}

		/* (whether the console has an address and DNS servers yet: XNet's
		lookup needs both, from DHCP or the dashboard's settings) */
		DbgPrint("halo: game list: network status 0x%08lx%s%s%s\n", (unsigned long)xnstatus,
			xnstatus & XNET_GET_XNADDR_PENDING ? " pending" : "",
			xnstatus & XNET_GET_XNADDR_DHCP ? " dhcp" : "",
			xnstatus & XNET_GET_XNADDR_DNS ? " dns" : " (no dns servers)");
		lookup = XNetDnsLookup(host, event, &dns);
		if (lookup == 0 && dns)
		{
			/* (XNet's own lookup gives up within its timeouts; the wait is
			bounded all the same) */
			WaitForSingleObject(event, 20000);
			xnet_status = dns->iStatus;
			if (dns->iStatus == 0 && dns->cina > 0)
			{
				memcpy(address, &dns->aina[0], 4);
				result = GAME_LIST_OK;
			}
			else
				DbgPrint("halo: game list: lookup of %s: status %d, %u answers\n", host, dns->iStatus,
					(unsigned)dns->cina);
			XNetDnsRelease(dns);
		}
		else
			DbgPrint("halo: game list: lookup of %s not started (%d)\n", host, lookup);
		WSACloseEvent(event);
		/* (said once a run: the console's DNS, as the dashboard has it) */
		if (result != GAME_LIST_OK)
		{
			static int told;

			if (!told)
				platform_log("dns: the console's DNS servers did not find %s (lookup %d, status %d, network status "
					"0x%08lx%s); asking public DNS", host, lookup, xnet_status, (unsigned long)xnstatus,
					xnstatus & XNET_GET_XNADDR_DNS ? "" : ", no DNS servers set");
			told = 1;
			if (!fallback_first && resolve_fallback(host, address))
			{
				fallback_first = 1;
				result = GAME_LIST_OK;
			}
		}
		return result;
	}
#else
	{
		struct addrinfo hints, *found = NULL;
		int result = GAME_LIST_ERROR_RESOLVE;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (!getaddrinfo(host, NULL, &hints, &found) && found)
		{
			memcpy(address, &((struct sockaddr_in *)found->ai_addr)->sin_addr, 4);
			result = GAME_LIST_OK;
		}
		if (found)
			freeaddrinfo(found);
		return result;
	}
#endif
}

/* until socket is ready to read (or write), or the deadline: 1 ready */
static int wait_ready(socket_type socket_value, int write, unsigned long deadline)
{
	fd_set set;
	struct timeval wait;
	unsigned long now = now_ms();
	unsigned long left;

	if ((long)(deadline - now) <= 0)
		return 0;
	left = deadline - now;
	FD_ZERO(&set);
	FD_SET(socket_value, &set);
	wait.tv_sec = (long)(left / 1000);
	wait.tv_usec = (long)(left % 1000) * 1000;
	return select((int)socket_value + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

/* whether the response's first size bytes hold its headers' end ("\r\n\r\n"
or "\n\n"), and the response's whole size, if they give a Content-Length
(else 0) */
static int header_end(const char *data, unsigned long size, unsigned long *total)
{
	struct game_list_response response;
	unsigned long index;

	for (index = 1; index < size; index++)
	{
		if (data[index] == '\n' && (data[index - 1] == '\n' || (index >= 2 && data[index - 1] == '\r' &&
			data[index - 2] == '\n')))
		{
			int result = game_list_parse_response(data, size, &response);

			/* (the body not all here yet is the usual case) */
			*total = 0;
			if ((result == GAME_LIST_OK || result == GAME_LIST_ERROR_BODY_TRUNCATED) && response.content_length >= 0)
				*total = index + 1 + (unsigned long)response.content_length;
			return 1;
		}
	}
	return 0;
}

int game_list_request(unsigned long address, unsigned short port, const char *host, const char *path,
	const char *body, char *buffer, unsigned long size, unsigned long *received, unsigned long timeout_ms)
{
	char request[1024];
	struct sockaddr_in target;
	socket_type socket_value;
	unsigned long deadline = now_ms() + timeout_ms;
	unsigned long sent = 0, total = 0;
	int headers_done = 0;
	int length;
	int result = GAME_LIST_OK;

	*received = 0;
	if (body)
		length = snprintf(request, sizeof(request),
			"POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Warthog/1\r\nAccept: application/json\r\n"
			"Accept-Encoding: identity\r\nContent-Type: application/json\r\nContent-Length: %d\r\n"
			"Connection: close\r\n\r\n%s", path, host, (int)strlen(body), body);
	else
		length = snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Warthog/1\r\nAccept: text/plain\r\n"
			"Accept-Encoding: identity\r\nConnection: close\r\n\r\n", path, host);
	if (length <= 0 || length >= (int)sizeof(request))
		return GAME_LIST_ERROR_SEND;

	socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_value == INVALID_SOCKET_VALUE)
		return GAME_LIST_ERROR_CONNECT;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(port);
	memcpy(&target.sin_addr, &address, 4);
	/* (a blocking connect: the stack's own retries end it) */
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0)
	{
		close_socket(socket_value);
		return GAME_LIST_ERROR_CONNECT;
	}

	while (sent < (unsigned long)length)
	{
		int count;

		if (!wait_ready(socket_value, 1, deadline))
		{
			result = GAME_LIST_ERROR_TIMEOUT;
			break;
		}
		count = send(socket_value, request + sent, length - (int)sent, 0);
		if (count <= 0)
		{
			result = GAME_LIST_ERROR_SEND;
			break;
		}
		sent += (unsigned long)count;
	}

	/* to the server's close, or the Content-Length's end, never past size */
	while (result == GAME_LIST_OK)
	{
		int count;

		if (*received >= size)
		{
			result = GAME_LIST_ERROR_TOO_LARGE;
			break;
		}
		if (!wait_ready(socket_value, 0, deadline))
		{
			result = GAME_LIST_ERROR_TIMEOUT;
			break;
		}
		count = recv(socket_value, buffer + *received, (int)(size - *received), 0);
		if (count < 0)
		{
			result = GAME_LIST_ERROR_RECEIVE;
			break;
		}
		if (count == 0)
			break;
		*received += (unsigned long)count;
		if (!headers_done)
			headers_done = header_end(buffer, *received, &total);
		if (headers_done && total && *received >= total)
			break;
	}
	close_socket(socket_value);
	return result;
}

int game_list_fetch(unsigned long address, unsigned short port, const char *host, const char *path,
	char *buffer, unsigned long size, unsigned long *received, unsigned long timeout_ms)
{
	return game_list_request(address, port, host, path, NULL, buffer, size, received, timeout_ms);
}
