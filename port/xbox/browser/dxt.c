/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * dxt.h: a dds header (128 bytes: 'DDS ', height at 12, width at 16, its
 * fourcc at 84), then 8 bytes a 4x4 block: two 565 colors and 2 bits a pixel
 */

#include "dxt.h"

#include <string.h>

static uint32_t rd32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void rgb565(unsigned c, int *r, int *g, int *b)
{
	*r = ((c >> 11) & 31) * 255 / 31;
	*g = ((c >> 5) & 63) * 255 / 63;
	*b = (c & 31) * 255 / 31;
}

/* the pixel at (x, y) of a dxt1 image width wide */
static uint32_t pixel(const unsigned char *blocks, int width, int x, int y)
{
	const unsigned char *b = blocks + ((y / 4) * (width / 4) + (x / 4)) * 8;
	unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
	int index = (b[4 + (y & 3)] >> ((x & 3) * 2)) & 3;
	int r0, g0, b0, r1, g1, b1, r, g, bl;

	rgb565(c0, &r0, &g0, &b0);
	rgb565(c1, &r1, &g1, &b1);
	switch (index)
	{
	case 0: r = r0; g = g0; bl = b0; break;
	case 1: r = r1; g = g1; bl = b1; break;
	case 2:
		// two thirds between them, or halfway when c0 <= c1 (dxt1's 3 color mode)
		if (c0 > c1) { r = (2 * r0 + r1) / 3; g = (2 * g0 + g1) / 3; bl = (2 * b0 + b1) / 3; }
		else { r = (r0 + r1) / 2; g = (g0 + g1) / 2; bl = (b0 + b1) / 2; }
		break;
	default:
		if (c0 > c1) { r = (r0 + 2 * r1) / 3; g = (g0 + 2 * g1) / 3; bl = (b0 + 2 * b1) / 3; }
		else { r = g = bl = 0; }
		break;
	}
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static int header(const unsigned char *data, unsigned long size, uint32_t *width, uint32_t *height)
{
	if (size < 128 || memcmp(data, "DDS ", 4) || memcmp(data + 84, "DXT1", 4))
		return 0;
	*height = rd32(data + 12);
	*width = rd32(data + 16);
	return *width && *height && !(*width % 4) && !(*height % 4) &&
		size >= 128 + (unsigned long)(*width / 4) * (*height / 4) * 8;
}

/* the part x0..x1, y0..y1 of it (its own pixels) scaled into out (w x h) */
static void sample(const unsigned char *data, uint32_t width, int x0, int y0, int x1, int y1, uint32_t *out, int w,
	int h)
{
	int x, y;

	// nearest pixel: plenty for a 120 wide picture, and quick on a 733 mhz box
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			out[y * w + x] = pixel(data + 128, (int)width, x0 + (int)((long)x * (x1 - x0) / w), y0 + (int)((long)y * (y1 - y0) / h));
}

int dxt_thumbnail(const unsigned char *data, unsigned long size, uint32_t *out, int w, int h)
{
	uint32_t width, height;

	if (!header(data, size, &width, &height))
		return 0;
	sample(data, width, 0, 0, (int)width, (int)height, out, w, h);
	return 1;
}

static int dark(uint32_t c)
{
	return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) < 3 * 14;
}

int dxt_picture(const unsigned char *data, unsigned long size, uint32_t *out, int max_w, int max_h, int *w, int *h)
{
	// the store's canvas before it was squared for the gpu (it scaled 640x360 up to a power of 2)
	enum { CANVAS_W = 640, CANVAS_H = 360, PROBE_W = 160, PROBE_H = 90 };
	uint32_t width, height;
	int left = PROBE_W, right = -1, top = PROBE_H, bottom = -1, x, y, x0, x1, y0, y1;
	long shape_w, shape_h;

	if (!header(data, size, &width, &height))
		return 0;
	// where the picture is: a coarse look for whats not black padding, the
	// pixels sample() would pick for a PROBE_W x PROBE_H copy, read in place
	// (no 56 KB copy kept in the image for it)
	for (y = 0; y < PROBE_H; y++)
		for (x = 0; x < PROBE_W; x++)
			if (!dark(pixel(data + 128, (int)width, (int)((long)x * (int)width / PROBE_W),
				(int)((long)y * (int)height / PROBE_H))))
			{
				if (x < left) left = x;
				if (x > right) right = x;
				if (y < top) top = y;
				if (y > bottom) bottom = y;
			}
	if (right < left || bottom < top)
	{
		left = top = 0;
		right = PROBE_W - 1;
		bottom = PROBE_H - 1;
	}
	x0 = (int)((long)left * width / PROBE_W);
	x1 = (int)((long)(right + 1) * width / PROBE_W);
	y0 = (int)((long)top * height / PROBE_H);
	y1 = (int)((long)(bottom + 1) * height / PROBE_H);
	// its shape on the 640x360 canvas, then as big as fits
	shape_w = (long)(right - left + 1) * CANVAS_W / PROBE_W;
	shape_h = (long)(bottom - top + 1) * CANVAS_H / PROBE_H;
	*w = max_w;
	*h = (int)(max_w * shape_h / shape_w);
	if (*h > max_h)
	{
		*h = max_h;
		*w = (int)(max_h * shape_w / shape_h);
	}
	if (*w < 1) *w = 1;
	if (*h < 1) *h = 1;
	sample(data, width, x0, y0, x1, y1, out, *w, *h);
	return 1;
}
