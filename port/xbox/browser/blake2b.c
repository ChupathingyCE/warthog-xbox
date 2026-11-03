/*
 * blake2b.h: RFC 7693's reference BLAKE2b (appendix C), unkeyed
 */

#include "blake2b.h"

#include <stdio.h>

#define ROTR64(x, y) (((x) >> (y)) ^ ((x) << (64 - (y))))

#define B2B_G(a, b, c, d, x, y) \
	do \
	{ \
		v[a] = v[a] + v[b] + x; \
		v[d] = ROTR64(v[d] ^ v[a], 32); \
		v[c] = v[c] + v[d]; \
		v[b] = ROTR64(v[b] ^ v[c], 24); \
		v[a] = v[a] + v[b] + y; \
		v[d] = ROTR64(v[d] ^ v[a], 16); \
		v[c] = v[c] + v[d]; \
		v[b] = ROTR64(v[b] ^ v[c], 63); \
	} \
	while (0)

static const uint64_t blake2b_iv[8] = {
	0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL, 0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
	0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL, 0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL,
};

static const uint8_t sigma[12][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
};

static uint64_t get64(const uint8_t *p)
{
	return (uint64_t)p[0] ^ ((uint64_t)p[1] << 8) ^ ((uint64_t)p[2] << 16) ^ ((uint64_t)p[3] << 24) ^
		((uint64_t)p[4] << 32) ^ ((uint64_t)p[5] << 40) ^ ((uint64_t)p[6] << 48) ^ ((uint64_t)p[7] << 56);
}

static void compress(blake2b_ctx *ctx, int last)
{
	uint64_t v[16], m[16];
	int i;

	for (i = 0; i < 8; i++)
	{
		v[i] = ctx->h[i];
		v[i + 8] = blake2b_iv[i];
	}
	v[12] ^= ctx->t[0];
	v[13] ^= ctx->t[1];
	if (last)
		v[14] = ~v[14];
	for (i = 0; i < 16; i++)
		m[i] = get64(&ctx->b[8 * i]);
	for (i = 0; i < 12; i++)
	{
		B2B_G(0, 4, 8, 12, m[sigma[i][0]], m[sigma[i][1]]);
		B2B_G(1, 5, 9, 13, m[sigma[i][2]], m[sigma[i][3]]);
		B2B_G(2, 6, 10, 14, m[sigma[i][4]], m[sigma[i][5]]);
		B2B_G(3, 7, 11, 15, m[sigma[i][6]], m[sigma[i][7]]);
		B2B_G(0, 5, 10, 15, m[sigma[i][8]], m[sigma[i][9]]);
		B2B_G(1, 6, 11, 12, m[sigma[i][10]], m[sigma[i][11]]);
		B2B_G(2, 7, 8, 13, m[sigma[i][12]], m[sigma[i][13]]);
		B2B_G(3, 4, 9, 14, m[sigma[i][14]], m[sigma[i][15]]);
	}
	for (i = 0; i < 8; ++i)
		ctx->h[i] ^= v[i] ^ v[i + 8];
}

void blake2b_init(blake2b_ctx *ctx, size_t outlen)
{
	size_t i;

	for (i = 0; i < 8; i++)
		ctx->h[i] = blake2b_iv[i];
	/* (no key: the parameter block's digest length, fanout and depth 1) */
	ctx->h[0] ^= 0x01010000 ^ outlen;
	ctx->t[0] = 0;
	ctx->t[1] = 0;
	ctx->c = 0;
	ctx->outlen = outlen;
	for (i = 0; i < 128; i++)
		ctx->b[i] = 0;
}

void blake2b_update(blake2b_ctx *ctx, const void *in, size_t inlen)
{
	const uint8_t *p = (const uint8_t *)in;
	size_t i;

	for (i = 0; i < inlen; i++)
	{
		if (ctx->c == 128)
		{
			ctx->t[0] += ctx->c;
			if (ctx->t[0] < ctx->c)
				ctx->t[1]++;
			compress(ctx, 0);
			ctx->c = 0;
		}
		ctx->b[ctx->c++] = p[i];
	}
}

void blake2b_final(blake2b_ctx *ctx, void *out)
{
	size_t i;

	ctx->t[0] += ctx->c;
	if (ctx->t[0] < ctx->c)
		ctx->t[1]++;
	while (ctx->c < 128)
		ctx->b[ctx->c++] = 0;
	compress(ctx, 1);
	for (i = 0; i < ctx->outlen; i++)
		((uint8_t *)out)[i] = (ctx->h[i >> 3] >> (8 * (i & 7))) & 0xFF;
}

void blake2b_hex(const uint8_t *digest, size_t outlen, char *hex)
{
	size_t i;

	for (i = 0; i < outlen; i++)
		sprintf(hex + i * 2, "%02x", digest[i]);
	hex[outlen * 2] = 0;
}
