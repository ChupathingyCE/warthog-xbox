/*
XBOX_SAVES.H

Warthog's identity on the console: its own title ID in the XBE's
certificate (port/xbox/build.py), 0x43480001 ("CH" and 1). Builds before
0.9.1 had cxbe's default, 0xFFFF0002, which every cxbe-built program that
keeps it shares: their saves sat in the same E:\UDATA\ffff0002 and they
shared a Z: cache partition. The old ID is the certificate's alternate
title ID, so the game may still mount the old folder, and the profiles
there are copied over once (src/xbox_saves.c). Never the retail game's
0x4D530004.
*/

#ifndef __XBOX_SAVES_H
#define __XBOX_SAVES_H

/* (port/xbox/build.py passes the same numbers to cxbe) */
#define XBOX_TITLE_ID 0x43480001UL
#define XBOX_OLD_TITLE_ID 0xFFFF0002UL

/* at the start, after the settings (xbox_memory_startup): the game's saves
under the old title ID (a folder with blam.sav or blam.lst) copied to U:\
once, never over one already there, each said in debug.txt; a marker in
T:\ says it is done */
void xbox_saves_move_old(void);

#endif
