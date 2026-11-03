/*
 * BLAKE2b (RFC 7693), for checking a custom edition map against opence's
 * signed list as it downloads: the list's blake2b column is BLAKE2b-256 of
 * the whole file (b2sum -l 256)
 */

#ifndef BLAKE2B_H
#define BLAKE2B_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
	uint8_t b[128];
	uint64_t h[8];
	uint64_t t[2];
	size_t c;
	size_t outlen;
} blake2b_ctx;

void blake2b_init(blake2b_ctx *ctx, size_t outlen);
void blake2b_update(blake2b_ctx *ctx, const void *in, size_t inlen);
void blake2b_final(blake2b_ctx *ctx, void *out);
/* the digest as lowercase hex, into hex (outlen * 2 + 1 bytes) */
void blake2b_hex(const uint8_t *digest, size_t outlen, char *hex);

#endif
