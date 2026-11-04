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
extern int platform_ce_alias;
unsigned long ce_resources_allocate(unsigned long size);

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
	/* (a 64 MB console: the range is the tag cache's, so the room the loader keeps below
	the BSPs is, in order: a block of it, at the cache's own address, which the GPU reads) */
	if (platform_ce_alias)
	{
		unsigned long room = ce_resources_allocate(bytes);

		return room ? (LPVOID)(XBOX_CE_VERTEX_BASE + (room - 0x40440000UL)) : NULL;
	}
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

	/* (the loader's room is given back as the loader does: a level's with it, a map's
	when it goes) */
	if (platform_ce_alias)
		return;

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

/* ---------- a 64 MB console: Custom Edition's tag cache on the Xbox's */

/* A 64 MB console has no room for a second tag cache. Custom Edition maps are
linked to 0x40440000, 23 MB; the Xbox's own tag cache at 0x803A6000, 22 MB, is idle
while one is loaded. So the range is made the Xbox tag cache's own pages, by the page
tables (the kernel keeps them at 0xC0000000, its directory's own entry at 0xC0300000):
the bottom of the range on the bottom of the cache, the map's structure BSPs at the
top of the range on the top of the cache, and a megabyte between them that is no
memory at all (platform_ce_room_end: what the loader may fill below the BSPs). Tried
alone on a console first (warthog-ce-alias-poc). */

#define CE_ALIAS_BASE 0x40440000UL
#define CE_ALIAS_SIZE 0x01700000UL
#define CE_ALIAS_CACHE 0x803A6000UL
#define CE_ALIAS_CACHE_SIZE 0x01600000UL
#define CE_ALIAS_PAGE 0x1000UL
#define CE_ALIAS_PTE_BASE 0xC0000000UL
#define CE_ALIAS_PDE_BASE 0xC0300000UL
/* (present, writable, accessed, dirty: as the kernel's own) */
#define CE_ALIAS_PTE_FLAGS 0x63UL
#define CE_ALIAS_FIRST_PDE (CE_ALIAS_BASE >> 22)
#define CE_ALIAS_PDE_COUNT (((CE_ALIAS_BASE + CE_ALIAS_SIZE - 1) >> 22) - CE_ALIAS_FIRST_PDE + 1)

ULONG_PTR __stdcall MmGetPhysicalAddress(PVOID address);
unsigned long ce_resources_allocate(unsigned long size);

/* 1 once the range is the tag cache's (the loader asks: cache_files.c, ce_map_checks.c) */
int platform_ce_alias = 0;

static unsigned long *ce_alias_tables;
static unsigned long ce_alias_physical;

static void ce_alias_flush(void)
{
	__asm__ volatile("mov %%cr3, %%eax\n\tmov %%eax, %%cr3" : : : "eax", "memory");
}

static void ce_alias_map(unsigned long address, unsigned long bytes, unsigned long cache_offset)
{
	unsigned long page;

	for (page = 0; page < bytes / CE_ALIAS_PAGE; page++)
	{
		ce_alias_tables[(address - (CE_ALIAS_FIRST_PDE << 22)) / CE_ALIAS_PAGE + page] =
			(ce_alias_physical + cache_offset + page * CE_ALIAS_PAGE) | CE_ALIAS_PTE_FLAGS;
	}
}

static void ce_alias_unmap(unsigned long address, unsigned long bytes)
{
	memset(&ce_alias_tables[(address - (CE_ALIAS_FIRST_PDE << 22)) / CE_ALIAS_PAGE], 0, bytes / CE_ALIAS_PAGE * 4);
}

/* the page tables for the range, once: 1 if it can be the tag cache's. Anything
unexpected stops it before a page table is written */
int xbox_ce_alias_install(void)
{
	volatile unsigned long *directory = (volatile unsigned long *)CE_ALIAS_PDE_BASE;
	unsigned long cr3, index;

	if (platform_ce_alias)
		return 1;
	__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
	if ((directory[CE_ALIAS_PTE_BASE >> 22] & ~0xFFFUL) != (cr3 & ~0xFFFUL))
	{
		platform_log("custom edition: the page tables aren't where they're expected; no tag cache");
		return 0;
	}
	for (index = 0; index < CE_ALIAS_PDE_COUNT; index++)
	{
		if (directory[CE_ALIAS_FIRST_PDE + index] & 1)
		{
			platform_log("custom edition: 0x40440000 is in use; no tag cache");
			return 0;
		}
	}
	/* (so the kernel gives nothing else out of it) */
	if (VirtualAlloc((void *)CE_ALIAS_BASE, CE_ALIAS_SIZE, MEM_RESERVE, PAGE_READWRITE) != (void *)CE_ALIAS_BASE)
	{
		platform_log("custom edition: 0x40440000 could not be reserved; no tag cache");
		return 0;
	}
	ce_alias_tables = (unsigned long *)XPhysicalAlloc(CE_ALIAS_PDE_COUNT * CE_ALIAS_PAGE, MAXULONG_PTR, CE_ALIAS_PAGE,
		PAGE_READWRITE);
	if (!ce_alias_tables)
	{
		platform_log("custom edition: no memory for the page tables; no tag cache");
		return 0;
	}
	memset(ce_alias_tables, 0, CE_ALIAS_PDE_COUNT * CE_ALIAS_PAGE);
	ce_alias_physical = MmGetPhysicalAddress((void *)CE_ALIAS_CACHE);
	for (index = 0; index < CE_ALIAS_PDE_COUNT; index++)
	{
		directory[CE_ALIAS_FIRST_PDE + index] =
			MmGetPhysicalAddress((unsigned char *)ce_alias_tables + index * CE_ALIAS_PAGE) | CE_ALIAS_PTE_FLAGS;
	}
	ce_alias_flush();
	platform_ce_alias = 1;
	platform_log("custom edition: its tag cache is the Xbox's (physical %08lx), by the page tables", ce_alias_physical);
	return 1;
}

/* a map about to load: the range's first 22 MB the tag cache's, in order (its
BSPs' window comes once they're known: platform_ce_room_end) */
void platform_ce_alias_whole(void)
{
	if (!platform_ce_alias)
		return;
	ce_alias_unmap(CE_ALIAS_BASE, CE_ALIAS_SIZE);
	ce_alias_map(CE_ALIAS_BASE, CE_ALIAS_CACHE_SIZE, 0);
	ce_alias_flush();
}

/* the lowest of a map's BSPs' bases (the room below them the loader fills): the
BSPs' window made, the top of the range on the top of the tag cache, and the end of
what may be filled below it, a megabyte lower (where there's no memory). The same
for the same map, as often as it's asked */
unsigned long platform_ce_room_end(unsigned long end_free)
{
	unsigned long first, top = CE_ALIAS_BASE + CE_ALIAS_SIZE;
	unsigned long gap = CE_ALIAS_SIZE - CE_ALIAS_CACHE_SIZE;

	if (!platform_ce_alias)
		return end_free;
	first = end_free & ~(CE_ALIAS_PAGE - 1);
	if (first < CE_ALIAS_BASE + gap)
		first = CE_ALIAS_BASE + gap;
	if (first > top)
		first = top;
	ce_alias_unmap(first - gap, top - (first - gap));
	ce_alias_map(first, top - first, first - gap - CE_ALIAS_BASE);
	ce_alias_flush();
	return first - gap;
}

/* ---------- a 64 MB console: what a map's load needs for a while */

/* The loader's checks, its sounds' decoding and its model data take more than a 64 MB
console has free (a model data block of 9 MB, a level's BSP read whole to check it,
an Ogg Vorbis stream decoded). While a Custom Edition map loads, those come out of the
texture cache's own memory, lent as the game lends it for a movie
(texture_cache_steal_memory), and go back once the map is loaded, before anything is
drawn of it. ce_map_checks.c, ce_resources.c and ce_vorbis.c's malloc, realloc and free
are these (port/xbox/build.py); outside a load, the heap's. */

void *texture_cache_steal_memory(long size);
void texture_cache_return_memory(void);
unsigned char texture_cache_memory_stolen(void);
long texture_cache_stealable_size(void);

#define SCRATCH_ALIGNMENT 16UL

static unsigned char *scratch_base;
static unsigned long scratch_size;
static struct vertex_block *scratch_blocks;
static long scratch_count, scratch_capacity;
static unsigned long scratch_high;
/* (what a load lent, after it's given back: a block of it freed late is let be) */
static unsigned char *scratch_last_base;
static unsigned long scratch_last_size;


void xbox_ce_scratch_begin(void)
{
	if (!platform_ce_alias || scratch_base || texture_cache_memory_stolen())
		return;
	/* (all the cache lends) */
	scratch_size = (unsigned long)texture_cache_stealable_size();
	scratch_base = (unsigned char *)texture_cache_steal_memory((long)scratch_size);
	scratch_count = 0;
	scratch_high = 0;
	if (!scratch_base)
		scratch_size = 0;
}

void xbox_ce_scratch_end(void)
{
	if (!scratch_base)
		return;
	if (scratch_count)
		platform_log("custom edition: %ld blocks of the load's memory still in use as it's given back", scratch_count);
	platform_log("custom edition: the load took %lu KB of the texture cache's %lu KB, and gave it back", scratch_high >> 10,
		scratch_size >> 10);
	/* (the borrowed memory was the CPU's cached memory, the texture cache's is
	write-combined: what the load left in the CPU's caches written out now, or
	it's written out later, over textures loaded there since) */
	__asm__ volatile("wbinvd" : : : "memory");
	scratch_last_base = scratch_base;
	scratch_last_size = scratch_size;
	scratch_base = NULL;
	scratch_count = 0;
	texture_cache_return_memory();
}

static int scratch_owns(void *address)
{
	return scratch_base && (unsigned char *)address >= scratch_base && (unsigned char *)address < scratch_base + scratch_size;
}

static int scratch_owned(void *address)
{
	return scratch_last_base && (unsigned char *)address >= scratch_last_base &&
		(unsigned char *)address < scratch_last_base + scratch_last_size;
}

void *platform_ce_heap_realloc(void *address, size_t size)
{
	return realloc(address, size);
}

void *platform_ce_scratch_malloc(size_t size)
{
	unsigned long bytes = ((unsigned long)size + SCRATCH_ALIGNMENT - 1) & ~(SCRATCH_ALIGNMENT - 1);
	unsigned long offset = 0;
	long index;

	if (!scratch_base)
		return malloc(size);
	if (!bytes)
		bytes = SCRATCH_ALIGNMENT;
	if (scratch_count == scratch_capacity)
	{
		long capacity = scratch_capacity ? scratch_capacity * 2 : 256;
		struct vertex_block *grown = (struct vertex_block *)realloc(scratch_blocks, capacity * sizeof(*grown));

		if (!grown)
			return NULL;
		scratch_blocks = grown;
		scratch_capacity = capacity;
	}
	for (index = 0; index < scratch_count; index++)
	{
		if (scratch_blocks[index].offset - offset >= bytes)
			break;
		offset = scratch_blocks[index].offset + scratch_blocks[index].size;
	}
	if (bytes > scratch_size - offset)
	{
		platform_log("custom edition: %lu bytes past what the load has (%lu KB)", (unsigned long)size, scratch_size >> 10);
		return NULL;
	}
	memmove(&scratch_blocks[index + 1], &scratch_blocks[index], (scratch_count - index) * sizeof(*scratch_blocks));
	scratch_blocks[index].offset = offset;
	scratch_blocks[index].size = bytes;
	scratch_count++;
	if (offset + bytes > scratch_high)
		scratch_high = offset + bytes;
	return scratch_base + offset;
}

static long scratch_find(void *address)
{
	unsigned long offset = (unsigned long)((unsigned char *)address - scratch_base);
	long low = 0, high = scratch_count - 1;

	while (low <= high)
	{
		long middle = (low + high) / 2;

		if (scratch_blocks[middle].offset == offset)
			return middle;
		if (scratch_blocks[middle].offset < offset)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return -1;
}

void platform_ce_scratch_free(void *address)
{
	long index;

	if (!address)
		return;
	if (!scratch_owns(address))
	{
		if (scratch_owned(address))
		{
			platform_log("custom edition: a block of a load's memory freed after it was given back");
			return;
		}
		free(address);
		return;
	}
	index = scratch_find(address);
	if (index < 0)
		return;
	memmove(&scratch_blocks[index], &scratch_blocks[index + 1], (scratch_count - index - 1) * sizeof(*scratch_blocks));
	scratch_count--;
}

void *platform_ce_scratch_realloc(void *address, size_t size)
{
	long index;
	void *moved;

	if (!address)
		return platform_ce_scratch_malloc(size);
	if (!scratch_owns(address))
		return realloc(address, size);
	index = scratch_find(address);
	if (index < 0)
		return NULL;
	/* (the last block, or one with room after it, grows where it is) */
	{
		unsigned long bytes = ((unsigned long)size + SCRATCH_ALIGNMENT - 1) & ~(SCRATCH_ALIGNMENT - 1);
		unsigned long limit = index + 1 < scratch_count ? scratch_blocks[index + 1].offset : scratch_size;

		if (scratch_blocks[index].offset + bytes <= limit)
		{
			scratch_blocks[index].size = bytes;
			if (scratch_blocks[index].offset + bytes > scratch_high)
				scratch_high = scratch_blocks[index].offset + bytes;
			return address;
		}
	}
	moved = platform_ce_scratch_malloc(size);
	if (!moved)
		return NULL;
	index = scratch_find(address);
	memcpy(moved, address, scratch_blocks[index].size < size ? scratch_blocks[index].size : size);
	platform_ce_scratch_free(address);
	return moved;
}

/* a map's load begun and ended (cache_files.c): the range made whole, the texture
cache's memory lent, and given back */
void platform_ce_load_begin(void)
{
	platform_ce_alias_whole();
	xbox_ce_scratch_begin();
}

void platform_ce_load_end(void)
{
	xbox_ce_scratch_end();
}

/* the game's heap (cseries.h's malloc) in ce_map_checks.c and ce_resources.c:
the same, a block of it the game's own outside a load */
void *debug_malloc(unsigned int size, unsigned char clear, const char *file, long line);
void debug_free(void *pointer, const char *file, long line);
void *debug_realloc(void *pointer, unsigned int size, const char *file, long line);

void *xbox_ce_debug_malloc(unsigned int size, unsigned char clear, const char *file, long line)
{
	void *address;

	if (!scratch_base)
		return debug_malloc(size, clear, file, line);
	address = platform_ce_scratch_malloc(size);
	if (address && clear)
		memset(address, 0, size);
	return address;
}

void xbox_ce_debug_free(void *pointer, const char *file, long line)
{
	if (!pointer)
		return;
	if (scratch_owns(pointer) || scratch_owned(pointer))
		platform_ce_scratch_free(pointer);
	else
		debug_free(pointer, file, line);
}

void *xbox_ce_debug_realloc(void *pointer, unsigned int size, const char *file, long line)
{
	void *moved;

	if (!pointer)
		return xbox_ce_debug_malloc(size, 0, file, line);
	if (scratch_owns(pointer))
		return platform_ce_scratch_realloc(pointer, size);
	if (!scratch_base)
		return debug_realloc(pointer, size, file, line);
	/* (a block of the game's heap grown during a load: there still) */
	moved = debug_realloc(pointer, size, file, line);
	return moved;
}
