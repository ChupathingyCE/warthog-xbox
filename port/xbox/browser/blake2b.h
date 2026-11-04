/*
 * BLAKE2b (RFC 7693), for checking a custom edition map against the store's
 * list as it downloads: the list's blake2b column is BLAKE2b-256 of the whole
 * file (b2sum -l 256). the game's own Monocypher (chupathingyce
 * port/third_party/monocypher), the one its map hashes use
 */

#ifndef BLAKE2B_H
#define BLAKE2B_H

#include <stddef.h>
#include <stdint.h>

#include "port/third_party/monocypher/monocypher.h"

typedef crypto_blake2b_ctx blake2b_ctx;

#define blake2b_init(ctx, outlen) crypto_blake2b_init((ctx), (outlen))
#define blake2b_update(ctx, in, inlen) crypto_blake2b_update((ctx), (const uint8_t *)(in), (inlen))
#define blake2b_final(ctx, out) crypto_blake2b_final((ctx), (uint8_t *)(out))

/* the digest as lowercase hex, into hex (outlen * 2 + 1 bytes) */
static inline void blake2b_hex(const uint8_t *digest, size_t outlen, char *hex)
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < outlen; i++)
	{
		hex[i * 2] = digits[digest[i] >> 4];
		hex[i * 2 + 1] = digits[digest[i] & 15];
	}
	hex[outlen * 2] = 0;
}

#endif
