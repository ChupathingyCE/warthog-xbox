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

void xbox_ce_bitmap_loaded(
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
		return;
	/* (nothing moves in a compressed bitmap that isn't a cube map) */
	if (TEST_FLAG(bitmap->flags, _bitmap_compressed_bit) && bitmap->type != _bitmap_type_cube_map)
		return;
	source = (byte *)malloc(source_size);
	if (!source)
	{
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
	ce_bitmap_channels(bitmap, size);
	__asm__ volatile("sfence");
}
