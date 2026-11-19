/*
XBOX_SCREEN.H

The console's screen (src/xbox_screen.c, src/xbox_screen_mode.c): 640x480,
and on a 16:9 television the same 640 pixels stretched to fit
(display.widescreen). The interface is then laid out in the stretched
picture's units, XBOX_SCREEN_WIDE_WIDTH of them across: the desktop builds'
width at 16:9 (port/linux/src/d3d8_gl.c, screen_mode_choose), so the HUD and
menus sit where they sit on a 16:9 desktop.
*/

#ifndef __XBOX_SCREEN_H
#define __XBOX_SCREEN_H

#define XBOX_SCREEN_WIDTH 640
#define XBOX_SCREEN_WIDE_WIDTH 852

/* the dashboard's widescreen bit (XC_VIDEO_FLAGS_WIDESCREEN, xbox.h) and
Direct3D's present flag for it (D3DPRESENTFLAG_WIDESCREEN, d3d8types.h) */
#define XBOX_SCREEN_VIDEO_FLAGS_WIDESCREEN 0x00000001UL
#define XBOX_SCREEN_PRESENT_FLAG_WIDESCREEN 0x00000010UL
/* the dashboard's PAL60 bit (XC_VIDEO_FLAGS_PAL_60Hz) and the console's TV
standards (XC_VIDEO_STANDARD_*, XGetVideoStandard) */
#define XBOX_SCREEN_VIDEO_FLAGS_PAL_60HZ 0x00000040UL
#define XBOX_SCREEN_STANDARD_NTSC_M 1UL
#define XBOX_SCREEN_STANDARD_NTSC_J 2UL
#define XBOX_SCREEN_STANDARD_PAL_I 3UL

/* the interface's width for the dashboard's video flags and
display.widescreen ("off", the default, or "dashboard"; anything else is
"dashboard") */
long xbox_screen_choose_width(unsigned long video_flags, const char *setting);
/* whether display.widescreen is "off" (in any case; "4:3", "false", "no",
"none" and "0" too) */
int xbox_screen_setting_off(const char *setting);

/* the interface's units for one of the 640 pixels' columns, and back, for a
screen width units across; each the nearest */
long xbox_screen_interface_x(long width, long physical_x);
long xbox_screen_physical_x(long width, long interface_x);

/* the frames a second for display.frame_rate: 60 when it says 60, else the
game's 30 */
short xbox_screen_choose_frame_rate(long setting);
/* the display's refresh rate: the mode's, when Direct3D says 50 or 60, else
what the console's standard and the dashboard's PAL60 make it (50 for PAL
without PAL60, 60 otherwise). The game asks for no rate, as the retail game
didn't, so Direct3D's mode table picks it */
short xbox_screen_choose_refresh_rate(unsigned long standard, unsigned long video_flags,
	unsigned long mode_refresh_rate);
/* the frames a second drawn for xbox_screen_choose_frame_rate's choice on a
display of refresh_rate: 60 is a frame every refresh (50 at 50 Hz), 30 a
frame each tick */
short xbox_screen_effective_frame_rate(short frame_rate, short refresh_rate);
/* the standard's name: "NTSC-M", "NTSC-J", "PAL-I" */
const char *xbox_screen_standard_name(unsigned long standard);
/* the flicker filter's level for display.flicker_filter, 0 to 5 */
long xbox_screen_choose_flicker_filter(long setting);

/* the mode's debug.txt line: "screen: 480p 60 Hz widescreen (component,
NTSC-M), 852 units across", with "PAL60 on" or "off" after a PAL standard */
void xbox_screen_mode_text(char *text, unsigned long size, long width, unsigned long mode_flags,
	unsigned long refresh_rate, unsigned long av_pack, unsigned long standard, unsigned long video_flags);
/* the frame rate's debug.txt line: "screen: 50 frames a second at 50 Hz
(display.frame_rate 60), flicker filter 5" */
void xbox_screen_rate_text(char *text, unsigned long size, short frame_rate, short refresh_rate,
	long flicker_filter);

#endif
