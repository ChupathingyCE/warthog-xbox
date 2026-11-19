/*
XBOX_SUPPORT.CPP

What the Xbox link needs besides the game, the SDK libraries and
xbox_dsound.c.

Direct3D: the game links with an Aug 2001 Direct3D (the state numbering of
the January SDK, which later SDKs changed), built to run inside the kernel.
That build allocates through D3DK::MemAlloc and MemFree, which the kernel
provided; here they come from the pool, as there.

The game: fast_ftol_C is a Halo function missing from the reconstruction
(as in port/linux/src/halo_linker_common.c).

The debug monitor: see below.

This file is compiled with the later SDK's headers, not the game's.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

extern "C" {
PVOID WINAPI ExAllocatePoolWithTag(SIZE_T size, ULONG tag);
VOID WINAPI ExFreePool(PVOID memory);
ULONG __cdecl DbgPrint(PCSTR format, ...);
}

/* ---------- Direct3D */

#define D3D_POOL_TAG 'D3DK'

namespace D3DK {

void *WINAPI MemAllocNoZero(DWORD size)
{
	return ExAllocatePoolWithTag(size, D3D_POOL_TAG);
}

void *WINAPI MemAlloc(DWORD size)
{
	void *memory= ExAllocatePoolWithTag(size, D3D_POOL_TAG);

	if (memory)
	{
		ZeroMemory(memory, size);
	}

	return memory;
}

void WINAPI MemFree(void *memory)
{
	if (memory)
	{
		ExFreePool(memory);
	}
}

}

/* ---------- the game */

extern "C" long fast_ftol_C(float value)
{
	long result;

	__asm
	{
		fld value
		fistp result
	}

	return result;
}

/* ---------- the debug monitor

The game walks the loaded modules' sections through xbdm.dll, which only a
development kit or a debug BIOS loads; a title importing it does not start
on a retail console. Its one use is shell_xbox.c's start, which makes the
section holding the game's code read-only, so a stray write into the code
faults where it is made instead of turning an instruction into another one
later (a blue screen with C000001D, illegal instruction, at a function's
second instruction).

Here the walk is answered from the XBE's own header, which the kernel keeps
at 0x10000: one module (the XBE), and its sections loaded at the start. */

#define XBDM_NOERR ((HRESULT)0x02DB0000L)
#define XBDM_ENDOFLIST ((HRESULT)0x82DB0104L)

/* xbdm.h's notification structures, as the game's walk fills them */
struct xbox_dm_module
{
	char name[MAX_PATH];
	PVOID base_address;
	ULONG size;
	ULONG time_stamp;
	ULONG checksum;
	ULONG flags;
};

struct xbox_dm_section
{
	char name[MAX_PATH];
	PVOID base_address;
	ULONG size;
	USHORT index;
	USHORT flags;
};

/* the XBE's image header and its section headers (the parts read here) */
#define XBE_BASE_ADDRESS 0x00010000UL
#define XBE_MAGIC 0x48454258UL /* 'XBEH' */
#define XBE_SECTION_PRELOAD 0x00000002UL

struct xbe_section_header
{
	ULONG flags;
	ULONG virtual_address;
	ULONG virtual_size;
	ULONG raw_address;
	ULONG raw_size;
	ULONG name_address;
	ULONG name_reference_count;
	ULONG head_shared_page_reference_count_address;
	ULONG tail_shared_page_reference_count_address;
	UCHAR digest[20];
};

extern "C" void xbox_boot_log(const char *format, ...);

static ULONG const *xbe_header(void)
{
	ULONG const *header = (ULONG const *)XBE_BASE_ADDRESS;

	return header[0] == XBE_MAGIC && header[0x104 / 4] == XBE_BASE_ADDRESS ? header : NULL;
}

extern "C" HRESULT WINAPI DmWalkLoadedModules(void **walk, void *module)
{
	ULONG const *header = xbe_header();
	struct xbox_dm_module *out = (struct xbox_dm_module *)module;

	/* (one module: the second call ends the list) */
	if (!walk || *walk || !out || !header)
		return XBDM_ENDOFLIST;
	*walk = (void *)1;
	ZeroMemory(out, sizeof(*out));
	lstrcpynA(out->name, "default.xbe", sizeof(out->name));
	out->base_address = (PVOID)XBE_BASE_ADDRESS;
	out->size = header[0x10C / 4];
	out->time_stamp = header[0x114 / 4];
	out->flags = 0x0001; /* DMN_MODFLAG_XBE */
	return XBDM_NOERR;
}

extern "C" HRESULT WINAPI DmWalkModuleSections(void **walk, const char *name, void *section)
{
	ULONG const *header = xbe_header();
	struct xbox_dm_section *out = (struct xbox_dm_section *)section;
	struct xbe_section_header const *sections;
	ULONG count, index;

	(void)name;
	if (!walk || !out || !header)
		return XBDM_ENDOFLIST;
	count = header[0x11C / 4];
	sections = (struct xbe_section_header const *)header[0x120 / 4];
	/* (*walk: the next section's index, plus one; NULL, the first) */
	for (index = (ULONG)(ULONG_PTR)*walk; index < count; index++)
	{
		/* (only the sections the kernel loaded at the start) */
		if (!(sections[index].flags & XBE_SECTION_PRELOAD))
			continue;
		ZeroMemory(out, sizeof(*out));
		if (sections[index].name_address)
			lstrcpynA(out->name, (const char *)sections[index].name_address, sizeof(out->name));
		out->base_address = (PVOID)sections[index].virtual_address;
		out->size = sections[index].virtual_size;
		out->index = (USHORT)index;
		out->flags = 0x0001; /* DMN_SECFLAG_LOADED */
		*walk = (void *)(ULONG_PTR)(index + 1);
		return XBDM_NOERR;
	}
	return XBDM_ENDOFLIST;
}

/* (the game closes the walk once it has made its code's section read-only:
said in debug.txt, as the kernel has it now, over the whole section, from
its first page to its last. The walk's handle names the section the game
protected (DmWalkModuleSections: its index, plus one). VirtualQuery gives a
run of pages of one protection from the page asked about, so one query from
this function's own page saw only the run from there on: "448 KB at
0x00217000" of a 2.5 MB .text that starts at 0x00011000) */
#define CODE_WRITABLE_RUNS_LOGGED 4

static int code_page_writable(DWORD protect)
{
	return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

extern "C" HRESULT WINAPI DmCloseModuleSections(void *walk)
{
	ULONG const *header = xbe_header();
	struct xbe_section_header const *sections;
	ULONG index = (ULONG)(ULONG_PTR)walk, count, start, end, address;
	ULONG protected_bytes = 0, writable_bytes = 0, unknown_bytes = 0, writable_runs = 0;
	char runs[CODE_WRITABLE_RUNS_LOGGED * 24 + 8], detail[sizeof(runs) + 64];
	const char *name;
	int used = 0;

	if (!header)
		return 0;
	count = header[0x11C / 4];
	sections = (struct xbe_section_header const *)header[0x120 / 4];
	/* (the section the walk stopped at; without one, the section holding
	this function, which is in the same .text as the game's own check) */
	if (!index || index > count)
	{
		for (index = 0; index < count; index++)
		{
			ULONG here = (ULONG)(ULONG_PTR)&DmCloseModuleSections;

			if (here >= sections[index].virtual_address &&
				here < sections[index].virtual_address + sections[index].virtual_size)
				break;
		}
		if (index == count)
			return 0;
		index++;
	}
	index--;
	start = sections[index].virtual_address & ~0xFFFUL;
	end = (sections[index].virtual_address + sections[index].virtual_size + 0xFFF) & ~0xFFFUL;
	runs[0] = 0;
	detail[0] = 0;
	for (address = start; address < end;)
	{
		MEMORY_BASIC_INFORMATION information;
		ULONG run_end;

		if (!VirtualQuery((LPCVOID)address, &information, sizeof(information)) || !information.RegionSize)
		{
			/* (a page the kernel would not describe: counted apart, and the
			next page asked about) */
			unknown_bytes += 0x1000;
			address += 0x1000;
			continue;
		}
		run_end = (ULONG)(ULONG_PTR)information.BaseAddress + (ULONG)information.RegionSize;
		if (run_end > end || run_end <= address)
			run_end = end;
		/* (pages not committed have no protection to speak of: counted with
		the ones the kernel would not describe, not as read-only) */
		if (information.State != MEM_COMMIT)
		{
			unknown_bytes += run_end - address;
			address = run_end;
			continue;
		}
		if (code_page_writable(information.Protect))
		{
			writable_bytes += run_end - address;
			if (writable_runs < CODE_WRITABLE_RUNS_LOGGED)
			{
				int written = _snprintf(runs + used, sizeof(runs) - used - 1, "%s0x%08lX-0x%08lX",
					writable_runs ? ", " : "", (unsigned long)address, (unsigned long)run_end);

				if (written > 0)
					used += written;
			}
			writable_runs++;
		}
		else
		{
			protected_bytes += run_end - address;
		}
		address = run_end;
	}
	runs[sizeof(runs) - 1] = 0;
	name = sections[index].name_address ? (const char *)sections[index].name_address : "?";
	if (!writable_bytes && !unknown_bytes)
	{
		xbox_boot_log("code: %s 0x%08lX-0x%08lX, all %lu KB read-only", name, (unsigned long)start,
			(unsigned long)end, (unsigned long)(end - start) >> 10);
		return 0;
	}
	if (writable_bytes)
	{
		_snprintf(detail, sizeof(detail) - 1, "; STILL WRITABLE: %lu KB in %lu run%s (%s%s)",
			(unsigned long)writable_bytes >> 10, (unsigned long)writable_runs, writable_runs == 1 ? "" : "s", runs,
			writable_runs > CODE_WRITABLE_RUNS_LOGGED ? ", ..." : "");
		detail[sizeof(detail) - 1] = 0;
	}
	if (unknown_bytes)
	{
		size_t length = strlen(detail);

		_snprintf(detail + length, sizeof(detail) - length - 1, "; %lu KB not committed or not described by the kernel",
			(unsigned long)unknown_bytes >> 10);
		detail[sizeof(detail) - 1] = 0;
	}
	xbox_boot_log("code: %s 0x%08lX-0x%08lX, %lu of %lu KB read-only%s", name, (unsigned long)start,
		(unsigned long)end, (unsigned long)protected_bytes >> 10, (unsigned long)(end - start) >> 10, detail);
	return 0;
}
