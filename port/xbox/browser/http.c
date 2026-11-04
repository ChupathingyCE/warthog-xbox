/*
 * http.h: GET over plat's sockets. the headers are read whole (8 KB at most),
 * then the body goes to the callback as it comes, decoded when chunked
 */

#include "http.h"
#include "plat.h"
#include "port/console/https/console_https.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void platform_log(const char *format, ...);

// headers past 8k = something is very wrong, just give up
#define HTTP_HEADER_MAX 8192
#define HTTP_USER_AGENT "Warthog/1 (map browser)"
/* (the build, to the store: "Warthog 0.9.0") */
#ifndef HALO_VERSION
#define HALO_VERSION "unknown"
#endif
#define HTTP_CLIENT_VERSION "X-Client-Version: Warthog " HALO_VERSION "\r\n"
/* (an https answer's handshake, then the most to wait for each piece) */
#define PLAT_HTTPS_TIMEOUT_MS 30000

// a little read buffer over the socket. start/end are whats not read yet
typedef struct
{
	int socket;
	char buffer[16384];
	int start, end;
} http_stream;

// more bytes off the socket into the buffer. 0 when its closed
static int stream_fill(http_stream *s)
{
	int got;

	if (s->start > 0 && s->start == s->end)
		s->start = s->end = 0;
	if (s->end == (int)sizeof(s->buffer))
		return 0;
	got = plat_recv(s->socket, s->buffer + s->end, (int)sizeof(s->buffer) - s->end);
	if (got <= 0)
		return 0;
	s->end += got;
	return 1;
}

/* a line (without its \r\n) into line: 0 at the end of the stream */
// one header line. handles \r\n and bare \n both
static int stream_line(http_stream *s, char *line, int size)
{
	for (;;)
	{
		char *nl = memchr(s->buffer + s->start, '\n', s->end - s->start);

		if (nl)
		{
			int length = (int)(nl - (s->buffer + s->start));
			int copy = length < size - 1 ? length : size - 1;

			memcpy(line, s->buffer + s->start, copy);
			line[copy] = 0;
			if (copy && line[copy - 1] == '\r')
				line[copy - 1] = 0;
			s->start += length + 1;
			return 1;
		}
		/* (move what's left to the front for more) */
		if (s->start)
		{
			memmove(s->buffer, s->buffer + s->start, s->end - s->start);
			s->end -= s->start;
			s->start = 0;
		}
		if (!stream_fill(s))
			return 0;
	}
}

/* up to want bytes of the body to the callback: what was passed, -1 when the
callback stopped it */
// hand the body to the callback a piece at a time, up to want bytes
static long stream_body(http_stream *s, unsigned long want, http_body_proc body, void *context)
{
	unsigned long passed = 0;

	while (passed < want)
	{
		int have = s->end - s->start, take;

		if (!have)
		{
			s->start = s->end = 0;
			if (!stream_fill(s))
				break;
			continue;
		}
		take = (unsigned long)have < want - passed ? have : (int)(want - passed);
		if (!body(s->buffer + s->start, take, context))
			return -1;
		s->start += take;
		passed += take;
	}
	return (long)passed;
}

// header names are case insensitive, cloudflare sends em lowercase sometimes
static int header_is(const char *line, const char *name)
{
	size_t n = strlen(name);
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i]))
			return 0;
	}
	return line[n] == ':';
}

static const char *header_value(const char *line)
{
	const char *v = strchr(line, ':');

	v = v ? v + 1 : line + strlen(line);
	while (*v == ' ' || *v == '\t')
		v++;
	return v;
}

/* http://host[:port]/path into its parts: 0 if it isn't one */
// http://host[:port]/path only. no https, theres no tls on the box here
static int parse_url(const char *url, char *host, int host_size, int *port, char *path, int path_size)
{
	const char *p, *slash, *colon;
	int length;

	if (strncmp(url, "http://", 7))
		return 0;
	p = url + 7;
	slash = strchr(p, '/');
	if (!slash)
		slash = p + strlen(p);
	colon = memchr(p, ':', slash - p);
	length = (int)((colon ? colon : slash) - p);
	if (length <= 0 || length >= host_size)
		return 0;
	memcpy(host, p, length);
	host[length] = 0;
	*port = colon ? atoi(colon + 1) : 80;
	snprintf(path, path_size, "%s", *slash ? slash : "/");
	return 1;
}

// one request, no redirects. the caller deals w those
static int get_once(const char *url, char *location, int location_size, http_size_proc size, http_body_proc body,
	void *context, char *error, int error_size)
{
	char host[128], path[1024], line[HTTP_HEADER_MAX], request[1400];
	int port, status = -1, chunked = 0, have_length = 0, length;
	unsigned long content_length = 0;
	http_stream *s;

	if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path)))
	{
		snprintf(error, error_size, "not an http:// url: %.80s", url);
		return -1;
	}
	s = (http_stream *)malloc(sizeof(*s));
	if (!s)
	{
		snprintf(error, error_size, "out of memory");
		return -1;
	}
	s->start = s->end = 0;
	s->socket = plat_connect(host, port, error, error_size);
	/* (only -1 is a failure: the xbox's sockets are kernel addresses, negative as an int) */
	if (s->socket == -1)
	{
		free(s);
		return -1;
	}
	// the request. user agent matters, cloudflare blocks some defaults
	// x-client-version names the build to the store (its rate limit lets ours through)
	// identity = no gzip, we dont have zlib wired up for this
	length = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: " HTTP_USER_AGENT "\r\n"
		HTTP_CLIENT_VERSION "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
		path, host);
	if (plat_send(s->socket, request, length) != length)
	{
		snprintf(error, error_size, "send to %s failed", host);
		goto done;
	}

	/* the status line, then the headers */
	if (!stream_line(s, line, sizeof(line)) || sscanf(line, "HTTP/%*d.%*d %d", &status) != 1)
	{
		snprintf(error, error_size, "no answer from %s", host);
		status = -1;
		goto done;
	}
	location[0] = 0;
	while (stream_line(s, line, sizeof(line)) && line[0])
	{
		if (header_is(line, "content-length"))
		{
			content_length = strtoul(header_value(line), NULL, 10);
			have_length = 1;
		}
		else if (header_is(line, "transfer-encoding") && strstr(header_value(line), "chunked"))
			chunked = 1;
		else if (header_is(line, "location"))
			snprintf(location, location_size, "%s", header_value(line));
	}
	// redirect. let http_get follow it
	if (status >= 300 && status < 400)
		goto done; /* (the caller follows it) */
	if (status != 200 && status != 206)
	{
		/* the server's message, for the error */
		char message[160];
		long got = 0;

		while (got < (long)sizeof(message) - 1 && (s->end > s->start || stream_fill(s)))
		{
			int take = s->end - s->start;

			if (take > (int)sizeof(message) - 1 - got)
				take = (int)sizeof(message) - 1 - got;
			memcpy(message + got, s->buffer + s->start, take);
			s->start += take;
			got += take;
		}
		message[got] = 0;
		snprintf(error, error_size, "HTTP %d: %.120s", status, message);
		goto done;
	}

	if (size)
		size(have_length && !chunked ? content_length : 0, context);
	// cloudflare likes chunked for the dynamic stuff (the list). maps come w content-length
	if (chunked)
	{
		for (;;)
		{
			unsigned long chunk;
			long passed;

			if (!stream_line(s, line, sizeof(line)))
			{
				snprintf(error, error_size, "the body ended early");
				status = -1;
				break;
			}
			chunk = strtoul(line, NULL, 16);
			if (!chunk)
				break;
			passed = stream_body(s, chunk, body, context);
			if (passed < 0 || (unsigned long)passed != chunk)
			{
				snprintf(error, error_size, passed < 0 ? "stopped" : "the body ended early");
				status = -1;
				break;
			}
			stream_line(s, line, sizeof(line)); /* (the chunk's \r\n) */
		}
	}
	else
	{
		long passed = stream_body(s, have_length ? content_length : (unsigned long)-1, body, context);

		if (passed < 0 || (have_length && (unsigned long)passed != content_length))
		{
			snprintf(error, error_size, passed < 0 ? "stopped" : "the body ended early (%ld of %lu bytes)",
				passed, content_length);
			status = -1;
		}
	}

done:
	plat_close(s->socket);
	free(s);
	if (status != 200 && status != 206 && !(status >= 300 && status < 400))
		platform_log("map browser: GET %.120s: status %d, %s", url, status, error);
	return status;
}

/* an https:// url, by the console's HTTPS client: the status, or -1 with
why in error. no redirects (the store's api answers in place) */
struct https_body
{
	http_size_proc size;
	http_body_proc body;
	void *context;
};

static void https_size(unsigned long size, void *context)
{
	struct https_body *b = (struct https_body *)context;

	if (b->size)
		b->size(size, b->context);
}

static int https_body(const char *data, unsigned long size, void *context)
{
	struct https_body *b = (struct https_body *)context;

	return b->body(data, (int)size, b->context);
}

static int get_https(const char *url, http_size_proc size, http_body_proc body, void *context, char *error,
	int error_size)
{
	char host[128];
	const char *p = url + 8, *slash = strchr(p, '/');
	struct https_body b;
	int length, status = 0, result;

	length = slash ? (int)(slash - p) : (int)strlen(p);
	if (length <= 0 || length >= (int)sizeof(host) || memchr(p, ':', length))
	{
		snprintf(error, error_size, "not an https:// url this can ask: %.80s", url);
		return -1;
	}
	memcpy(host, p, length);
	host[length] = 0;
	b.size = size;
	b.body = body;
	b.context = context;
	result = console_https_get_stream(host, slash ? slash : "/", HTTP_CLIENT_VERSION, &status,
		https_size, https_body, &b, PLAT_HTTPS_TIMEOUT_MS);
	if (result != CONSOLE_HTTPS_OK)
	{
		snprintf(error, error_size, "%s: %s", host, console_https_error_string(result));
		platform_log("map browser: GET %.120s: %s", url, error);
		return -1;
	}
	if (status != 200 && status != 206)
	{
		snprintf(error, error_size, "HTTP %d", status);
		platform_log("map browser: GET %.120s: status %d", url, status);
	}
	return status;
}

void http_plain_url(char *out, int size, const char *base, const char *path)
{
	if (!strncmp(base, "https://", 8))
		snprintf(out, size, "http://%s%s", base + 8, path);
	else
		snprintf(out, size, "%s%s", base, path);
}

// the public one. follows one redirect but only to another http:// url
// an https redirect = fail w a message, cant follow that from here
int http_get(const char *url, http_size_proc size, http_body_proc body, void *context, char *error, int error_size)
{
	char location[1024];
	int status;

	if (!strncmp(url, "https://", 8))
		return get_https(url, size, body, context, error, error_size);
	status = get_once(url, location, sizeof(location), size, body, context, error, error_size);

	if (status >= 300 && status < 400)
	{
		if (!location[0] || strncmp(location, "http://", 7))
		{
			snprintf(error, error_size, "redirected to %.100s (https isn't supported here)", location);
			return -1;
		}
		status = get_once(location, location, sizeof(location), size, body, context, error, error_size);
		if (status >= 300 && status < 400)
		{
			snprintf(error, error_size, "redirected twice");
			return -1;
		}
	}
	return status;
}
