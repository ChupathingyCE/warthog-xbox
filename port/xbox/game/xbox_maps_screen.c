/*
XBOX_MAPS_SCREEN.C

The Multiplayer menu's MAP BROWSER: the store's maps (xboxhomebrew.store) on a
screen of its own over the menus, as ONLINE GAMES' (xbox_browser_screen.c, whose
public functions call these): drawn and driven by code, in the menus' fonts.

Two lists, white and black between them: the archive's Custom Edition maps, a page
at a time, into D:\maps_ce, and the Xbox map packs, into D:\maps_<pack>. Up and down
pick, left and right turn the page, A installs, Y removes, X asks again, B stops a
download, or goes back. Under the list, the picked map: its screenshot (the store's
DXT1, decoded by port/xbox/browser/dxt.c into a texture of this screen's), who made
it, its files and the tag space they need, and its description. The work is the
browser's thread's (port/xbox/browser/xbox_maps.c): the menus go on while it runs.
*/

#ifdef HALO_XBOX_CONSOLE

#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "bitmaps/bitmap_group.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/virtual_keyboard.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include <stdio.h>
#include <string.h>

#include "../browser/xbox_maps.h"

/* (ui_widget.c's) */
long ui_widget_online_games_font(boolean heading);
/* (xbox_texture_cache.c's) */
void texture_cache_map_browser_bitmap(struct bitmap_data *bitmap, void *pixels, short size);

/* ---------- constants */

enum
{
	_maps_event_left_stick = 1,
	_maps_event_button = 3,

	_justify_left = 0,
	_justify_right,
	_justify_center,
	_maps_sound_cursor = 1,
	_maps_sound_forward = 2,
	_maps_sound_back = 3,

	OPEN_SETTLE = 600,
	/* how long a row is picked before its details are asked for */
	DETAIL_SETTLE = 250,
	STATUS_DURATION = 5000,
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,

	/* the layout of the store's map browser, and of ONLINE GAMES */
	SCREEN_LEFT = 48,
	SCREEN_RIGHT = 592,
	TITLE_TOP = 38,
	RULE_Y = 72,
	HEAD_TOP = 76,
	LIST_TOP = 96,
	ROW_HEIGHT = 20,
	DETAIL_TOP = 308,
	DETAIL_BOTTOM = 412,
	PROMPT_TOP = 420,
	CELL_INSET = 6,
	PICTURE_LEFT = SCREEN_LEFT + 8,
	PICTURE_RIGHT = PICTURE_LEFT + 120,
	DETAIL_TEXT = PICTURE_RIGHT + 14,
	DETAIL_LABEL_WIDTH = 92,

	/* the picture's texture: square, a power of two, and past its largest */
	PICTURE_TEXTURE_SIZE = 128,
	/* (PAGE_READWRITE | PAGE_WRITECOMBINE: what the GPU reads) */
	PICTURE_PROTECT = 0x404,

	DESCRIPTION_LINES = 2,
	DESCRIPTION_LINE_LENGTH = 80,
};

#define COLOR_BACKGROUND 0xFF050B18
#define COLOR_RULE 0xFF2A62C8
#define COLOR_PANEL 0xFF07122A
#define COLOR_HEADER 0xFF0C1E40
#define COLOR_ROW_SELECTED 0xFF2052B0
#define COLOR_PICTURE 0xFF0A1A33

/* the buttons' colors */
#define COLOR_BUTTON_A 0xFF3CB043
#define COLOR_BUTTON_Y 0xFFF2C230
#define COLOR_BUTTON_X 0xFF2D6FD8
#define COLOR_BUTTON_WHITE 0xFFD0D0D0
#define COLOR_BUTTON_B 0xFFD83A3A

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	short tab;
	/* the Custom Edition list: the page's first entry, and the row picked on it */
	int ce_offset;
	short ce_row;
	/* the page's rows' states, as their details have said: the fids they're of */
	long row_fid[XBOX_MAPS_ROWS];
	signed char row_state[XBOX_MAPS_ROWS];
	/* the packs: the one picked */
	short pack_row;
	boolean packs_loaded;
	/* a row picked since, whose details wait */
	long detail_wanted;
	unsigned long picked_time;
	/* what comes when the thread is done */
	boolean want_page;
	boolean want_packs;
	boolean refresh_detail;
	unsigned long step_time, step_last;
	short step_move;
	/* what the thread last said, while it's shown */
	int status_serial;
	unsigned long status_time;
	/* the picture: its texture, the entry in it, and the part of it that's picture */
	struct bitmap_data picture_bitmap;
	void *picture_pixels;
	int picture_serial;
	long picture_fid;
	short picture_width, picture_height;
	/* the search (X, the game's keyboard): what's typed, the words the list is
	of ("": the whole archive), and whether the keyboard is up for it */
	wchar_t search_text[32];
	char query[32];
	boolean searching;
	/* the description, broken into lines for the font, for one entry */
	long wrapped_fid;
	char wrapped[DESCRIPTION_LINES][DESCRIPTION_LINE_LENGTH];
} maps_screen;

/* ---------- private code */

static ce_entry const *picked_entry(
	void)
{
	int count, total;
	ce_entry const *rows = xbox_maps_rows(&count, &total);

	return maps_screen.ce_row >= 0 && maps_screen.ce_row < count ? &rows[maps_screen.ce_row] : NULL;
}

static map_entry const *picked_pack(
	void)
{
	int count;
	map_entry const *packs = xbox_maps_packs(&count);

	return maps_screen.pack_row >= 0 && maps_screen.pack_row < count ? &packs[maps_screen.pack_row] : NULL;
}

static ce_detail const *picked_detail(
	void)
{
	ce_detail const *detail = xbox_maps_detail();
	ce_entry const *entry = picked_entry();

	return detail && entry && detail->fid == entry->fid ? detail : NULL;
}

static void ask_for_details(
	void)
{
	ce_entry const *entry = picked_entry();

	maps_screen.detail_wanted = entry ? entry->fid : 0;
	maps_screen.picked_time = system_milliseconds();
}

/* the Xbox's order for a square texture: the x and y bits in turn */
static unsigned long picture_swizzle(
	unsigned long x,
	unsigned long y)
{
	unsigned long result = 0;
	unsigned long bit;

	for (bit = 0; (1UL << bit) < PICTURE_TEXTURE_SIZE; bit++)
	{
		result |= ((x >> bit) & 1) << (2 * bit);
		result |= ((y >> bit) & 1) << (2 * bit + 1);
	}
	return result;
}

/* a picture the thread has fetched: into the texture */
static void take_picture(
	void)
{
	unsigned int const *pixels;
	int width, height, serial;
	long fid = xbox_maps_picture(&pixels, &width, &height, &serial);

	if (serial == maps_screen.picture_serial || !pixels || fid <= 0)
		return;
	maps_screen.picture_serial = serial;
	if (!maps_screen.picture_pixels)
	{
		maps_screen.picture_pixels = XPhysicalAlloc(PICTURE_TEXTURE_SIZE * PICTURE_TEXTURE_SIZE * 4, -1, 0, PICTURE_PROTECT);
		if (!maps_screen.picture_pixels)
		{
			error(_error_silent, "map browser: no memory for the picture's texture");
			return;
		}
		texture_cache_map_browser_bitmap(&maps_screen.picture_bitmap, maps_screen.picture_pixels, PICTURE_TEXTURE_SIZE);
	}
	if (width > PICTURE_TEXTURE_SIZE || height > PICTURE_TEXTURE_SIZE)
	{
		error(_error_silent, "map browser: a picture of %dx%d is past the texture", width, height);
		return;
	}
	{
		unsigned long *to = (unsigned long *)maps_screen.picture_pixels;
		int x, y;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
				to[picture_swizzle(x, y)] = 0xFF000000UL | pixels[y * XBOX_MAPS_PICTURE_WIDTH + x];
		}
	}
	maps_screen.picture_fid = fid;
	maps_screen.picture_width = (short)width;
	maps_screen.picture_height = (short)height;
}

/* what the details just said, kept for the row's HERE */
static void take_detail(
	void)
{
	ce_detail const *detail = picked_detail();

	if (detail && maps_screen.ce_row >= 0 && maps_screen.ce_row < XBOX_MAPS_ROWS)
	{
		maps_screen.row_fid[maps_screen.ce_row] = detail->fid;
		maps_screen.row_state[maps_screen.ce_row] = (signed char)detail->state;
	}
}

/* what waits for the thread, once it's free: run from the main loop */
static void run_wanted(
	void)
{
	int status_serial = 0;

	xbox_maps_status(NULL, &status_serial);
	if (status_serial != maps_screen.status_serial)
	{
		maps_screen.status_serial = status_serial;
		maps_screen.status_time = system_milliseconds();
	}
	if (xbox_maps_busy())
		return;
	take_picture();
	take_detail();
	if (maps_screen.want_page)
	{
		xbox_maps_set_query(maps_screen.query);
		if (xbox_maps_load_page(maps_screen.ce_offset))
		{
			maps_screen.want_page = FALSE;
			csmemset(maps_screen.row_fid, 0, sizeof(maps_screen.row_fid));
		}
		return;
	}
	if (maps_screen.want_packs)
	{
		if (xbox_maps_load_packs())
		{
			maps_screen.want_packs = FALSE;
			maps_screen.packs_loaded = TRUE;
		}
		return;
	}
	if (maps_screen.tab != XBOX_MAPS_TAB_CE)
		return;
	if (maps_screen.refresh_detail)
	{
		maps_screen.refresh_detail = FALSE;
		ask_for_details();
	}
	/* (a new page's picked row, once the page has come) */
	if (!maps_screen.detail_wanted && picked_entry() && !picked_detail())
		ask_for_details();
	if (maps_screen.detail_wanted && system_milliseconds() - maps_screen.picked_time > DETAIL_SETTLE)
	{
		if (xbox_maps_load_detail(maps_screen.detail_wanted))
			maps_screen.detail_wanted = 0;
		return;
	}
	/* its picture, after its details (they name it), once: a failed one isn't asked again */
	if (!maps_screen.detail_wanted && picked_detail())
	{
		unsigned int const *pixels;
		int width, height, serial;
		long fid = xbox_maps_picture(&pixels, &width, &height, &serial);
		long held = picked_detail()->fid;

		if (fid != held && fid != -held)
			xbox_maps_load_picture();
	}
}

static short held_move(
	short move)
{
	unsigned long now = system_milliseconds();

	if (!move)
		return 0;
	if (move != maps_screen.step_move || now - maps_screen.step_last > STEP_RELEASE_GAP)
	{
		maps_screen.step_move = move;
		maps_screen.step_last = now;
		maps_screen.step_time = now + STEP_FIRST_REPEAT_DELAY;
		return move;
	}
	maps_screen.step_last = now;
	if (now >= maps_screen.step_time)
	{
		maps_screen.step_time = now + STEP_REPEAT_INTERVAL;
		return move;
	}
	return 0;
}

static void to_page(
	int offset,
	short row)
{
	maps_screen.ce_offset = offset;
	maps_screen.ce_row = row;
	maps_screen.want_page = TRUE;
	maps_screen.detail_wanted = 0;
	ui_play_audio_feedback_sound(_maps_sound_cursor);
}

static void move_ce(
	short rows,
	short pages)
{
	int count, total;

	xbox_maps_rows(&count, &total);
	if (xbox_maps_busy() || maps_screen.want_page)
	{
		/* (a page on its way: a row of it waits for it) */
		if (!pages && !maps_screen.want_page && maps_screen.ce_row + rows >= 0 && maps_screen.ce_row + rows < count)
		{
			maps_screen.ce_row += rows;
			ui_play_audio_feedback_sound(_maps_sound_cursor);
			ask_for_details();
		}
		return;
	}
	if (pages)
	{
		int offset = maps_screen.ce_offset + pages * XBOX_MAPS_ROWS;

		if (offset >= 0 && offset < total)
			to_page(offset, 0);
		return;
	}
	if (maps_screen.ce_row + rows < 0)
	{
		if (maps_screen.ce_offset >= XBOX_MAPS_ROWS)
			to_page(maps_screen.ce_offset - XBOX_MAPS_ROWS, XBOX_MAPS_ROWS - 1);
		return;
	}
	if (maps_screen.ce_row + rows >= count)
	{
		if (maps_screen.ce_offset + count < total)
			to_page(maps_screen.ce_offset + XBOX_MAPS_ROWS, 0);
		return;
	}
	maps_screen.ce_row += rows;
	ui_play_audio_feedback_sound(_maps_sound_cursor);
	ask_for_details();
}

static void move_pack(
	short rows)
{
	int count;
	short row;

	xbox_maps_packs(&count);
	row = (short)PIN(maps_screen.pack_row + rows, 0, MAX(0, count - 1));
	if (row != maps_screen.pack_row)
	{
		maps_screen.pack_row = row;
		ui_play_audio_feedback_sound(_maps_sound_cursor);
	}
}

/* the search's words from what the keyboard took (the store's are ASCII) */
static void take_search(
	void)
{
	char query[sizeof(maps_screen.query)];
	short in, out = 0;

	for (in = 0; maps_screen.search_text[in] && out < (short)sizeof(query) - 1; in++)
	{
		wchar_t c = maps_screen.search_text[in];

		if (c >= 0x20 && c < 0x7f)
			query[out++] = (char)c;
	}
	/* (the spaces before and after dropped) */
	while (out && query[out - 1] == ' ')
		out--;
	query[out] = 0;
	for (in = 0; query[in] == ' '; in++)
		;
	if (!strcmp(maps_screen.query, query + in))
		return;
	strncpy(maps_screen.query, query + in, sizeof(maps_screen.query) - 1);
	maps_screen.query[sizeof(maps_screen.query) - 1] = 0;
	to_page(0, 0);
}

static void switch_tab(
	short tab)
{
	if (tab == maps_screen.tab)
		return;
	maps_screen.tab = tab;
	ui_play_audio_feedback_sound(_maps_sound_cursor);
	if (tab == XBOX_MAPS_TAB_PACKS && !maps_screen.packs_loaded)
		maps_screen.want_packs = TRUE;
}

/* ---------- public code */

boolean maps_screen_active(
	void)
{
	return maps_screen.active;
}

/* the MAP BROWSER item's description in the menu (source/interface/ui_widget.c) */
wchar_t const *maps_screen_description(
	void)
{
	return L"Download community maps \r\nand map packs, from the \r\nxboxhomebrew.store archive.";
}

void maps_screen_open(
	void)
{
	if (!xbox_maps_open())
	{
		error(_error_silent, "map browser: no memory to open");
		return;
	}
	maps_screen.active = TRUE;
	maps_screen.opened_time = system_milliseconds();
	maps_screen.want_page = TRUE;
	maps_screen.packs_loaded = FALSE;
	maps_screen.want_packs = FALSE;
	maps_screen.detail_wanted = 0;
	maps_screen.refresh_detail = FALSE;
	maps_screen.picture_fid = 0;
	maps_screen.wrapped_fid = 0;
	maps_screen.tab = XBOX_MAPS_TAB_CE;
	maps_screen.searching = FALSE;
	event_manager_flush();
}

void maps_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	/* (the keyboard put away: its words searched for, if it was done) */
	if (maps_screen.searching && !virtual_keyboard_active())
	{
		maps_screen.searching = FALSE;
		if (virtual_keyboard_last_exit_saved_text())
			take_search();
		event_manager_flush();
	}
	while (maps_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == _maps_event_left_stick)
		{
			if (event.data.stick.y == SHORT_MAX)
				move = -1;
			else if (event.data.stick.y == SHORT_MIN)
				move = 1;
			else if (event.data.stick.x == SHORT_MIN)
				move = -XBOX_MAPS_ROWS;
			else if (event.data.stick.x == SHORT_MAX)
				move = XBOX_MAPS_ROWS;
		}
		else if (event.type == _maps_event_button)
		{
			boolean settled = system_milliseconds() - maps_screen.opened_time > OPEN_SETTLE;

			switch (event.data.button.index)
			{
			case _gamepad_binary_button_dpad_up: move = -1; break;
			case _gamepad_binary_button_dpad_down: move = 1; break;
			case _gamepad_binary_button_dpad_left: move = -XBOX_MAPS_ROWS; break;
			case _gamepad_binary_button_dpad_right: move = XBOX_MAPS_ROWS; break;
			case _gamepad_analog_button_white:
			case _gamepad_analog_button_black:
				switch_tab(maps_screen.tab == XBOX_MAPS_TAB_CE ? XBOX_MAPS_TAB_PACKS : XBOX_MAPS_TAB_CE);
				break;
			case _gamepad_analog_button_a:
				if (settled && !xbox_maps_busy())
				{
					boolean started = FALSE;

					if (maps_screen.tab == XBOX_MAPS_TAB_CE)
					{
						if (picked_detail())
						{
							started = xbox_maps_install_detail();
							maps_screen.refresh_detail = started;
						}
					}
					else if (picked_pack())
						started = xbox_maps_install_pack(maps_screen.pack_row);
					ui_play_audio_feedback_sound(started ? _maps_sound_forward : _maps_sound_back);
				}
				break;
			case _gamepad_analog_button_y:
				if (settled && !xbox_maps_busy())
				{
					boolean started = FALSE;

					if (maps_screen.tab == XBOX_MAPS_TAB_CE)
					{
						if (picked_detail() && picked_detail()->state != MAP_MISSING)
						{
							started = xbox_maps_remove_detail();
							maps_screen.refresh_detail = started;
						}
					}
					else if (picked_pack() && picked_pack()->state != MAP_MISSING)
						started = xbox_maps_remove_pack(maps_screen.pack_row);
					ui_play_audio_feedback_sound(started ? _maps_sound_forward : _maps_sound_back);
				}
				break;
			case _gamepad_analog_button_x:
				/* (on the Custom Edition list, a search: the game's keyboard,
				with the words searched for last) */
				if (maps_screen.tab == XBOX_MAPS_TAB_CE)
				{
					if (settled)
					{
						short index;

						for (index = 0; maps_screen.query[index] && index < 31; index++)
							maps_screen.search_text[index] = (wchar_t)(unsigned char)maps_screen.query[index];
						maps_screen.search_text[index] = 0;
						maps_screen.searching = virtual_keyboard_launch_plain(maps_screen.search_text,
							sizeof(maps_screen.search_text), L"SEARCH MAPS");
						if (!maps_screen.searching)
							ui_play_audio_feedback_sound(_maps_sound_back);
						else
							return;
					}
				}
				else
				{
					ui_play_audio_feedback_sound(_maps_sound_forward);
					maps_screen.want_packs = TRUE;
				}
				break;
			case _gamepad_analog_button_b:
				ui_play_audio_feedback_sound(_maps_sound_back);
				{
					char name[8];
					unsigned long done, total;

					/* (B stops a download; with none, goes back) */
					if (xbox_maps_progress(name, sizeof(name), &done, &total))
						xbox_maps_stop();
					/* (and a search, the whole archive again) */
					else if (maps_screen.tab == XBOX_MAPS_TAB_CE && maps_screen.query[0])
					{
						maps_screen.query[0] = 0;
						to_page(0, 0);
					}
					else
					{
						maps_screen.active = FALSE;
						xbox_maps_close();
					}
				}
				break;
			default: break;
			}
		}
		move = held_move(move);
		if (move)
		{
			if (maps_screen.tab == XBOX_MAPS_TAB_CE)
			{
				if (move == XBOX_MAPS_ROWS || move == -XBOX_MAPS_ROWS)
					move_ce(0, move > 0 ? 1 : -1);
				else
					move_ce(move, 0);
			}
			else
				move_pack(move);
			move = 0;
		}
	}
	if (maps_screen.active)
		run_wanted();
	/* (the widgets behind take nothing while the screen is up) */
	event_manager_flush();
}

/* ---------- drawing */

static void set_color(
	real_argb_color *color,
	real alpha,
	real red,
	real green,
	real blue)
{
	color->alpha = alpha;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

static void quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d bounds;

	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, color);
}

static void text(
	long font,
	short justification,
	real_argb_color const *color,
	short x0,
	short y0,
	short x1,
	short y1,
	char const *string)
{
	wchar_t wide[128];
	rectangle2d bounds;
	short index;

	if (font == NONE || !string)
		return;
	for (index = 0; index < NUMBEROF(wide) - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_string_set_draw_mode(font, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, wide);
}

static short line_height(
	long font)
{
	struct font_header *header = font != NONE ? font_definition_get(font) : NULL;

	return header ? (short)(header->ascending_height + header->descending_height) : 16;
}

static short text_width(
	long font,
	char const *string)
{
	wchar_t wide[128];
	rectangle2d bounds, text_bounds, cursor_bounds;
	real_argb_color color;
	short index;

	if (font == NONE || !string)
		return 0;
	for (index = 0; index < NUMBEROF(wide) - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 640;
	bounds.y1 = 480;
	set_color(&color, 1.0f, 1.0f, 1.0f, 1.0f);
	draw_string_set_draw_mode(font, NONE, _justify_left, 0, &color);
	draw_unicode_string_compute_bounds(&bounds, wide, &text_bounds, &cursor_bounds);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

static void size_text(
	unsigned long bytes,
	char *out,
	int size)
{
	if (bytes >= 1048576UL)
		snprintf(out, size, "%lu.%lu MB", bytes / 1048576UL, (bytes % 1048576UL) * 10 / 1048576UL);
	else
		snprintf(out, size, "%lu KB", bytes / 1024UL);
}

/* the description in lines that fit the panel, the last saying when there's more.
Measured once for each entry: the font's bounds aren't for every frame */
static void wrap_description(
	long font,
	ce_detail const *detail,
	short width)
{
	char const *words = detail->description;
	short line;

	maps_screen.wrapped_fid = detail->fid;
	for (line = 0; line < DESCRIPTION_LINES; line++)
	{
		char *row = maps_screen.wrapped[line];
		char test[DESCRIPTION_LINE_LENGTH];
		short length = 0, fits = 0;

		row[0] = 0;
		while (*words == ' ' || *words == '\r' || *words == '\n' || *words == '\t')
			words++;
		while (length < DESCRIPTION_LINE_LENGTH - 4)
		{
			char next = words[length];
			boolean ends = !next || next == '\r' || next == '\n';

			if (ends || next == ' ' || next == '\t')
			{
				csmemcpy(test, words, length);
				test[length] = 0;
				if (text_width(font, test) > width)
					break;
				fits = length;
				if (ends)
					break;
			}
			length++;
		}
		/* (one long word) */
		if (!fits)
			fits = MIN(length, 30);
		csmemcpy(row, words, fits);
		row[fits] = 0;
		words += fits;
		/* (the last line says when there's more) */
		if (line == DESCRIPTION_LINES - 1 && *words && fits >= 3)
			csmemcpy(row + fits - 3, "...", 3);
	}
}

/* a round button as big as the menu font's letters need, its letter in the middle, and
its word after it, on a line: the x after them */
static short draw_button(
	long font,
	short x,
	short y,
	pixel32 color,
	char const *letter,
	char const *word,
	real_argb_color const *dark_color,
	real_argb_color const *text_color)
{
	short height = line_height(font);
	/* (the widest letter, W, with room around it) */
	short diameter = (short)PIN(MAX(height + 4, text_width(font, "W") + 8), 16, 30);
	short text_top = (short)(y + (diameter - height) / 2);
	short row;

	for (row = 0; row < diameter; row++)
	{
		/* (the circle's half width at this row: the largest w with (2w)^2 + dy^2 <= d^2, in half pixels) */
		long dy = 2 * row + 1 - diameter;
		long half = 0;

		while ((2 * (half + 1)) * (2 * (half + 1)) + dy * dy <= (long)diameter * diameter)
			half++;
		quad((short)(x + diameter / 2 - half), (short)(y + row), (short)(x + (diameter + 1) / 2 + half), (short)(y + row + 1), color);
	}
	text(font, _justify_center, dark_color, x, text_top, (short)(x + diameter), (short)(text_top + height + 2), letter);
	text(font, _justify_left, text_color, (short)(x + diameter + 6), text_top, SCREEN_RIGHT, (short)(text_top + height + 2), word);
	return (short)(x + diameter + 6 + text_width(font, word) + 16);
}

static void draw_picture_box(
	long font,
	long heading_font,
	char const *big,
	char const *small,
	boolean picture,
	real_argb_color const *title_color,
	real_argb_color const *dim_color)
{
	short y0 = DETAIL_TOP + 6, y1 = DETAIL_BOTTOM - 6;

	quad(PICTURE_LEFT, y0, PICTURE_RIGHT, y1, COLOR_PICTURE);
	if (picture && maps_screen.picture_pixels)
	{
		rectangle2d bounds, art;
		short x = (short)(PICTURE_LEFT + (PICTURE_RIGHT - PICTURE_LEFT - maps_screen.picture_width) / 2);
		short y = (short)(y0 + (y1 - y0 - maps_screen.picture_height) / 2);

		bounds.x0 = x;
		bounds.y0 = y;
		bounds.x1 = (short)(x + maps_screen.picture_width);
		bounds.y1 = (short)(y + maps_screen.picture_height);
		art.x0 = 0;
		art.y0 = 0;
		art.x1 = maps_screen.picture_width;
		art.y1 = maps_screen.picture_height;
		draw_bitmap_in_rect(&maps_screen.picture_bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
		return;
	}
	{
		char one[2];

		one[0] = big[0] ? big[0] : '?';
		one[1] = 0;
		text(heading_font != NONE ? heading_font : font, _justify_center, title_color, PICTURE_LEFT, DETAIL_TOP + 26,
			PICTURE_RIGHT, DETAIL_TOP + 60, one);
		text(font, _justify_center, dim_color, PICTURE_LEFT, DETAIL_TOP + 62, PICTURE_RIGHT, DETAIL_TOP + 84, small);
	}
}

/* a label and its value, on a line of the details */
static void detail_line(
	long font,
	short *y,
	char const *label,
	char const *value,
	real_argb_color const *label_color,
	real_argb_color const *value_color)
{
	short height = (short)(line_height(font) + 1);

	text(font, _justify_left, label_color, DETAIL_TEXT, *y, value[0] ? DETAIL_TEXT + DETAIL_LABEL_WIDTH : SCREEN_RIGHT - 8,
		(short)(*y + height), label);
	text(font, _justify_left, value_color, DETAIL_TEXT + DETAIL_LABEL_WIDTH, *y, SCREEN_RIGHT - 8, (short)(*y + height), value);
	*y = (short)(*y + height);
}

static void draw_tabs(
	long font,
	real_argb_color const *text_color,
	real_argb_color const *dim_color)
{
	static char const *const names[XBOX_MAPS_TAB_COUNT] = { "CUSTOM EDITION", "XBOX PACKS" };
	short x = SCREEN_RIGHT;
	short tab;

	for (tab = XBOX_MAPS_TAB_COUNT - 1; tab >= 0; tab--)
	{
		short width = (short)(text_width(font, names[tab]) + 16);

		x = (short)(x - width);
		quad(x, RULE_Y - 22, (short)(x + width), RULE_Y, tab == maps_screen.tab ? COLOR_ROW_SELECTED : COLOR_HEADER);
		text(font, _justify_left, tab == maps_screen.tab ? text_color : dim_color, (short)(x + 8), RULE_Y - 21, (short)(x + width),
			RULE_Y, names[tab]);
		x = (short)(x - 4);
	}
}

static void draw_list_heads(
	long font,
	char const *const *heads,
	short const *columns,
	real_argb_color const *head_color)
{
	short ty = (short)(HEAD_TOP + (LIST_TOP - HEAD_TOP - line_height(font)) / 2);
	short index;

	quad(SCREEN_LEFT, HEAD_TOP, SCREEN_RIGHT, LIST_TOP, COLOR_HEADER);
	for (index = 0; index < 4; index++)
	{
		text(font, _justify_left, head_color, columns[index], ty, index < 3 ? (short)(columns[index + 1] - CELL_INSET) : SCREEN_RIGHT,
			LIST_TOP, heads[index]);
	}
	quad(SCREEN_LEFT, LIST_TOP, SCREEN_RIGHT, LIST_TOP + XBOX_MAPS_ROWS * ROW_HEIGHT, COLOR_PANEL);
}

static void draw_under_list(
	long font,
	short page,
	short pages,
	real_argb_color const *good_color,
	real_argb_color const *note_color,
	real_argb_color const *head_color)
{
	short y = LIST_TOP + XBOX_MAPS_ROWS * ROW_HEIGHT + 3;
	short y1 = (short)(y + line_height(font) + 2);
	int failed = 0;
	char const *status = xbox_maps_status(&failed, NULL);
	char line[40];

	if (status[0] && system_milliseconds() - maps_screen.status_time < STATUS_DURATION)
	{
		text(font, _justify_left, failed ? note_color : good_color, SCREEN_LEFT + CELL_INSET, y, SCREEN_RIGHT - 130, y1, status);
	}
	else if (maps_screen.tab == XBOX_MAPS_TAB_CE && maps_screen.query[0])
	{
		int count, total;
		char searched[80];

		xbox_maps_rows(&count, &total);
		if (maps_screen.want_page || xbox_maps_busy())
			snprintf(searched, sizeof(searched), "Searching for \"%s\"...", maps_screen.query);
		else
			snprintf(searched, sizeof(searched), "\"%s\": %d found", maps_screen.query, total);
		text(font, _justify_left, good_color, SCREEN_LEFT + CELL_INSET, y, SCREEN_RIGHT - 130, y1, searched);
	}
	snprintf(line, sizeof(line), "PAGE %d OF %d", page + 1, pages < 1 ? 1 : pages);
	text(font, _justify_right, head_color, SCREEN_LEFT, y, SCREEN_RIGHT - CELL_INSET, y1, line);
}

static char const *state_word(
	int state)
{
	return state == MAP_INSTALLED ? "OK" : state == MAP_OUTDATED ? "!!" : "--";
}

static real_argb_color const *state_color(
	int state,
	real_argb_color const *good_color,
	real_argb_color const *note_color,
	real_argb_color const *dim_color)
{
	return state == MAP_INSTALLED ? good_color : state == MAP_OUTDATED ? note_color : dim_color;
}

static void draw_ce(
	long font,
	long heading_font,
	real_argb_color const *title_color,
	real_argb_color const *head_color,
	real_argb_color const *text_color,
	real_argb_color const *dim_color,
	real_argb_color const *note_color,
	real_argb_color const *good_color)
{
	static char const *const heads[] = { "MAP", "AUTHOR", "DATE", "HERE" };
	static short const columns[] = { SCREEN_LEFT + CELL_INSET, 268, 410, 540 };
	short text_top = (short)((ROW_HEIGHT - line_height(font)) / 2);
	int count, total;
	ce_entry const *rows = xbox_maps_rows(&count, &total);
	ce_entry const *entry = picked_entry();
	ce_detail const *detail = picked_detail();
	short row;
	short y;
	char line[128], value[48];

	draw_list_heads(font, heads, columns, head_color);
	if (!count)
	{
		text(font, _justify_center, dim_color, SCREEN_LEFT, LIST_TOP + ROW_HEIGHT * 2, SCREEN_RIGHT, LIST_TOP + ROW_HEIGHT * 3,
			maps_screen.query[0] && !maps_screen.want_page && !xbox_maps_busy() ?
				"No maps found. X searches again, B shows them all." : "Getting the archive...");
	}
	for (row = 0; !maps_screen.want_page && row < count && row < XBOX_MAPS_ROWS; row++)
	{
		short y0 = (short)(LIST_TOP + row * ROW_HEIGHT);
		short y1 = (short)(y0 + ROW_HEIGHT);
		short ty = (short)(y0 + text_top);

		if (row == maps_screen.ce_row)
			quad(SCREEN_LEFT, y0, SCREEN_RIGHT, y1, COLOR_ROW_SELECTED);
		/* (a map that won't fit a 64 MB console's tag space: its name in orange) */
		text(font, _justify_left, rows[row].fits64 == 0 ? note_color : text_color, columns[0], ty,
			(short)(columns[1] - CELL_INSET), y1, rows[row].title);
		text(font, _justify_left, dim_color, columns[1], ty, (short)(columns[2] - CELL_INSET), y1, rows[row].author);
		text(font, _justify_left, text_color, columns[2], ty, (short)(columns[3] - CELL_INSET), y1, rows[row].date);
		/* (its state once its details are in) */
		if (maps_screen.row_fid[row] == rows[row].fid)
		{
			text(font, _justify_left, state_color(maps_screen.row_state[row], good_color, note_color, dim_color), columns[3], ty,
				SCREEN_RIGHT, y1, state_word(maps_screen.row_state[row]));
		}
	}
	draw_under_list(font, (short)(maps_screen.ce_offset / XBOX_MAPS_ROWS), (short)((total + XBOX_MAPS_ROWS - 1) / XBOX_MAPS_ROWS),
		good_color, note_color, head_color);

	quad(SCREEN_LEFT, DETAIL_TOP, SCREEN_RIGHT, DETAIL_BOTTOM, COLOR_PANEL);
	if (!entry || maps_screen.want_page)
		return;
	{
		unsigned int const *pixels;
		int width, height, serial;
		/* (negative: its picture was asked for, and there's none) */
		long tried = xbox_maps_picture(&pixels, &width, &height, &serial);
		char const *word = !console_memory_extras() ? "CUSTOM ED." : detail && tried == -entry->fid ? "NO PICTURE" : "LOADING";

		draw_picture_box(font, heading_font, entry->title, word, maps_screen.picture_fid == entry->fid, title_color, dim_color);
	}
	y = DETAIL_TOP + 3;
	text(font, _justify_left, text_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, (short)(y + line_height(font) + 1), entry->title);
	y = (short)(y + line_height(font) + 2);
	snprintf(line, sizeof(line), "%s, %s", entry->author, entry->date);
	detail_line(font, &y, "By", line, head_color, text_color);
	if (!detail)
	{
		detail_line(font, &y, "Maps", "...", head_color, text_color);
		return;
	}
	{
		unsigned long size = 0;
		short index, ready = 0;

		for (index = 0; index < detail->map_count; index++)
		{
			if (detail->maps[index].ready)
			{
				ready++;
				size += detail->maps[index].size;
			}
		}
		size_text(size, value, sizeof(value));
		snprintf(line, sizeof(line), "%s, %d of %d on the mirror", value, ready, detail->map_count);
		detail_line(font, &y, "Maps", line, head_color, text_color);
		/* (the xbox's tag space: what its first map needs of 22 MB) */
		if (detail->map_count && detail->maps[0].fits64 >= 0)
		{
			unsigned long needs = detail->maps[0].needs;

			snprintf(line, sizeof(line), "%lu.%lu MB of 22", needs / 1048576UL, (needs % 1048576UL) * 10 / 1048576UL);
			if (detail->maps[0].fits64)
				detail_line(font, &y, "Tag space", line, head_color, good_color);
			else
				detail_line(font, &y, "WON'T FIT A 64 MB CONSOLE", "", note_color, note_color);
		}
		else
			detail_line(font, &y, "File", detail->map_count ? detail->maps[0].file : "-", head_color, text_color);
		if (maps_screen.wrapped_fid != detail->fid)
			wrap_description(font, detail, (short)(SCREEN_RIGHT - 8 - DETAIL_TEXT));
		for (index = 0; index < DESCRIPTION_LINES && maps_screen.wrapped[index][0] && y + line_height(font) <= DETAIL_BOTTOM; index++)
		{
			text(font, _justify_left, dim_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, (short)(y + line_height(font) + 1),
				maps_screen.wrapped[index]);
			y = (short)(y + line_height(font));
		}
	}
}

static void draw_packs(
	long font,
	long heading_font,
	real_argb_color const *title_color,
	real_argb_color const *head_color,
	real_argb_color const *text_color,
	real_argb_color const *dim_color,
	real_argb_color const *note_color,
	real_argb_color const *good_color)
{
	static char const *const heads[] = { "MAP", "FILE", "SIZE", "HERE" };
	static short const columns[] = { SCREEN_LEFT + CELL_INSET, 280, 440, 548 };
	short text_top = (short)((ROW_HEIGHT - line_height(font)) / 2);
	int count;
	map_entry const *packs = xbox_maps_packs(&count);
	map_entry const *pack = picked_pack();
	short first = (short)(maps_screen.pack_row - maps_screen.pack_row % XBOX_MAPS_ROWS);
	short row, y;
	char line[64], value[96];

	draw_list_heads(font, heads, columns, head_color);
	if (!count)
	{
		text(font, _justify_center, dim_color, SCREEN_LEFT, LIST_TOP + ROW_HEIGHT * 2, SCREEN_RIGHT, LIST_TOP + ROW_HEIGHT * 3,
			maps_screen.packs_loaded && !xbox_maps_busy() ? "No packs in the store yet." : "Getting the list...");
	}
	for (row = 0; !maps_screen.want_packs && row < XBOX_MAPS_ROWS && first + row < count; row++)
	{
		map_entry const *entry = &packs[first + row];
		short y0 = (short)(LIST_TOP + row * ROW_HEIGHT);
		short y1 = (short)(y0 + ROW_HEIGHT);
		short ty = (short)(y0 + text_top);

		if (first + row == maps_screen.pack_row)
			quad(SCREEN_LEFT, y0, SCREEN_RIGHT, y1, COLOR_ROW_SELECTED);
		size_text(entry->size, value, sizeof(value));
		text(font, _justify_left, text_color, columns[0], ty, (short)(columns[1] - CELL_INSET), y1, entry->name[0] ? entry->name : entry->file);
		text(font, _justify_left, dim_color, columns[1], ty, (short)(columns[2] - CELL_INSET), y1, entry->file);
		text(font, _justify_left, text_color, columns[2], ty, (short)(columns[3] - CELL_INSET), y1, value);
		text(font, _justify_left, state_color(entry->state, good_color, note_color, dim_color), columns[3], ty, SCREEN_RIGHT, y1,
			state_word(entry->state));
	}
	draw_under_list(font, (short)(maps_screen.pack_row / XBOX_MAPS_ROWS), (short)((count + XBOX_MAPS_ROWS - 1) / XBOX_MAPS_ROWS),
		good_color, note_color, head_color);

	quad(SCREEN_LEFT, DETAIL_TOP, SCREEN_RIGHT, DETAIL_BOTTOM, COLOR_PANEL);
	if (!pack || maps_screen.want_packs)
		return;
	draw_picture_box(font, heading_font, pack->pack, "XBOX PACK", FALSE, title_color, dim_color);
	y = DETAIL_TOP + 3;
	text(font, _justify_left, text_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, (short)(y + line_height(font) + 1),
		pack->name[0] ? pack->name : "(not in the menus)");
	y = (short)(y + line_height(font) + 2);
	snprintf(value, sizeof(value), "maps_%s\\%s.map", pack->pack, pack->file);
	detail_line(font, &y, "Folder", value, head_color, text_color);
	size_text(pack->size, line, sizeof(line));
	snprintf(value, sizeof(value), "%s, version %s", line, pack->version);
	detail_line(font, &y, "Size", value, head_color, text_color);
	snprintf(value, sizeof(value), "%08lx", pack->fingerprint);
	detail_line(font, &y, "Print", value, head_color, text_color);
	detail_line(font, &y, "Here", pack->state == MAP_INSTALLED ? "installed" : pack->state == MAP_OUTDATED ?
		"another version (A replaces it)" : "not installed", head_color, text_color);
}

/* a download's progress, over the screen */
static void draw_busy(
	long font,
	real_argb_color const *title_color,
	real_argb_color const *text_color,
	real_argb_color const *dim_color)
{
	enum { X0 = 120, X1 = 520, Y0 = 180, Y1 = 300, BAR0 = X0 + 20, BAR1 = X1 - 20 };
	char name[64], line[96], done_text[24], total_text[24];
	unsigned long done, total;

	if (!xbox_maps_progress(name, sizeof(name), &done, &total))
		return;
	quad(X0 - 2, Y0 - 2, X1 + 2, Y1 + 2, COLOR_RULE);
	quad(X0, Y0, X1, Y1, COLOR_HEADER);
	text(font, _justify_left, title_color, X0 + 20, Y0 + 12, X1 - 20, Y0 + 34, "DOWNLOADING   (B stops it)");
	text(font, _justify_left, text_color, X0 + 20, Y0 + 36, X1 - 20, Y0 + 58, name);
	quad(BAR0, Y0 + 64, BAR1, Y0 + 80, COLOR_PANEL);
	size_text(done, done_text, sizeof(done_text));
	size_text(total, total_text, sizeof(total_text));
	if (total)
	{
		/* (in KB: a map of hundreds of MB doesn't overflow) */
		unsigned long width = (unsigned long)(BAR1 - BAR0);
		unsigned long total_kb = MAX(total >> 10, 1UL);
		unsigned long filled = (done >> 10) * width / total_kb;

		quad(BAR0, Y0 + 64, (short)(BAR0 + MIN(filled, width)), Y0 + 80, COLOR_ROW_SELECTED);
		snprintf(line, sizeof(line), "%s of %s  (%lu%%)", done_text, total_text, MIN((done >> 10) * 100 / total_kb, 100UL));
	}
	else
		snprintf(line, sizeof(line), "%s", done_text);
	text(font, _justify_left, dim_color, X0 + 20, Y0 + 86, X1 - 20, Y0 + 108, line);
}

void maps_screen_render(
	void)
{
	long heading_font = ui_widget_online_games_font(TRUE);
	long font = ui_widget_online_games_font(FALSE);
	real_argb_color title_color, head_color, text_color, dim_color, note_color, good_color, dark_color;
	short x = SCREEN_LEFT + CELL_INSET, y = PROMPT_TOP + 2;

	/* (the search's keyboard drawn instead, by the menus) */
	if (virtual_keyboard_active())
		return;
	set_color(&title_color, 1.0f, 0.24f, 0.55f, 1.0f);
	set_color(&head_color, 1.0f, 0.5f, 0.69f, 1.0f);
	set_color(&text_color, 1.0f, 0.9f, 0.93f, 0.99f);
	set_color(&dim_color, 1.0f, 0.56f, 0.65f, 0.78f);
	set_color(&note_color, 1.0f, 0.94f, 0.54f, 0.29f);
	set_color(&good_color, 1.0f, 0.37f, 0.82f, 0.41f);
	set_color(&dark_color, 1.0f, 0.06f, 0.06f, 0.06f);

	quad(0, 0, 640, 480, COLOR_BACKGROUND);
	text(heading_font != NONE ? heading_font : font, _justify_left, &title_color, SCREEN_LEFT, TITLE_TOP, SCREEN_RIGHT,
		RULE_Y - 2, "MAP BROWSER");
	draw_tabs(font, &text_color, &dim_color);
	quad(SCREEN_LEFT, RULE_Y, SCREEN_RIGHT, RULE_Y + 1, COLOR_RULE);

	if (maps_screen.tab == XBOX_MAPS_TAB_CE)
		draw_ce(font, heading_font, &title_color, &head_color, &text_color, &dim_color, &note_color, &good_color);
	else
		draw_packs(font, heading_font, &title_color, &head_color, &text_color, &dim_color, &note_color, &good_color);

	x = draw_button(font, x, y, COLOR_BUTTON_A, "A", "Install", &dark_color, &text_color);
	x = draw_button(font, x, y, COLOR_BUTTON_Y, "Y", "Remove", &dark_color, &text_color);
	x = draw_button(font, x, y, COLOR_BUTTON_X, "X", maps_screen.tab == XBOX_MAPS_TAB_CE ? "Search" : "Refresh",
		&dark_color, &text_color);
	x = draw_button(font, x, y, COLOR_BUTTON_WHITE, "W", "Tab", &dark_color, &text_color);
	x = draw_button(font, x, y, COLOR_BUTTON_B, "B",
		maps_screen.tab == XBOX_MAPS_TAB_CE && maps_screen.query[0] ? "All maps" : "Back", &dark_color, &text_color);

	draw_busy(font, &title_color, &text_color, &dim_color);
}

#endif
