/*
XBOX_PRINTF.C

C99's snprintf and vsnprintf, which the console's C runtime (2003's) lacks,
on its _vsnprintf and _vscprintf. Apart from xbox_port.c so the host's tests
run them against a stand-in for that runtime (port/xbox/tests/printf_test.c).

They say what C99's do: the length the whole text needs, however much of it
fit (negative on an error), so a caller's "length >= size" check sees a cut.
The first ones said size - 1 for a cut text, and every such check in the
shared code let it through: a game report of 10 or more players went out
cut at 2047 bytes with the whole body's Content-Length, and the site waited
for the rest and answered 408 (#140).
*/

#include <stdarg.h>
#include <stdio.h>

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	va_list again;
	int length;

	if (!size)
		return _vscprintf(format, arguments);
	va_copy(again, arguments);
	length = _vsnprintf(buffer, size, format, arguments);
	/* _vsnprintf leaves a full buffer unterminated (and says -1, or size
	when it just fits): terminate it then, and only then. size can be more
	than the buffer (csprintf passes MAXIMUM_STRING_SIZE for any buffer),
	so writing buffer[size - 1] every time wrote a zero up to 8 KB past a
	small buffer */
	if (length < 0 || (size_t)length >= size)
		buffer[size - 1] = 0;
	/* (a cut: the whole text's length, counted without writing it) */
	if (length < 0)
		length = _vscprintf(format, again);
	va_end(again);
	return length;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return length;
}
