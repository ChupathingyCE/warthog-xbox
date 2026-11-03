/*
XBOX_CE_FILES.C

ReadFile for the game's units (halo_xbox_prefix.h). A Custom Edition map's file
is opened for overlapped reads (the cache thread's, cache_files_windows.c), and
its header and checks read it without an OVERLAPPED, at the file's position
(ce_map_open, ce_map_checks.c, ce_resources.c), which the desktop builds' layer
takes. The kernel refuses that: a file opened so has no position of its own to
read at (STATUS_INVALID_PARAMETER). Such a read is made here at the position the
file was given, and moves it on, as a plain read would.
*/

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE ((DWORD)-1)
#endif

BOOL WINAPI halo_xbox_ReadFile(
	HANDLE file,
	LPVOID buffer,
	DWORD size,
	LPDWORD bytes_read,
	LPOVERLAPPED overlapped)
{
	OVERLAPPED at;
	DWORD position;
	DWORD done = 0;
	BOOL read;

	if (overlapped || ReadFile(file, buffer, size, bytes_read, NULL))
		return overlapped ? ReadFile(file, buffer, size, bytes_read, overlapped) : TRUE;
	if (GetLastError() != ERROR_INVALID_PARAMETER)
		return FALSE;

	position = SetFilePointer(file, 0, NULL, FILE_CURRENT);
	if (position == INVALID_SET_FILE_POINTER)
		return FALSE;
	ZeroMemory(&at, sizeof(at));
	at.Offset = position;
	at.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!at.hEvent)
		return FALSE;
	read = ReadFile(file, buffer, size, &done, &at);
	if (!read && GetLastError() == ERROR_IO_PENDING)
		read = GetOverlappedResult(file, &at, &done, TRUE);
	CloseHandle(at.hEvent);
	if (bytes_read)
		*bytes_read = read ? done : 0;
	if (read)
		SetFilePointer(file, (LONG)(position + done), NULL, FILE_BEGIN);
	return read;
}

/* ---------- a map file's hash */

/* port/linux/src/map_file_hash.c's, on the console's own file calls: a host of
network version 29 or later names its Custom Edition map's version by this, so a
joining console reads the whole file once to say whether it has the host's. Kept
by the file's path and size for the run. */

#define XBOX_CE_HASHES_KEPT 16
#define XBOX_CE_HASH_SIZE 32
#define XBOX_CE_HASH_MAXIMUM_FILE 0x20000000UL

typedef int (*map_archive_read_proc)(void *context, unsigned int offset, void *buffer, unsigned int size);
typedef int (*map_archive_stop_proc)(void *context);
int map_archive_map_hash(map_archive_read_proc read, map_archive_stop_proc stop, void *context, unsigned int size,
	unsigned char *hash);
unsigned int map_archive_hash_prefix(const unsigned char *hash);

static struct
{
	char path[128];
	DWORD size;
	unsigned int prefix;
} map_hashes[XBOX_CE_HASHES_KEPT];
static int map_hash_next;

static int map_hash_read(void *context, unsigned int offset, void *buffer, unsigned int size)
{
	HANDLE file = *(HANDLE *)context;
	DWORD read = 0;

	if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return 0;
	return ReadFile(file, buffer, size, &read, NULL) && read == size;
}

int map_file_hash_prefix(const char *path, unsigned int *prefix)
{
	unsigned char hash[XBOX_CE_HASH_SIZE];
	HANDLE file;
	DWORD size;
	int index, ok;

	if (!path || strlen(path) >= sizeof(map_hashes[0].path))
		return 0;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	size = GetFileSize(file, NULL);
	if (size == INVALID_FILE_SIZE || size > XBOX_CE_HASH_MAXIMUM_FILE)
	{
		CloseHandle(file);
		return 0;
	}
	for (index = 0; index < XBOX_CE_HASHES_KEPT; index++)
	{
		if (map_hashes[index].path[0] && map_hashes[index].size == size && !_stricmp(map_hashes[index].path, path))
		{
			*prefix = map_hashes[index].prefix;
			CloseHandle(file);
			return 1;
		}
	}
	ok = map_archive_map_hash(map_hash_read, NULL, &file, size, hash);
	CloseHandle(file);
	if (!ok)
		return 0;
	*prefix = map_archive_hash_prefix(hash);
	strcpy(map_hashes[map_hash_next].path, path);
	map_hashes[map_hash_next].size = size;
	map_hashes[map_hash_next].prefix = *prefix;
	map_hash_next = (map_hash_next + 1) % XBOX_CE_HASHES_KEPT;
	return 1;
}

int map_file_hash_version(const char *path, unsigned int *version)
{
	unsigned int prefix;

	if (!map_file_hash_prefix(path, &prefix))
		return 0;
	/* (0 is the version a client checks nothing for: map_archive_hash_version) */
	*version = prefix ? prefix : 1;
	return 1;
}

/* ---------- a Custom Edition map's converted vertices */

/* ce_bsp.c and ce_models.c's XPhysicalAlloc and XPhysicalFree (port/xbox/build.py
renames them in those two). They ask for a block for each model part and BSP
material, and the console's free memory is in pieces (a 128 MB console with 59 MB
free: its largest piece 1120 KB), so a level's failed and the game drew vertices it
can't read. The Xbox's own tag cache, at its fixed address, holds nothing while a
Custom Edition map (with a tag cache of its own) is loaded, and is one contiguous
block the GPU reads vertices from anyway. A block goes in the first gap that holds
it: they come back in any order (a level's outlives its map, until the next level's
is made). */

#define XBOX_CE_VERTEX_BASE 0x803A6000UL
#define XBOX_CE_VERTEX_SIZE 0x01600000UL
#define XBOX_CE_VERTEX_ALIGNMENT 16UL

struct vertex_block
{
	unsigned long offset;
	unsigned long size;
};

/* the blocks in use, by their offsets (outside the tag cache: the next map's tags
load over it before these are freed) */
static struct vertex_block *vertex_blocks;
static long vertex_block_count, vertex_block_capacity;
static unsigned long vertex_logged;

void platform_log(const char *format, ...);

LPVOID WINAPI xbox_ce_vertex_alloc(
	SIZE_T size,
	ULONG_PTR maximum,
	ULONG_PTR alignment,
	DWORD protect)
{
	unsigned long bytes = ((unsigned long)size + XBOX_CE_VERTEX_ALIGNMENT - 1) & ~(XBOX_CE_VERTEX_ALIGNMENT - 1);
	unsigned long offset = 0;
	long index;

	(void)maximum;
	(void)alignment;
	(void)protect;
	if (bytes < size)
		return NULL;
	if (vertex_block_count == vertex_block_capacity)
	{
		long capacity = vertex_block_capacity ? vertex_block_capacity * 2 : 1024;
		struct vertex_block *grown = (struct vertex_block *)realloc(vertex_blocks, capacity * sizeof(*grown));

		if (!grown)
			return NULL;
		vertex_blocks = grown;
		vertex_block_capacity = capacity;
	}
	for (index = 0; index < vertex_block_count; index++)
	{
		if (vertex_blocks[index].offset - offset >= bytes)
			break;
		offset = vertex_blocks[index].offset + vertex_blocks[index].size;
	}
	if (bytes > XBOX_CE_VERTEX_SIZE - offset)
	{
		platform_log("custom edition: %lu bytes of vertices don't fit the tag cache (%ld blocks in use)",
			(unsigned long)size, vertex_block_count);
		return NULL;
	}
	memmove(&vertex_blocks[index + 1], &vertex_blocks[index], (vertex_block_count - index) * sizeof(*vertex_blocks));
	vertex_blocks[index].offset = offset;
	vertex_blocks[index].size = bytes;
	vertex_block_count++;
	/* (each megabyte, as the top of what's in use passes it) */
	if ((offset + bytes) >> 20 != vertex_logged >> 20 && offset + bytes > vertex_logged)
		platform_log("custom edition: %lu KB of vertices in the tag cache", (offset + bytes) >> 10);
	if (offset + bytes > vertex_logged)
		vertex_logged = offset + bytes;
	return (LPVOID)(XBOX_CE_VERTEX_BASE + offset);
}

VOID WINAPI xbox_ce_vertex_free(
	LPVOID address)
{
	unsigned long offset = (unsigned long)address - XBOX_CE_VERTEX_BASE;
	long low = 0, high = vertex_block_count - 1;

	while (low <= high)
	{
		long middle = (low + high) / 2;

		if (vertex_blocks[middle].offset == offset)
		{
			memmove(&vertex_blocks[middle], &vertex_blocks[middle + 1],
				(vertex_block_count - middle - 1) * sizeof(*vertex_blocks));
			vertex_block_count--;
			return;
		}
		if (vertex_blocks[middle].offset < offset)
			low = middle + 1;
		else
			high = middle - 1;
	}
}
