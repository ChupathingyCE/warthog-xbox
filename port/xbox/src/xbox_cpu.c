/*
XBOX_CPU.C

The console's processor, said once at the start (xbox_memory_startup):
what CPUID calls it, and its clock as measured, so a console with an
upgraded processor (a 1.4 GHz Tualatin in place of the 733 MHz Pentium III)
shows in every debug.txt:

  cpu: GenuineIntel family 6 model 8 stepping 10 (0x0068A), brand id 0x00, 733 MHz (measured over 20 ms)

The brand string follows, in quotes, on a processor that has one (CPUID's
0x80000002 to 0x80000004; the stock processor does not).

The clock is the time stamp counter's count over about 20 ms of the
kernel's performance counter (QueryPerformanceCounter, the ACPI timer's
3.375 MHz on the Xbox), which runs apart from the processor's clock. The
20 ms are spent once, before the game's own start.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

void xbox_boot_log(const char *format, ...);

static void cpuid(unsigned long leaf, unsigned long registers[4])
{
	unsigned long a, b, c, d;

	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(0));
	registers[0] = a;
	registers[1] = b;
	registers[2] = c;
	registers[3] = d;
}

static unsigned __int64 time_stamp(void)
{
	unsigned __int64 value;

	__asm__ volatile("rdtsc" : "=A"(value));
	return value;
}

/* the processor's clock in MHz, 0 if the kernel's counter cannot say */
static unsigned long clock_measure(void)
{
	LARGE_INTEGER frequency, start, now;
	unsigned __int64 counts, ticks;
	unsigned long interval;

	/* (the counter's rate fits 32 bits: no 64-bit division, which is the C
	runtime's) */
	if (!QueryPerformanceFrequency(&frequency) || frequency.HighPart || !frequency.LowPart)
		return 0;
	interval = frequency.LowPart / 50;
	QueryPerformanceCounter(&start);
	/* (start on a fresh count of the counter, so the interval is whole) */
	do
		QueryPerformanceCounter(&now);
	while (now.QuadPart == start.QuadPart);
	start = now;
	counts = time_stamp();
	do
		QueryPerformanceCounter(&now);
	while (now.QuadPart - start.QuadPart < (LONGLONG)interval);
	counts = time_stamp() - counts;
	ticks = (unsigned __int64)(now.QuadPart - start.QuadPart);
	if (!ticks)
		return 0;
	/* (in floating point, for the same reason) */
	return (unsigned long)((double)(__int64)counts * (double)frequency.LowPart / (double)(__int64)ticks / 1000000.0 +
		0.5);
}

void xbox_cpu_log(void)
{
	unsigned long registers[4], signature, family, model, stepping, mhz;
	char vendor[13], brand[49], text[64];

	cpuid(0, registers);
	memcpy(vendor, &registers[1], 4);
	memcpy(vendor + 4, &registers[3], 4);
	memcpy(vendor + 8, &registers[2], 4);
	vendor[12] = 0;
	brand[0] = 0;
	signature = 0;
	registers[1] = 0;
	if (registers[0] >= 1)
	{
		cpuid(1, registers);
		signature = registers[0];
	}
	/* (Intel's: the extended family and model count only for family 15,
	and 6 for the model) */
	family = (signature >> 8) & 0xF;
	model = (signature >> 4) & 0xF;
	stepping = signature & 0xF;
	if (family == 0xF)
		family += (signature >> 20) & 0xFF;
	if (family == 0x6 || family >= 0xF)
		model += ((signature >> 16) & 0xF) << 4;
	{
		unsigned long brand_id = registers[1] & 0xFF;

		cpuid(0x80000000UL, registers);
		if (registers[0] >= 0x80000004UL && registers[0] < 0x8000FFFFUL)
		{
			unsigned long leaf;
			const char *first;

			for (leaf = 0; leaf < 3; leaf++)
			{
				cpuid(0x80000002UL + leaf, registers);
				memcpy(brand + leaf * 16, registers, 16);
			}
			brand[48] = 0;
			/* (Intel's string is right-aligned with spaces before it) */
			for (first = brand; *first == ' '; first++)
				;
			memmove(brand, first, strlen(first) + 1);
		}
		mhz = clock_measure();
		text[0] = 0;
		if (mhz)
			_snprintf(text, sizeof(text) - 1, "%lu MHz (measured over 20 ms)", mhz);
		else
			strcpy(text, "clock not measured (no performance counter)");
		text[sizeof(text) - 1] = 0;
		xbox_boot_log("cpu: %s family %lu model %lu stepping %lu (0x%05lX), brand id 0x%02lX, %s%s%s%s", vendor,
			family, model, stepping, signature & 0xFFFFFUL, brand_id, text, brand[0] ? ", \"" : "", brand,
			brand[0] ? "\"" : "");
	}
}
