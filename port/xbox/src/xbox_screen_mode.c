/*
XBOX_SCREEN_MODE.C

The screen's choices and sums (xbox_screen.h), without the SDK, so that the
host's tests run them (tests/screen_test.c).
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_screen.h"

int snprintf(char *buffer, size_t size, const char *format, ...);

long xbox_screen_choose_width(unsigned long video_flags, const char *setting)
{
	if (setting && !strcmp(setting, "off"))
		return XBOX_SCREEN_WIDTH;
	return (video_flags & XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN) ? XBOX_SCREEN_WIDE_WIDTH : XBOX_SCREEN_WIDTH;
}

/* (the nearest, halves away from zero, for either sign) */
static long scale_nearest(long value, long numerator, long denominator)
{
	long product = value * numerator;

	if (product >= 0)
		return (product + denominator / 2) / denominator;
	return -((-product + denominator / 2) / denominator);
}

long xbox_screen_interface_x(long width, long physical_x)
{
	return scale_nearest(physical_x, width, XBOX_SCREEN_WIDTH);
}

long xbox_screen_physical_x(long width, long interface_x)
{
	return scale_nearest(interface_x, XBOX_SCREEN_WIDTH, width);
}

static const char *av_pack_name(unsigned long av_pack)
{
	/* (XC_AV_PACK_*, xbox.h) */
	switch (av_pack)
	{
	case 0:
		return "SCART";
	case 1:
		return "component";
	case 2:
		return "VGA";
	case 3:
		return "RF";
	case 4:
		return "S-Video";
	case 6:
		return "composite";
	default:
		return "unknown cable";
	}
}

void xbox_screen_mode_text(char *text, unsigned long size, long width, unsigned long mode_flags,
	unsigned long refresh_rate, unsigned long av_pack)
{
	/* (D3DPRESENTFLAG_PROGRESSIVE, d3d8types.h) */
	const char *scan = (mode_flags & 0x00000040UL) ? "480p" : "480i";

	if (!text || !size)
		return;
	snprintf(text, size, "screen: %s %lu Hz %s (%s), %ld units across", scan, refresh_rate,
		(mode_flags & XBOX_SCREEN_PRESENT_FLAG_WIDESCREEN) ? "widescreen" : "4:3", av_pack_name(av_pack), width);
}
