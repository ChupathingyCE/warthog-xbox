/*
The console's screen choices and sums (src/xbox_screen_mode.c), on the host:
when the interface is wide, how its units and the 640 pixels convert for the
title-safe frame and the split screen windows, and the mode's debug.txt line.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_screen.h"

static int failures;

static void expect(int condition, const char *what)
{
	if (!condition)
	{
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void expect_width(void)
{
	expect(xbox_screen_choose_width(0, "dashboard") == 640, "4:3 dashboard is not 640");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "dashboard") == 852,
		"widescreen dashboard is not 852");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "off") == 640, "off is not 640");
	expect(xbox_screen_choose_width(0, "off") == 640, "off on 4:3 is not 640");
	/* (off as players write it) */
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "Off") == 640, "Off is not 640");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, " OFF ") == 640, "OFF is not 640");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "4:3") == 640, "4:3 is not 640");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "false") == 640, "false is not 640");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "offf") == 852, "offf is off");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "off the dashboard, please") == 852,
		"a long setting is off");
	/* (the dashboard's 480p, 720p, 1080i and letterbox bits change nothing) */
	expect(xbox_screen_choose_width(0x08 | 0x02 | 0x04 | 0x10, "dashboard") == 640, "HDTV bits made it wide");
	expect(xbox_screen_choose_width(0x08 | XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "dashboard") == 852,
		"480p widescreen is not 852");
	/* (an unknown or missing setting follows the dashboard) */
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, "") == 852, "empty setting is not dashboard");
	expect(xbox_screen_choose_width(XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN, NULL) == 852, "no setting is not dashboard");
}

/* display.frame_rate and display.flicker_filter */
static void expect_frames_and_filter(void)
{
	expect(xbox_screen_choose_frame_rate(30) == 30, "30 is not 30");
	expect(xbox_screen_choose_frame_rate(60) == 60, "60 is not 60");
	/* (anything else is the game's 30: 0 from a setting that isn't a number) */
	expect(xbox_screen_choose_frame_rate(0) == 30, "0 is not 30");
	expect(xbox_screen_choose_frame_rate(59) == 30, "59 is not 30");
	expect(xbox_screen_choose_frame_rate(120) == 30, "120 is not 30");
	expect(xbox_screen_choose_frame_rate(-60) == 30, "-60 is not 30");
	expect(xbox_screen_choose_flicker_filter(5) == 5, "5 is not 5");
	expect(xbox_screen_choose_flicker_filter(1) == 1, "1 is not 1");
	expect(xbox_screen_choose_flicker_filter(0) == 0, "0 is not 0");
	expect(xbox_screen_choose_flicker_filter(-3) == 0, "-3 is not 0");
	expect(xbox_screen_choose_flicker_filter(9) == 5, "9 is not 5");
}

static void expect_units(void)
{
	long x;

	/* 4:3: the interface is the pixels */
	for (x = -64; x <= 704; x++)
	{
		if (xbox_screen_interface_x(640, x) != x || xbox_screen_physical_x(640, x) != x)
		{
			expect(0, "640 is not the identity");
			break;
		}
	}
	/* 16:9: the screen, the title-safe frame (48 to 592) and the split
	screen windows' edges (320 across, 240 down) */
	expect(xbox_screen_interface_x(852, 0) == 0, "screen left");
	expect(xbox_screen_interface_x(852, 640) == 852, "screen right");
	expect(xbox_screen_interface_x(852, 320) == 426, "split middle");
	expect(xbox_screen_interface_x(852, 48) == 64, "safe left");
	expect(xbox_screen_interface_x(852, 592) == 788, "safe right");
	expect(852 - xbox_screen_interface_x(852, 592) == xbox_screen_interface_x(852, 48), "safe frame not symmetric");
	/* each window's edges come back to the same pixels */
	for (x = 0; x <= 640; x++)
	{
		if (xbox_screen_physical_x(852, xbox_screen_interface_x(852, x)) != x)
		{
			expect(0, "a pixel column does not come back");
			printf("  column %ld\n", x);
			break;
		}
	}
	/* the menus' 640 columns centered: 106 units each side, 80 pixels */
	expect(xbox_screen_physical_x(852, (852 - 640) / 2) == 80, "centered menus' left edge");
	expect(xbox_screen_physical_x(852, (852 - 640) / 2 + 640) == 560, "centered menus' right edge");
	/* and the pixels' aspect: 852 units over 640 pixels, 16:9 over 4:3 */
	expect((852.0 / 640.0) / ((16.0 / 9.0) / (4.0 / 3.0)) > 0.998 &&
		(852.0 / 640.0) / ((16.0 / 9.0) / (4.0 / 3.0)) < 1.002, "852 is not 16:9");
	/* negative columns round the same way as positive ones */
	expect(xbox_screen_interface_x(852, -48) == -64, "negative column");
}

static void expect_text(void)
{
	char text[128];

	xbox_screen_mode_text(text, sizeof(text), 852, 0x40 | XBOX_SCREEN_PRESENT_FLAG_WIDESCREEN, 60, 1);
	expect(!strcmp(text, "screen: 480p 60 Hz widescreen (component), 852 units across"), text);
	xbox_screen_mode_text(text, sizeof(text), 640, 0x20, 60, 6);
	expect(!strcmp(text, "screen: 480i 60 Hz 4:3 (composite), 640 units across"), text);
	xbox_screen_mode_text(text, sizeof(text), 852, 0x20 | XBOX_SCREEN_PRESENT_FLAG_WIDESCREEN, 50, 0);
	expect(!strcmp(text, "screen: 480i 50 Hz widescreen (SCART), 852 units across"), text);
	xbox_screen_mode_text(text, 8, 640, 0, 60, 4);
	expect(strlen(text) == 7, "a short buffer is not cut");
}

int main(void)
{
	expect_width();
	expect_units();
	expect_frames_and_filter();
	expect_text();
	if (failures)
		return 1;
	printf("screen_test: ok\n");
	return 0;
}
