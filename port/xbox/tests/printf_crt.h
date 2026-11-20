/* the console's C runtime's, as port/xbox/tests/printf_test.c plays them */
#include <stdarg.h>
#include <stddef.h>
int _vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int _vscprintf(const char *format, va_list arguments);
