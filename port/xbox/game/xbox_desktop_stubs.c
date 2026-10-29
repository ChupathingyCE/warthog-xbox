/*
XBOX_DESKTOP_STUBS.C

The desktop builds' extras (port/linux/game and port/linux/src) that the
console does not build, answered as a machine that has none of them: the
shared game code calls these unconditionally. The console's 64 MB hold none
of their code or memory, and keep the Xbox's own behavior: its own loading
screen, its own prompts (always the controller's), no team changes, no
clipboard invites, no PC menu styles. (The follow camera and the power-up
timers are built: port/linux/game.)
*/

#include "cseries.h"
#include "math/real_math.h"

#include "network_rules.h"

/* ---------- the desktop's HUD and menu extras */

void loading_screen_reset(
	void)
{
}

boolean loading_screen_step_due(
	void)
{
	return FALSE;
}

void loading_screen_draw(
	float progress,
	float brightness,
	float fade_alpha,
	unsigned int const *picture,
	long picture_width,
	long picture_height,
	unsigned int const *mask,
	long mask_width,
	long mask_height)
{
	(void)progress;
	(void)brightness;
	(void)fade_alpha;
	(void)picture;
	(void)picture_width;
	(void)picture_height;
	(void)mask;
	(void)mask_width;
	(void)mask_height;
}

int halo_keyboard_prompts(
	void)
{
	return 0;
}

int halo_keyboard_action_shown(
	int action,
	char *shown,
	unsigned long size)
{
	(void)action;
	(void)shown;
	(void)size;
	return 0;
}

void invite_prompt_render(
	void)
{
}

unsigned char pc_menu_split_players(
	void)
{
	return 0;
}

boolean pc_menu_button_key_cap(
	long bitmap_tag_index,
	short x0,
	short y0,
	short x1,
	short y1,
	float alpha)
{
	(void)bitmap_tag_index;
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
	(void)alpha;
	return FALSE;
}

void menu_style_gametype_rules(
	long profile_index,
	wchar_t *text,
	long size)
{
	(void)profile_index;
	if (size > 0)
		text[0] = 0;
}

char const *build_identity_watermark(
	void)
{
	return "";
}

/* ---------- team changes (a host's offer over Delta Peer) */

boolean team_change_death(
	long dead_player_index)
{
	(void)dead_player_index;
	return FALSE;
}

void team_change_player_died(
	long dead_player_index)
{
	(void)dead_player_index;
}

/* ---------- collision's packed bounding spheres (object_bounds_cache.c:
2048 objects of 28 bytes, which the console does without) */

void object_bounds_cache_update(
	long object_index,
	real_point3d const *center,
	real radius)
{
	(void)object_index;
	(void)center;
	(void)radius;
}

void object_bounds_cache_invalidate(
	void)
{
}

boolean object_bounds_cache_out_of_reach(
	long object_index,
	real_point3d const *point,
	real radius)
{
	(void)object_index;
	(void)point;
	(void)radius;
	return FALSE;
}

