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
said in debug.txt, as the kernel has it now) */
extern "C" HRESULT WINAPI DmCloseModuleSections(void *walk)
{
	MEMORY_BASIC_INFORMATION information;

	(void)walk;
	if (VirtualQuery((LPCVOID)&DmCloseModuleSections, &information, sizeof(information)))
	{
		xbox_boot_log("code: %lu KB at 0x%08lX %s", (unsigned long)(information.RegionSize >> 10),
			(unsigned long)(ULONG_PTR)information.BaseAddress,
			information.Protect == PAGE_READONLY ? "read-only" : "still writable");
	}
	return 0;
}
