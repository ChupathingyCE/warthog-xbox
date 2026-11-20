/*
PRINTF_TEST.C

The console's snprintf and vsnprintf (port/xbox/src/xbox_printf.c) on the
host, over a stand-in for its C runtime's _vsnprintf as 2003's behaves: a
text that fits is written and terminated; one exactly the buffer's size is
written without its terminator (and says its length); a longer one is cut,
unterminated, and says -1. Built with the unit's names changed to
xbox_snprintf and xbox_vsnprintf (port/xbox/tests/run.sh), so the host's
own are what the stand-in uses.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printf_crt.h"

int xbox_snprintf(char *buffer, size_t size, const char *format, ...);

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "printf_test: line %d: %s\n", __LINE__, #condition); } } while (0)

int _vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	char whole[16384];
	int length = vsnprintf(whole, sizeof(whole), format, arguments);

	if (length < 0 || length >= (int)sizeof(whole))
		return -1;
	if ((size_t)length < size)
	{
		memcpy(buffer, whole, (size_t)length + 1);
		return length;
	}
	memcpy(buffer, whole, size);
	return (size_t)length == size ? length : -1;
}

int _vscprintf(const char *format, va_list arguments)
{
	return vsnprintf(NULL, 0, format, arguments);
}

int main(void)
{
	char small[8], *big, *report;
	int length, index;

	/* fits */
	memset(small, 'x', sizeof(small));
	CHECK(xbox_snprintf(small, sizeof(small), "%s", "abc") == 3 && !strcmp(small, "abc"));
	CHECK(xbox_snprintf(small, sizeof(small), "%d", 1234567) == 7 && !strcmp(small, "1234567"));
	/* one too many: cut, terminated, and the whole length said */
	memset(small, 'x', sizeof(small));
	CHECK(xbox_snprintf(small, sizeof(small), "%d", 12345678) == 8 && !strcmp(small, "1234567"));
	/* far too many */
	memset(small, 'x', sizeof(small));
	CHECK(xbox_snprintf(small, sizeof(small), "%s and %d more", "a longer text", 42) == 25 &&
		!strcmp(small, "a longe"));
	/* no room: nothing written, the length said */
	memset(small, 'x', sizeof(small));
	CHECK(xbox_snprintf(small, 0, "%s", "abc") == 3 && small[0] == 'x');
	CHECK(xbox_snprintf(NULL, 0, "%s", "abcdef") == 6);
	/* a size larger than the text's buffer (csprintf's): written as it is */
	CHECK(xbox_snprintf(small, 8192, "%s", "abc") == 3 && !strcmp(small, "abc"));

	/* warthog-xbox #140: a POST of a 20-player report into the 2048-byte
	request says its whole length, so the "length >= sizeof(request)"
	check sees the cut */
	report = malloc(8192);
	big = malloc(2048);
	CHECK(report && big);
	if (report && big)
	{
		strcpy(report, "{\"players\": [");
		for (index = 0; index < 20; index++)
		{
			size_t used = strlen(report);

			snprintf(report + used, 8192 - used, "%s{\"name\": \"Player %02d\", \"team\": %d, \"score\": %d, "
				"\"kills\": %d, \"deaths\": %d, \"shots_fired\": %d, \"shots_hit\": %d, \"best_spree\": %d}",
				index ? ", " : "", index, index & 1, 20 - index, 20 - index, index, 300 + index, 100 + index,
				index % 5);
		}
		strcat(report, "]}");
		length = xbox_snprintf(big, 2048, "POST /v1/games HTTP/1.0\r\nContent-Length: %lu\r\n\r\n%s",
			(unsigned long)strlen(report), report);
		CHECK(strlen(report) > 2048);
		CHECK(length == snprintf(NULL, 0, "POST /v1/games HTTP/1.0\r\nContent-Length: %lu\r\n\r\n%s",
			(unsigned long)strlen(report), report));
		CHECK(strlen(big) == 2047);
	}
	free(report);
	free(big);
	if (failures)
	{
		fprintf(stderr, "printf_test: %d failed\n", failures);
		return 1;
	}
	puts("printf_test: snprintf says the whole length, cut or not");
	return 0;
}
