/*
XBOX_SCREEN.C

What the shared code asks of the console's screen (HALO_CONSOLE_ANAMORPHIC_SCREEN,
port/linux/include/halo_console_features.h): 640x480 as the Xbox drew it, in
the mode Direct3D picks from the dashboard's video settings, and widescreen
when the dashboard has it and display.widescreen allows it.

480p needs nothing of the game's: Direct3D picks the first mode of its table
that is 640x480 with the game's widescreen bit, and its 480p rows come first
for the component cable, offered only when the dashboard allows 480p. So
component with 480p on in the dashboard is 480p, anything else 480i, as for
the retail game. Widescreen is the dashboard's bit (XGetVideoFlags), which
the game passes on as D3DPRESENTFLAG_WIDESCREEN: the table has 16:9 rows at
480p and 480i for every cable. The 640 pixels are then stretched to the
television's 16:9; the shared code draws the 3D view wider and lays the
interface out in XBOX_SCREEN_WIDE_WIDTH units (xbox_screen.h), which take
no memory: the buffers stay 640x480.
*/

#include <xtl.h>
#include <stdio.h>

#include "../include/xbox_screen.h"
#include "port/linux/src/port_config.h"

void platform_log(const char *format, ...);

/* (xbox_screen.h's copies of the SDK's bits, for the host's tests) */
typedef char xbox_screen_flags_match[
	(XC_VIDEO_FLAGS_WIDESCREEN == XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN &&
		D3DPRESENTFLAG_WIDESCREEN == XBOX_SCREEN_PRESENT_FLAG_WIDESCREEN &&
		D3DPRESENTFLAG_PROGRESSIVE == 0x40 && XC_AV_PACK_HDTV == 1 && XC_AV_PACK_STANDARD == 6) ? 1 : -1];

/* 0 until first asked; fixed from then on (the device is made once) */
static long screen_width;

long halo_screen_width(void)
{
	if (!screen_width)
		screen_width = xbox_screen_choose_width(XGetVideoFlags(), config_string("display.widescreen"));
	return screen_width;
}

long halo_screen_commit(void)
{
	return halo_screen_width();
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

/* 60 frames a second over the 30 Hz ticks (display.frame_rate), or the
game's one a tick: read once, as the device is made once (rasterizer_xbox.c,
HALO_CONSOLE_FRAME_RATE). Between the ticks only the view moves, which takes
no memory (render_interpolation.c, HALO_CONSOLE_VIEW_INTERPOLATION) */
static short frame_rate;

short halo_console_frame_rate(void)
{
	if (!frame_rate)
		frame_rate = xbox_screen_choose_frame_rate(config_integer("display.frame_rate"));
	return frame_rate;
}

int halo_interpolation_enabled(void)
{
	return halo_console_frame_rate() == 60;
}

/* the TV encoder's flicker filter (display.flicker_filter, the game's 5) */
long halo_console_flicker_filter(void)
{
	return xbox_screen_choose_flicker_filter(config_integer("display.flicker_filter"));
}

unsigned long halo_screen_present_flags(void)
{
	return halo_screen_width() > XBOX_SCREEN_WIDTH ? D3DPRESENTFLAG_WIDESCREEN : 0;
}

long halo_screen_interface_x(long physical_x)
{
	return xbox_screen_interface_x(halo_screen_width(), physical_x);
}

long halo_screen_physical_x(long interface_x)
{
	return xbox_screen_physical_x(halo_screen_width(), interface_x);
}

void halo_screen_device_report(void)
{
	D3DDISPLAYMODE mode;
	char text[96];

	ZeroMemory(&mode, sizeof(mode));
	D3DDevice_GetDisplayMode(&mode);
	xbox_screen_mode_text(text, sizeof(text), halo_screen_width(), mode.Flags, mode.RefreshRate, XGetAVPack());
	/* (and the setting as it was read, and the dashboard's: which of them
	made the picture what it is) */
	platform_log("%s; display.widescreen \"%.20s\" (%s), the dashboard %s", text,
		config_string("display.widescreen"),
		xbox_screen_setting_off(config_string("display.widescreen")) ? "off" : "follows the dashboard",
		(XGetVideoFlags() & XC_VIDEO_FLAGS_WIDESCREEN) ? "widescreen" : "4:3");
	platform_log("screen: %d frames a second (display.frame_rate), flicker filter %ld", halo_console_frame_rate(),
		halo_console_flicker_filter());
}
