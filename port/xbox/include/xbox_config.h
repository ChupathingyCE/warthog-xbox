/*
XBOX_CONFIG.H

The console's settings: D:\config.toml beside default.xbe, read and written
by the desktop builds' port/linux/src/port_config.c (its settings marked
_platform_xbox, the console's own in [console]). config_boolean and the
rest are port_config.h's.

The empty files beside default.xbe that were the console's switches before
it (D:\bypass_security.txt, memory_64.txt, large_caches.txt, trace.txt),
and join.txt and game_list.txt, are read only by a build configured with
--xbox-old-flag-files (XBOX_OLD_FLAG_FILES), each said in debug.txt to be
deprecated. Their release of grace is over.
*/

#ifndef __XBOX_CONFIG_H
#define __XBOX_CONFIG_H

/* the first thing main does (xbox_memory_startup, before the memory, whose
settings are among these): config.toml read, written with the defaults if
it is missing, the old switch files taken (XBOX_OLD_FLAG_FILES only), and internet play off unless the
network starts open (console.bypass_security) */
void xbox_config_startup(void);

/* a string setting's text (console.join), else (XBOX_OLD_FLAG_FILES only)
the first line of the old file it replaces (D:\join.txt, read each time),
at most size - 1
characters and a terminator: its length, 0 if neither has one */
unsigned long xbox_config_line(const char *name, const char *old_file, char *text, unsigned long size);

#endif
