/*
XBOX_CONFIG.C

The console's settings (xbox_config.h): D:\config.toml, through the desktop
builds' port_config.c. The old files beside default.xbe it replaced are
read only by a build configured with --xbox-old-flag-files
(XBOX_OLD_FLAG_FILES): their release of grace is over, and the retail game
read no such files.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_config.h"
#include "../include/xbox_memory.h"
#include "port/linux/src/port_config.h"

int snprintf(char *buffer, size_t size, const char *format, ...);
void platform_log(const char *format, ...);

#ifdef XBOX_OLD_FLAG_FILES
/* the switches that were empty files */
static const struct
{
	const char *file;
	const char *setting;
} old_switches[] =
{
	{ "D:\\bypass_security.txt", "console.bypass_security" },
	{ "D:\\memory_64.txt", "console.simulate_64mb" },
	{ "D:\\large_caches.txt", "console.large_caches" },
	{ "D:\\trace.txt", "console.trace" },
};

static int file_exists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}
#endif

void xbox_config_startup(void)
{
	static int started;
#ifdef XBOX_OLD_FLAG_FILES
	unsigned int index;
#endif

	if (started)
		return;
	started = 1;
	/* (the file read now, written with the defaults if it is missing: its
	few allocations are the C runtime heap's, which the start already has,
	and freed before the memory is laid out) */
	config_boolean("console.bypass_security");
#ifdef XBOX_OLD_FLAG_FILES
	for (index = 0; index < sizeof(old_switches) / sizeof(old_switches[0]); index++)
	{
		if (!file_exists(old_switches[index].file))
			continue;
		xbox_boot_log("%s is deprecated, use %s in config.toml", old_switches[index].file, old_switches[index].setting);
		config_set(old_switches[index].setting, "true");
	}
#endif
	/* internet play reaches the internet only when XNet starts open */
	if (!config_boolean("console.bypass_security"))
		config_set("network.online", "false");
}

unsigned long xbox_config_line(const char *name, const char *old_file, char *text, unsigned long size)
{
#ifdef XBOX_OLD_FLAG_FILES
	static char told[2][32];
	unsigned long length;
	unsigned int index;
	FILE *file;
#endif
	const char *value = config_string(name);

	if (!size)
		return 0;
	if (*value)
	{
		snprintf(text, size, "%s", value);
		return (unsigned long)strlen(text);
	}
	text[0] = 0;
#ifndef XBOX_OLD_FLAG_FILES
	(void)old_file;
	return 0;
#else
	file = fopen(old_file, "rb");
	if (!file)
		return 0;
	length = (unsigned long)fread(text, 1, size - 1, file);
	fclose(file);
	text[length] = 0;
	/* (said once for each file) */
	for (index = 0; index < sizeof(told) / sizeof(told[0]); index++)
	{
		if (!strcmp(told[index], old_file))
			break;
		if (!told[index][0])
		{
			snprintf(told[index], sizeof(told[index]), "%s", old_file);
			platform_log("%s is deprecated, use %s in config.toml", old_file, name);
			break;
		}
	}
	return length;
#endif
}
