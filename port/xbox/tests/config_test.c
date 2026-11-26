/*
CONFIG_TEST.C

The console's D:\config.toml (port/linux/src/port_config.c built as the
console builds it, HALO_XBOX_CONSOLE) on the host, in a folder of its own
where "d:\config.toml" is a file's name: port/xbox/tests/run.sh.

    config_test new     no file: the defaults are written, the console's
                        settings only, and read
    config_test given   a file with a few values (as tools/xbox_package.py
                        writes it): they are read, and the rest added
    config_test wide_named      a file from before the 4:3 default that
                        names display.widescreen = "dashboard": it keeps it
    config_test wide_commented  one that left it commented out at
                        "dashboard": it follows the new default, "off"
    config_test keys    no file: every setting the new one has, set or
                        commented out at its default, is one the console
                        reads (warthog-xbox #193), and so are the two
                        tools/xbox_package.py writes
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_config.h"

#define CONFIG_FILE "d:\\config.toml"

static int failures;

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("  log: ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

static void expect(int condition, const char *what)
{
	if (!condition)
	{
		failures++;
		fprintf(stderr, "config_test: %s\n", what);
	}
}

static char *file_text(void)
{
	size_t size = 0;

	return config_file_read(CONFIG_FILE, &size);
}

static void expect_shared(void)
{
	/* the shared settings marked for the console, at their defaults */
	expect(config_boolean("network.online") == 1, "network.online is not true");
	expect(config_real("audio.music_volume") == 1.0, "audio.music_volume is not 1.0");
	expect(!strcmp(config_string("game.console_log"), "important"), "game.console_log is not \"important\"");
	/* and those that are not, empty, 0 or false, as before the file */
	expect(config_boolean("display.interpolation") == 0, "display.interpolation is not false");
	expect(config_boolean("network.allow_upnp") == 0, "network.allow_upnp is not false");
	expect(!strcmp(config_string("display.player_names"), ""), "display.player_names is not empty");
}

/* every "key = value" line of a config.toml, and its commented-out
"# key = value" ones (a setting at its default), under its [section]: each
one of the console's settings. The number of them checked, or -1 */
static int keys_checked(const char *text)
{
	char section[64] = "";
	const char *line;
	int count = 0;

	for (line = text; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0))
	{
		const char *key = line;
		size_t length;
		char name[160];

		if (*line == '[')
		{
			length = strcspn(line + 1, "]\n");
			if (line[1 + length] != ']' || length >= sizeof(section))
				return -1;
			memcpy(section, line + 1, length);
			section[length] = 0;
			continue;
		}
		if (key[0] == '#' && key[1] == ' ')
			key += 2;
		for (length = 0; islower((unsigned char)key[length]) || isdigit((unsigned char)key[length]) ||
			key[length] == '_'; length++)
			;
		/* (help text and the layout's own line are not settings) */
		if (!length || strncmp(key + length, " = ", 3) || !section[0])
			continue;
		snprintf(name, sizeof(name), "%s.%.*s", section, (int)length, key);
		if (config_setting_kind(name) < 0)
		{
			failures++;
			fprintf(stderr, "config_test: %s is in the new config.toml but not one of the console's\n", name);
		}
		count++;
	}
	return count;
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";
	char *text;

	if (!strcmp(mode, "new"))
	{
		remove(CONFIG_FILE);
		expect(config_boolean("console.debug_geometry") == 0, "console.debug_geometry is not false");
		expect(!strcmp(config_string("console.log_flush"), "buffered"), "console.log_flush is not buffered");
		expect(!strcmp(config_string("console.join"), ""), "console.join is not empty");
		expect(!strcmp(config_string("console.delta"), "auto"), "console.delta is not auto");
		expect(!strcmp(config_string("console.memory_class"), "auto"), "console.memory_class is not auto");
		expect(!strcmp(config_string("display.widescreen"), "off"), "display.widescreen is not off");
		expect(config_integer("display.frame_rate") == 30, "display.frame_rate is not 30");
		expect(config_integer("display.flicker_filter") == 5, "display.flicker_filter is not 5");
		expect_shared();
		text = file_text();
		expect(text != NULL, "no config.toml written");
		if (text)
		{
			expect(strstr(text, "config_version = 2\n") != NULL, "no config_version");
			expect(strstr(text, "\n[console]\n") != NULL, "no [console]");
			expect(strstr(text, "# bypass_security = false\n") != NULL, "no bypass_security");
			expect(strstr(text, "# debug_geometry = false\n") != NULL, "no debug_geometry");
			expect(strstr(text, "# log_flush = \"buffered\"\n") != NULL, "no log_flush");
			expect(strstr(text, "# memory_class = \"auto\"\n") != NULL, "no memory_class");
			expect(strstr(text, "# widescreen = \"off\"\n") != NULL, "no display.widescreen");
			expect(strstr(text, "# frame_rate = 30\n") != NULL, "no display.frame_rate");
			expect(strstr(text, "# flicker_filter = 5\n") != NULL, "no display.flicker_filter");
			expect(strstr(text, "# game_list_server = \"\"\n") != NULL, "no game_list_server");
			expect(strstr(text, "# online = true\n") != NULL, "no network.online");
			expect(strstr(text, "fullscreen") == NULL, "a desktop setting written");
			expect(strstr(text, "HALO_") == NULL, "an environment variable written");
			free(text);
		}
		/* a setting for this run only: the file stays */
		text = file_text();
		expect(config_set("console.trace", "true") && config_boolean("console.trace"), "config_set did not set");
		{
			char *after = file_text();

			expect(text && after && !strcmp(text, after), "config_set changed the file");
			free(after);
		}
		free(text);
	}
	else if (!strcmp(mode, "given"))
	{
		FILE *file = fopen(CONFIG_FILE, "wb");

		if (!file)
			return 1;
		fputs("# The layout of this file, for the game: leave it.\nconfig_version = 2\n\n[console]\n"
			"bypass_security = true\ndebug_geometry = true\nlog_flush = \"every_line\"\n"
			"join = \"192.168.1.20\"\ndelta = \"off\"\nmemory_class = \"64\"\n", file);
		fclose(file);
		expect(config_boolean("console.bypass_security") == 1, "console.bypass_security is not true");
		expect(config_boolean("console.debug_geometry") == 1, "console.debug_geometry is not true");
		expect(!strcmp(config_string("console.log_flush"), "every_line"), "console.log_flush is not every_line");
		/* (xbox_memory.c: "64" holds the console to the 64 MB baseline) */
		expect(!strcmp(config_string("console.memory_class"), "64"), "console.memory_class is not 64");
		expect(!strcmp(config_string("console.join"), "192.168.1.20"), "console.join is not the address");
		/* (the switch delta_peer_protocol reads: "off" keeps the legacy protocol alone) */
		expect(!strcmp(config_string("console.delta"), "off"), "console.delta is not off");
		expect_shared();
		text = file_text();
		expect(text && strstr(text, "debug_geometry = true\n") != NULL, "the value written is gone");
		expect(text && strstr(text, "# trace = false\n") != NULL, "the missing settings were not added");
		free(text);
	}
	else if (!strcmp(mode, "wide_named") || !strcmp(mode, "wide_commented"))
	{
		/* (warthog-xbox #114: the picture is 4:3 by default now; a player
		who chose widescreen keeps it) */
		int named = !strcmp(mode, "wide_named");
		FILE *file = fopen(CONFIG_FILE, "wb");

		if (!file)
			return 1;
		fputs("# The layout of this file, for the game: leave it.\nconfig_version = 2\n\n[display]\n", file);
		fputs(named ? "widescreen = \"dashboard\"\n" : "# widescreen = \"dashboard\"\n", file);
		fclose(file);
		expect(!strcmp(config_string("display.widescreen"), named ? "dashboard" : "off"),
			named ? "a named \"dashboard\" was not kept" : "a commented-out \"dashboard\" did not follow \"off\"");
	}
	else if (!strcmp(mode, "keys"))
	{
		int count;

		remove(CONFIG_FILE);
		expect(config_boolean("console.bypass_security") == 0, "console.bypass_security is not false");
		text = file_text();
		expect(text != NULL, "no config.toml written");
		count = text ? keys_checked(text) : -1;
		/* (a parse that found next to nothing would pass for nothing) */
		expect(count >= 20, "next to no settings found in the new config.toml");
		/* (the ones tools/xbox_package.py sets) */
		expect(config_setting_kind("console.bypass_security") == CONFIG_KIND_BOOLEAN, "no console.bypass_security");
		expect(config_setting_kind("console.game_list_server") == CONFIG_KIND_STRING, "no console.game_list_server");
		/* (and a desktop one is not, or the check checks nothing) */
		expect(config_setting_kind("display.fullscreen") < 0, "display.fullscreen is one of the console's");
		if (count >= 0 && !failures)
			printf("config_test keys: %d settings in the new config.toml, all the console's\n", count);
		free(text);
	}
	else
	{
		fprintf(stderr, "usage: config_test new|given|wide_named|wide_commented|keys\n");
		return 2;
	}
	if (failures)
		return 1;
	printf("config_test %s: ok\n", mode);
	return 0;
}
