/*
XBOX_PERF_LOG.C

The original Xbox's frame timing log (port/linux/include/halo_perf_log.h),
on with console.perf_log = true in config.toml. Times are read from the
processor's time stamp counter, whose rate each second's line measures
against the system's millisecond clock (an upgraded processor runs it
faster than 733 MHz). The line goes through the game's log (errors.c), which
keeps it in memory and writes debug.txt once a second. The tick is split
into its parts (units and players, AI, effects, the game type, objects with
the vehicles' physics, and the rest), with the collision tests made in it
and the distributed netcode's per-tick state after it. A second line
splits the render finer (its start, each view's setup, the lens flares, the
HUD's screen effect, its end, and what is left), with the waits on the
graphics chip and the disc. With console.perf_gpu_sync the game waits for the
graphics chip at each part's end, so a part's time includes the chip's.
*/

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/players.h"
#include "units/units.h"

#include "halo_perf_log.h"

int config_boolean(const char *name);

/* the Xbox Direct3D library's (d3d8.lib): returns once the graphics chip
has done everything it was given */
void __stdcall D3DDevice_BlockUntilIdle(void);

int halo_perf_log_on = FALSE;
static int perf_gpu_sync = FALSE;
unsigned long halo_perf_counters[NUMBER_OF_HALO_PERF_COUNTERS];

/* ---------- the clock */

static __inline __int64 perf_now(void)
{
	return (__int64)__builtin_ia32_rdtsc();
}

/* ---------- the second being measured */

static struct
{
	boolean started;
	unsigned long second_start_milliseconds;
	__int64 second_start;
	__int64 last_frame;
	/* (counter ticks a millisecond: the last second's, 733 MHz at first) */
	double ticks_per_millisecond;
	boolean rate_logged;

	long frames;
	__int64 frame_total;
	__int64 frame_maximum;
	long slow_frames;

	__int64 phase_start[NUMBER_OF_HALO_PERF_PHASES];
	__int64 phase_total[NUMBER_OF_HALO_PERF_PHASES];
	/* (how deep each phase is: a nested one is timed only outermost) */
	int phase_depth[NUMBER_OF_HALO_PERF_PHASES];
	int render_part;
	__int64 render_part_start;

	short zoom_level;
} perf;

void halo_perf_begin(int phase)
{
	/* (the last frame's drawing is not the render's) */
	/* (the collision tests: the game tick's only) */
	if (phase == _halo_perf_tick_collision && !perf.phase_depth[_halo_perf_tick])
		return;
	if (perf.phase_depth[phase]++)
		return;
	if (perf_gpu_sync && phase == _halo_perf_render)
		D3DDevice_BlockUntilIdle();
	perf.phase_start[phase] = perf_now();
}

void halo_perf_end(int phase)
{
	if (phase == _halo_perf_tick_collision && !perf.phase_depth[_halo_perf_tick])
		return;
	if (perf.phase_depth[phase] <= 0 || --perf.phase_depth[phase])
		return;
	perf.phase_total[phase] += perf_now() - perf.phase_start[phase];
}

void halo_perf_render_part(int phase)
{
	__int64 now;

	if (perf_gpu_sync && perf.render_part != NONE)
		D3DDevice_BlockUntilIdle();
	now = perf_now();

	if (perf.render_part != NONE)
		perf.phase_total[perf.render_part] += now - perf.render_part_start;
	perf.render_part = phase;
	perf.render_part_start = now;
}

/* the local player's zoom: a line when it changes */
static void perf_zoom_update(void)
{
	short zoom_level = NONE;

	if (game_in_progress())
	{
		long player_index = local_player_get_player_index(0);
		struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

		if (player && player->unit_index != NONE)
			zoom_level = unit_get_zoom_level(player->unit_index);
	}

	if (zoom_level != perf.zoom_level)
	{
		if (perf.zoom_level == NONE)
			error(_error_log, "perf: zoom in (level %d)", zoom_level + 1);
		else if (zoom_level == NONE)
			error(_error_log, "perf: zoom out");
		else
			error(_error_log, "perf: zoom level %d", zoom_level + 1);
		perf.zoom_level = zoom_level;
	}
}

static double perf_milliseconds(__int64 ticks, long frames)
{
	return frames > 0 ? (double)ticks / perf.ticks_per_millisecond / (double)frames : 0.0;
}

static void perf_second_log(unsigned long milliseconds, __int64 now)
{
	long frames = perf.frames;
	double frame_count = frames > 0 ? (double)frames : 1.0;
	unsigned long elapsed = milliseconds - perf.second_start_milliseconds;
	__int64 tick_other;

	if (elapsed > 0)
		perf.ticks_per_millisecond = (double)(now - perf.second_start) / (double)elapsed;

	/* (the tick less its parts: the scripts, the first-person weapons, the
	HUD, the game state's checks; the physics and the collision tests are
	inside the parts) */
	tick_other = perf.phase_total[_halo_perf_tick] - perf.phase_total[_halo_perf_tick_units] -
		perf.phase_total[_halo_perf_tick_ai] - perf.phase_total[_halo_perf_tick_effects] -
		perf.phase_total[_halo_perf_tick_game_engine] - perf.phase_total[_halo_perf_tick_objects];

	if (!perf.rate_logged)
	{
		error(_error_log, "perf: timing on, a line a second (processor about %ld MHz): times in ms a frame",
			(long)(perf.ticks_per_millisecond / 1000.0 + 0.5));
		perf.rate_logged = TRUE;
	}

	error(_error_log,
		"perf: %ld frames in %lu ms, frame %.1f avg %.1f max, %ld over 34 ms; "
		"tick %.1f (%.1f ticks: units %.1f ai %.1f effects %.1f game engine %.1f objects %.1f (physics %.1f) "
		"other %.1f; collision tests %.1f), netcode %.1f, render %.1f (world %.1f objects %.1f transparent %.1f effects %.1f hud %.1f), "
		"sound %.1f, network %.1f, present %.1f, vsync wait %.1f; "
		"%.0f draws, %.0f objects, %.0f models (%.0f high lod) a frame; zoom %d",
		frames, elapsed,
		perf_milliseconds(perf.frame_total, frames),
		perf_milliseconds(perf.frame_maximum, 1),
		perf.slow_frames,
		perf_milliseconds(perf.phase_total[_halo_perf_tick], frames),
		(double)halo_perf_counters[_halo_perf_ticks] / frame_count,
		perf_milliseconds(perf.phase_total[_halo_perf_tick_units], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_ai], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_effects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_game_engine], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_objects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_physics], frames),
		perf_milliseconds(tick_other, frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_collision], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_tick_netcode], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_world], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_objects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_transparent], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_effects], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_render_hud], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_sound], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_network], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_present], frames),
		perf_milliseconds(perf.phase_total[_halo_perf_vsync_wait], frames),
		(double)halo_perf_counters[_halo_perf_draws] / frame_count,
		(double)halo_perf_counters[_halo_perf_objects] / frame_count,
		(double)halo_perf_counters[_halo_perf_models] / frame_count,
		(double)halo_perf_counters[_halo_perf_models_high_lod] / frame_count,
		perf.zoom_level + 1);

	{
		static int const render_parts[] =
		{
			_halo_perf_render_world,
			_halo_perf_render_objects,
			_halo_perf_render_transparent,
			_halo_perf_render_effects,
			_halo_perf_render_hud,
			_halo_perf_render_begin,
			_halo_perf_render_setup,
			_halo_perf_render_lens_flares,
			_halo_perf_render_screen_effect,
			_halo_perf_render_end
		};
		__int64 other = perf.phase_total[_halo_perf_render] - perf.phase_total[_halo_perf_render_sound];
		int index;

		/* (the render less its sound and its parts: the views' cameras,
		main_game_render's own work) */
		for (index = 0; index < (int)NUMBEROF(render_parts); index++)
			other -= perf.phase_total[render_parts[index]];
		error(_error_log,
			"perf: render detail%s: begin %.1f setup %.1f lens flares %.1f screen effect %.1f (%.1f passes) "
			"end %.1f sound %.1f other %.1f; occlusion wait %.1f, texture wait %.1f (%.1f loads a frame)",
			perf_gpu_sync ? " (gpu sync)" : "",
			perf_milliseconds(perf.phase_total[_halo_perf_render_begin], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_setup], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_lens_flares], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_screen_effect], frames),
			(double)halo_perf_counters[_halo_perf_screen_effect_passes] / frame_count,
			perf_milliseconds(perf.phase_total[_halo_perf_render_end], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_render_sound], frames),
			perf_milliseconds(other, frames),
			perf_milliseconds(perf.phase_total[_halo_perf_occlusion_wait], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_texture_wait], frames),
			(double)halo_perf_counters[_halo_perf_texture_loads] / frame_count);
	}

	/* the objects' update split (objects_update), in a second that ran it */
	if (perf.phase_total[_halo_perf_tick_objects] > 0)
	{
		static int const object_parts[] =
		{
			_halo_perf_objects_bipeds,
			_halo_perf_objects_vehicles,
			_halo_perf_objects_weapons,
			_halo_perf_objects_items,
			_halo_perf_objects_projectiles,
			_halo_perf_objects_other_types,
			_halo_perf_objects_damage,
			_halo_perf_objects_nodes,
			_halo_perf_objects_functions,
			_halo_perf_objects_lights,
			_halo_perf_objects_activation,
			_halo_perf_objects_garbage
		};
		__int64 other = perf.phase_total[_halo_perf_tick_objects];
		double ticks = halo_perf_counters[_halo_perf_ticks] > 0 ? (double)halo_perf_counters[_halo_perf_ticks] : 1.0;
		int index;

		/* (the objects' update less its parts: the loops over the objects
		and the deletions) */
		for (index = 0; index < (int)NUMBEROF(object_parts); index++)
			other -= perf.phase_total[object_parts[index]];
		error(_error_log,
			"perf: objects detail: %.0f updated a tick; bipeds %.1f vehicles %.1f weapons %.1f items %.1f "
			"projectiles %.1f other types %.1f; damage %.1f nodes %.1f functions %.1f lights %.1f "
			"activation %.1f garbage %.1f other %.1f",
			(double)halo_perf_counters[_halo_perf_objects_updated] / ticks,
			perf_milliseconds(perf.phase_total[_halo_perf_objects_bipeds], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_vehicles], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_weapons], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_items], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_projectiles], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_other_types], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_damage], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_nodes], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_functions], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_lights], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_activation], frames),
			perf_milliseconds(perf.phase_total[_halo_perf_objects_garbage], frames),
			perf_milliseconds(other, frames));
	}
}

static void perf_second_start(unsigned long milliseconds, __int64 now)
{
	perf.second_start_milliseconds = milliseconds;
	perf.second_start = now;
	perf.frames = 0;
	perf.frame_total = 0;
	perf.frame_maximum = 0;
	perf.slow_frames = 0;
	csmemset(perf.phase_total, 0, sizeof(perf.phase_total));
	csmemset(halo_perf_counters, 0, sizeof(halo_perf_counters));
}

void halo_perf_frame(void)
{
	static int known = -1;
	unsigned long milliseconds;
	__int64 now;

	if (known < 0)
	{
		known = config_boolean("console.perf_log") ? 1 : 0;
		halo_perf_log_on = known;
		perf_gpu_sync = known && config_boolean("console.perf_gpu_sync");
		perf.render_part = NONE;
		perf.zoom_level = NONE;
		perf.ticks_per_millisecond = 733333.0;
	}
	if (!halo_perf_log_on)
		return;

	now = perf_now();
	milliseconds = system_milliseconds();
	if (!perf.started)
	{
		perf.started = TRUE;
		perf.last_frame = now;
		perf_second_start(milliseconds, now);
		return;
	}

	{
		__int64 frame = now - perf.last_frame;

		perf.last_frame = now;
		perf.frames++;
		perf.frame_total += frame;
		if (frame > perf.frame_maximum)
			perf.frame_maximum = frame;
		if ((double)frame > perf.ticks_per_millisecond * 34.0)
			perf.slow_frames++;
	}

	perf_zoom_update();

	if (milliseconds - perf.second_start_milliseconds >= 1000)
	{
		perf_second_log(milliseconds, now);
		perf_second_start(milliseconds, now);
	}
}
