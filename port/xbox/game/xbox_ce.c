/*
XBOX_CE.C

What Custom Edition maps ask of the desktop builds' units the console leaves out,
and their bitmaps made the Xbox's.
*/

/* the PC menus' tags (port/linux/game/menu_tags.c): the console has its own menus */
unsigned char pc_menu_owns(
	unsigned long address,
	long size)
{
	(void)address;
	(void)size;
	return 0;
}

/* ---------- a Custom Edition map's bitmaps */

/* The desktop builds' renderer is told a bitmap's pixels are Halo PC's
(D3DCOMMON_PORT_PC_LAYOUT, ..._PC_METER, ..._PC_MULTIPURPOSE: xbox_texture_cache.c)
and draws them so. The NV2A knows none of that, so a bitmap is made the Xbox's in
place, once its pixels are read (xbox_texture_cache.c), each time they are:
uncompressed texels in Morton order, a cube map's faces each with their levels (Halo
PC's: each level with its faces, the second and third faces swapped), a meter's and a
multipurpose map's channels where the Xbox's shaders read them. A compressed bitmap's
channels stay Halo PC's: its blocks aren't decoded here. */

#include <stdlib.h>
#include <string.h>
#include "cseries/cseries.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "cseries/errors.h"

/* (xbox_texture_cache.c's, the decomp's local to each unit) */
enum
{
	_bitmap_type_2d,
	_bitmap_type_3d,
	_bitmap_type_cube_map,
	NUMBER_OF_BITMAP_TYPES,
};
enum
{
	_bitmap_has_power_of_two_dimensions_bit,
	_bitmap_compressed_bit,
	_bitmap_palettized_bit,
	_bitmap_swizzled_bit,
	_bitmap_linear_bit,
	_bitmap_v16u16_bit,
	_bitmap_allocated_bit,
	_bitmap_cached_bit,
	NUMBER_OF_BITMAP_FLAGS,
};

extern boolean ce_hud_bitmap_is_meter(void const *bitmap);
extern boolean ce_models_bitmap_is_multipurpose(void const *bitmap);

#define CE_FACES 6

static unsigned long ce_morton(
	unsigned long x,
	unsigned long y,
	unsigned long z,
	unsigned long width,
	unsigned long height,
	unsigned long depth)
{
	unsigned long result = 0;
	unsigned long bit = 1;

	while (width > 1 || height > 1 || depth > 1)
	{
		if (width > 1)
		{
			if (x & 1)
				result |= bit;
			bit <<= 1;
			x >>= 1;
			width >>= 1;
		}
		if (height > 1)
		{
			if (y & 1)
				result |= bit;
			bit <<= 1;
			y >>= 1;
			height >>= 1;
		}
		if (depth > 1)
		{
			if (z & 1)
				result |= bit;
			bit <<= 1;
			z >>= 1;
			depth >>= 1;
		}
	}
	return result;
}

static void ce_swizzle_level(
	byte *destination,
	byte const *source,
	long width,
	long height,
	long depth,
	long pixel_bytes)
{
	long x, y, z;

	for (z = 0; z < depth; z++)
	{
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				byte *to = destination + ce_morton(x, y, z, width, height, depth) * pixel_bytes;

				switch (pixel_bytes)
				{
				case 4:
					*(unsigned long *)to = *(unsigned long const *)source;
					break;
				case 2:
					*(unsigned short *)to = *(unsigned short const *)source;
					break;
				default:
					*to = *source;
					break;
				}
				source += pixel_bytes;
			}
		}
	}
}

/* the same in place, with no memory of its own (a 64 MB console in a game has no
128 KB to spare for a copy of a HUD meter, nor any for a level's lightmaps, which
were then drawn in Halo PC's order: speckled, and the level's light in blocks).
The Xbox's order is the linear index's bits put in another order (ce_morton), so
it's made by swapping two of the index's bits at a time, each a pass of swapped
pixels, at most one per bit. 0 if it couldn't (a size that isn't a power of two) */
static boolean ce_power_of_two(
	long value)
{
	return value > 0 && !(value & (value - 1));
}

static short ce_log2(
	unsigned long value)
{
	short bits = 0;

	while (value > 1)
	{
		value >>= 1;
		bits++;
	}
	return bits;
}

static void ce_swap_index_bits(
	byte *pixels,
	unsigned long count,
	short low,
	short high,
	long pixel_bytes)
{
	unsigned long mask = (1UL << low) | (1UL << high);
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		unsigned long other;

		/* (each pair once: the one with the low bit set and the high one clear) */
		if (!(index & (1UL << low)) || (index & (1UL << high)))
			continue;
		other = index ^ mask;
		switch (pixel_bytes)
		{
		case 4:
		{
			unsigned long held = ((unsigned long *)pixels)[index];

			((unsigned long *)pixels)[index] = ((unsigned long *)pixels)[other];
			((unsigned long *)pixels)[other] = held;
			break;
		}
		case 2:
		{
			unsigned short held = ((unsigned short *)pixels)[index];

			((unsigned short *)pixels)[index] = ((unsigned short *)pixels)[other];
			((unsigned short *)pixels)[other] = held;
			break;
		}
		default:
		{
			byte held = pixels[index];

			pixels[index] = pixels[other];
			pixels[other] = held;
			break;
		}
		}
	}
}

static boolean ce_swizzle_level_in_place(
	byte *pixels,
	long width,
	long height,
	long depth,
	long pixel_bytes)
{
	/* where each of the linear index's bits goes (to[bit]), and which of them is at
	each place now (at[place]) */
	short to[32], at[32], bits, bit, place;
	short x_bits, y_bits, z_bits;
	unsigned long count;

	if (!ce_power_of_two(width) || !ce_power_of_two(height) || !ce_power_of_two(depth))
		return FALSE;
	x_bits = ce_log2((unsigned long)width);
	y_bits = ce_log2((unsigned long)height);
	z_bits = ce_log2((unsigned long)depth);
	bits = (short)(x_bits + y_bits + z_bits);
	count = 1UL << bits;
	/* (ce_morton's: x, y and z's bits in turn, while each has any) */
	{
		short x = 0, y = 0, z = 0, next = 0;

		while (x < x_bits || y < y_bits || z < z_bits)
		{
			if (x < x_bits)
				to[x++] = next++;
			if (y < y_bits)
				to[x_bits + y++] = next++;
			if (z < z_bits)
				to[x_bits + y_bits + z++] = next++;
		}
	}
	for (place = 0; place < bits; place++)
		at[place] = place;
	for (bit = 0; bit < bits; bit++)
	{
		short now = 0;

		for (place = 0; place < bits; place++)
		{
			if (at[place] == bit)
				now = place;
		}
		if (now != to[bit])
		{
			short low = now < to[bit] ? now : to[bit];
			short high = now < to[bit] ? to[bit] : now;
			short displaced = at[to[bit]];

			ce_swap_index_bits(pixels, count, low, high, pixel_bytes);
			at[to[bit]] = bit;
			at[now] = displaced;
		}
	}
	return TRUE;
}

/* a cube map's levels and faces in place, with no copy: Halo PC's each level with
its six faces (the second and third the Xbox's third and second), the Xbox's each
face with its levels, a face_stride apart. Each block brought to its place in
turn by rotating it there (three reversals), then the faces spread to their
stride, each level's pixels laid out (swizzle, uncompressed). Slow (a pass for
each of its blocks), and only for a console that has no memory for the copy */
enum
{
	CE_MAXIMUM_CUBE_LEVELS = 16,
};

static void ce_reverse(
	byte *start,
	byte *end)
{
	while (start < --end)
	{
		byte held = *start;

		*start++ = *end;
		*end = held;
	}
}

static void ce_rotate(
	byte *start,
	byte *middle,
	byte *end)
{
	if (start == middle || middle == end)
		return;
	ce_reverse(start, middle);
	ce_reverse(middle, end);
	ce_reverse(start, end);
}

static boolean ce_cube_in_place(
	byte *pixels,
	short levels,
	long const *level_bytes,
	long const *widths,
	long const *heights,
	long face_stride,
	long pixel_bytes)
{
	/* the blocks in the order they're in now: each its face (the Xbox's) and level */
	short face_of[CE_FACES * CE_MAXIMUM_CUBE_LEVELS], level_of[CE_FACES * CE_MAXIMUM_CUBE_LEVELS];
	short count = 0, placed = 0, face, level, index;
	long cursor = 0, face_bytes = 0;

	if (levels < 1 || levels > CE_MAXIMUM_CUBE_LEVELS)
		return FALSE;
	for (level = 0; level < levels; level++)
	{
		short pc_face;

		face_bytes += level_bytes[level];
		for (pc_face = 0; pc_face < CE_FACES; pc_face++)
		{
			face_of[count] = pc_face == 1 ? 2 : pc_face == 2 ? 1 : pc_face;
			level_of[count] = level;
			count++;
		}
	}
	if (face_stride < face_bytes)
		return FALSE;
	for (face = 0; face < CE_FACES; face++)
	{
		for (level = 0; level < levels; level++)
		{
			long offset = cursor;

			/* (where it is among those not placed yet) */
			for (index = placed; index < count && (face_of[index] != face || level_of[index] != level); index++)
				offset += level_bytes[level_of[index]];
			if (index == count)
				return FALSE;
			ce_rotate(pixels + cursor, pixels + offset, pixels + offset + level_bytes[level]);
			for (; index > placed; index--)
			{
				face_of[index] = face_of[index - 1];
				level_of[index] = level_of[index - 1];
			}
			face_of[placed] = face;
			level_of[placed] = level;
			placed++;
			cursor += level_bytes[level];
		}
	}
	/* (each face at its stride: the last first, as each moves up) */
	for (face = CE_FACES - 1; face > 0 && face_stride > face_bytes; face--)
		memmove(pixels + face * face_stride, pixels + face * face_bytes, face_bytes);
	if (pixel_bytes == 1 || pixel_bytes == 2 || pixel_bytes == 4)
	{
		for (face = 0; face < CE_FACES; face++)
		{
			long offset = face * face_stride;

			for (level = 0; level < levels; level++)
			{
				if (!ce_swizzle_level_in_place(pixels + offset, widths[level], heights[level], 1, pixel_bytes))
					return FALSE;
				offset += level_bytes[level];
			}
		}
	}
	return TRUE;
}

/* a level of one face (or of the whole bitmap) as the Xbox has it */
static void ce_put_level(
	byte *destination,
	byte const *source,
	struct bitmap_data *bitmap,
	short level,
	long bytes)
{
	long pixel_bytes = bitmap_format_get_bits_per_pixel(bitmap->format) / 8;

	if (!TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) &&
		(pixel_bytes == 1 || pixel_bytes == 2 || pixel_bytes == 4))
	{
		ce_swizzle_level(destination, source, bitmap_mipmap_get_width(bitmap, level),
			bitmap_mipmap_get_height(bitmap, level),
			bitmap->type == _bitmap_type_3d ? bitmap_mipmap_get_depth(bitmap, level) : 1, pixel_bytes);
	}
	else
	{
		memcpy(destination, source, bytes);
	}
}

static void ce_bitmap_channels(
	struct bitmap_data *bitmap,
	long size)
{
	boolean meter = ce_hud_bitmap_is_meter(bitmap);
	boolean multipurpose = ce_models_bitmap_is_multipurpose(bitmap);
	unsigned long *pixel = (unsigned long *)xbox_pointer(bitmap->base_address);
	long count = size / 4;
	long index;

	if ((!meter && !multipurpose) || TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) ||
		bitmap_format_get_bits_per_pixel(bitmap->format) != 32)
	{
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned long value = pixel[index];
		unsigned long a = value >> 24, r = (value >> 16) & 255, g = (value >> 8) & 255, b = value & 255;

		/* (a meter: its fill order in the color, its shape in alpha. a
		multipurpose map: specular, self-illumination, color change, auxiliary) */
		pixel[index] = meter ? (r << 24) | (a << 16) | (a << 8) | a : (r << 24) | (b << 16) | (g << 8) | a;
	}
}

/* a HUD meter's bitmap that ce_bitmap_channels left in Halo PC's channels (a
compressed one, as Custom Edition's own shield and health meters are, or one
not of 32 bits or linear, which aren't laid out): the meter shader reads them
where they are (rasterizer_xbox_dynavobgeom.c) */
boolean platform_ce_meter_in_pc_channels(
	struct bitmap_data const *bitmap)
{
	return bitmap && ce_hud_bitmap_is_meter(bitmap) &&
		(TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) || TEST_FLAG(bitmap->flags, _bitmap_linear_bit) ||
		bitmap_format_get_bits_per_pixel(bitmap->format) != 32);
}

/* (the large ones said, as each is laid out: what's drawn wrong on a console short
of memory is then in the log) */
char const *tag_get_name(long tag_index);

static void ce_bitmap_said(
	struct bitmap_data const *bitmap,
	long bytes,
	char const *how)
{
	static long said;

	if (bytes < 128 * 1024 || ++said > 64)
		return;
	error(_error_silent, "Custom Edition bitmap %s: %dx%dx%d, format %d, type %d, %ld bytes: %s",
		bitmap->tag_index != NONE ? tag_get_name(bitmap->tag_index) : "(none)", bitmap->width, bitmap->height,
		bitmap->depth, bitmap->format, bitmap->type, bytes, how);
}

void platform_ce_bitmap_loaded(
	struct bitmap_data *bitmap)
{
	static boolean reported = FALSE;
	long size = rasterizer_xbox_bitmap_get_pixel_data_size(bitmap);
	long source_size = bitmap->pixels_size > size ? bitmap->pixels_size : size;
	short levels = rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap);
	byte *pixels = (byte *)xbox_pointer(bitmap->base_address);
	byte *source;
	short level;

	if (TEST_FLAG(bitmap->flags, _bitmap_linear_bit))
	{
		ce_bitmap_said(bitmap, source_size, "linear, left");
		return;
	}
	/* (nothing moves in a compressed bitmap that isn't a cube map) */
	if (TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) && bitmap->type != _bitmap_type_cube_map)
		return;
	source = (byte *)malloc(source_size);
	/* (no memory for the copy: a bitmap that isn't a cube map each level in place,
	slower, reading the texture cache's uncached memory a pass a bit) */
	if (!source && bitmap->type != _bitmap_type_cube_map)
	{
		long pixel_bytes = bitmap_format_get_bits_per_pixel(bitmap->format) / 8;
		long offset = 0;
		boolean done = pixel_bytes == 1 || pixel_bytes == 2 || pixel_bytes == 4;

		for (level = 0; done && level <= levels; level++)
		{
			long level_bytes = bitmap_mipmap_get_pixel_data_size(bitmap, level);

			done = ce_swizzle_level_in_place(pixels + offset, bitmap_mipmap_get_width(bitmap, level),
				bitmap_mipmap_get_height(bitmap, level),
				bitmap->type == _bitmap_type_3d ? bitmap_mipmap_get_depth(bitmap, level) : 1, pixel_bytes);
			offset += level_bytes;
		}
		/* (the first level couldn't be: the copy, as before; a later one can't
		fail with the first done) */
		if (done || level > 1)
		{
			static long in_place;

			/* (said for the first few: the log says it happened) */
			if (++in_place <= 3)
				error(_error_silent, "Custom Edition maps: a bitmap of %ld bytes laid out in place (no memory for a copy)",
					source_size);
			ce_bitmap_said(bitmap, source_size, done ? "in place" : "in place, a level of it not");
			ce_bitmap_channels(bitmap, size);
			__asm__ volatile("sfence");
			return;
		}
	}
	/* (no memory for the copy of a cube map: its blocks moved in place) */
	if (!source && bitmap->type == _bitmap_type_cube_map && levels + 1 <= CE_MAXIMUM_CUBE_LEVELS)
	{
		long level_bytes[CE_MAXIMUM_CUBE_LEVELS], widths[CE_MAXIMUM_CUBE_LEVELS], heights[CE_MAXIMUM_CUBE_LEVELS];
		boolean compressed = TEST_FLAG(bitmap->flags, _bitmap_compressed_bit);

		for (level = 0; level <= levels; level++)
		{
			level_bytes[level] = bitmap_mipmap_get_pixel_data_size(bitmap, level) / CE_FACES;
			widths[level] = bitmap_mipmap_get_width(bitmap, level);
			heights[level] = bitmap_mipmap_get_height(bitmap, level);
		}
		if (ce_cube_in_place(pixels, (short)(levels + 1), level_bytes, widths, heights, size / CE_FACES,
			compressed ? 0 : bitmap_format_get_bits_per_pixel(bitmap->format) / 8))
		{
			ce_bitmap_said(bitmap, source_size, "cube map, in place");
			ce_bitmap_channels(bitmap, size);
			__asm__ volatile("sfence");
			return;
		}
	}
	if (!source)
	{
		ce_bitmap_said(bitmap, source_size, "NOT laid out (no memory)");
		if (!reported)
			error(_error_silent, "Custom Edition maps: no memory to lay out a bitmap of %ld bytes", source_size);
		reported = TRUE;
		return;
	}
	memcpy(source, pixels, bitmap->pixels_size);
	if (bitmap->type == _bitmap_type_cube_map)
	{
		long face_stride = size / CE_FACES;
		long face_offset = 0;
		long pc_offset = 0;

		for (level = 0; level <= levels; level++)
		{
			long level_bytes = bitmap_mipmap_get_pixel_data_size(bitmap, level) / CE_FACES;
			long face;

			for (face = 0; face < CE_FACES; face++)
			{
				/* (Halo PC's second and third faces are the Xbox's third and second) */
				long pc_face = face == 1 ? 2 : face == 2 ? 1 : face;

				ce_put_level(pixels + face * face_stride + face_offset, source + pc_offset + pc_face * level_bytes, bitmap,
					level, level_bytes);
			}
			face_offset += level_bytes;
			pc_offset += level_bytes * CE_FACES;
		}
	}
	else
	{
		long offset = 0;

		for (level = 0; level <= levels; level++)
		{
			long level_bytes = bitmap_mipmap_get_pixel_data_size(bitmap, level);

			ce_put_level(pixels + offset, source + offset, bitmap, level, level_bytes);
			offset += level_bytes;
		}
	}
	free(source);
	ce_bitmap_said(bitmap, source_size, "copied");
	ce_bitmap_channels(bitmap, size);
	__asm__ volatile("sfence");
}
