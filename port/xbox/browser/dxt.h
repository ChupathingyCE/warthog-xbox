/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * the store's screenshots come as dds (dxt1 / bc1), the same as its covers for
 * the xbox homebrew store client: this reads one and scales it into a small
 * picture of 0xRRGGBB pixels, for the details' picture box
 */

#ifndef DXT_H
#define DXT_H

#include <stdint.h>

/* the dds in data scaled into out (w x h): 1, or 0 if it isn't a dxt1 dds */
int dxt_thumbnail(const unsigned char *data, unsigned long size, uint32_t *out, int w, int h);

/* the same, the black bars the store padded it with cut off and the rest as big
as fits max_w x max_h, its shape kept (the store's 640x360 canvas is 16:9, most
halomaps shots are 4:3). the size it came out in *w, *h */
int dxt_picture(const unsigned char *data, unsigned long size, uint32_t *out, int max_w, int max_h, int *w, int *h);

#endif
