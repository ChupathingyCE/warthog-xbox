/*
PLAT_WARTHOG.C

plat.h on the console, inside the game: files through the SDK, sockets through
XNet's Winsock, as the game list's fetch (port/xbox/src/xbox_game_list_fetch.c).
The browser's other files are the store's map browser's, unchanged but for its
stop and its names (port/xbox/browser).
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "plat.h"
#include "../include/xbox_game_list.h"

#define PLAT_TIMEOUT_SECONDS 30
/* (the SDK leaves it out) */
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

int plat_init(char *status, int size)
{
	(void)status;
	(void)size;
	return 1;
}

int plat_is_file(const char *path)
{
	DWORD attributes = GetFileAttributesA(path);

	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_is_dir(const char *path)
{
	DWORD attributes = GetFileAttributesA(path);

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int plat_mkdir(const char *path)
{
	return CreateDirectoryA(path, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

int plat_remove(const char *path)
{
	return DeleteFileA(path) != 0;
}

int plat_rmdir(const char *path)
{
	return RemoveDirectoryA(path) != 0;
}

void plat_list(const char *path, plat_found_proc found, void *context)
{
	WIN32_FIND_DATAA data;
	char pattern[512];
	HANDLE find;

	snprintf(pattern, sizeof(pattern), "%s%s*", path, path[strlen(path) - 1] == '\\' ? "" : "\\");
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, ".."))
			continue;
		if (!found(data.cFileName, (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, context))
			break;
	}
	while (FindNextFileA(find, &data));
	FindClose(find);
}

const char *const *plat_roots(void)
{
	static const char *const roots[] = { "D:\\", NULL };

	return roots;
}

const char *plat_fallback_dir(void)
{
	return "D:\\";
}

static int plat_ready(int socket_value, int write)
{
	fd_set set;
	struct timeval wait;

	FD_ZERO(&set);
	FD_SET((SOCKET)socket_value, &set);
	wait.tv_sec = PLAT_TIMEOUT_SECONDS;
	wait.tv_usec = 0;
	return select(0, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

int plat_connect(const char *host, int port, char *error, int error_size)
{
	struct sockaddr_in target;
	unsigned long address;
	SOCKET socket_value;

	if (game_list_resolve(host, &address) != GAME_LIST_OK)
	{
		snprintf(error, error_size, "%s did not resolve", host);
		return -1;
	}
	socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_value == INVALID_SOCKET)
	{
		snprintf(error, error_size, "no socket (%d)", WSAGetLastError());
		return -1;
	}
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons((unsigned short)port);
	memcpy(&target.sin_addr, &address, 4);
	if (connect(socket_value, (struct sockaddr *)&target, sizeof(target)) != 0)
	{
		snprintf(error, error_size, "could not connect to %s (%d)", host, WSAGetLastError());
		closesocket(socket_value);
		return -1;
	}
	return (int)socket_value;
}

int plat_send(int socket_value, const char *data, int length)
{
	int sent = 0;

	while (sent < length)
	{
		int count;

		if (!plat_ready(socket_value, 1))
			return -1;
		count = send((SOCKET)socket_value, data + sent, length - sent, 0);
		if (count <= 0)
			return -1;
		sent += count;
	}
	return sent;
}

int plat_recv(int socket_value, char *buffer, int size)
{
	if (!plat_ready(socket_value, 0))
		return -1;
	return recv((SOCKET)socket_value, buffer, size, 0);
}

void plat_close(int socket_value)
{
	closesocket((SOCKET)socket_value);
}
