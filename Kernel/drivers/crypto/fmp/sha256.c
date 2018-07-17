/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <linux/string.h>
#include "sha256.h"

int SHA256_Init(SHA256_CTX *sha)
{
	memset(sha, 0, sizeof(SHA256_CTX));
	sha->h[0] = 0x6a09e667UL;
	sha->h[1] = 0xbb67ae85UL;
	sha->h[2] = 0x3c6ef372UL;
	sha->h[3] = 0xa54ff53aUL;
	sha->h[4] = 0x510e527fUL;
	sha->h[5] = 0x9b05688cUL;
	sha->h[6] = 0x1f83d9abUL;
	sha->h[7] = 0x5be0cd19UL;
	sha->md_len = SHA256_DIGEST_LENGTH;

	return 1;
}

unsigned char *SHA256(const unsigned char *data, unsigned int len, unsigned char *out)
{
	SHA256_CTX ctx;

	if (out == NULL)
		return NULL;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, len);
	SHA256_Final(out, &ctx);
	memset(&ctx, 0x00, sizeof(SHA256_CTX));

	return out;
}

#define HASH_CTX SHA256_CTX
#define HASH_CBLOCK 64

/* Note that FIPS180-2 discusses "Truncation of the Hash Function Output."
 * default: case below covers for it. It's not clear however if it's permitted
 * to truncate to amount of bytes not divisible by 4. I bet not, but if it is,
 * then default: case shall be extended. For reference. Idea behind separate
 * cases for pre-defined lenghts is to let the compiler decide if it's
 * appropriate to unroll small loops.
 *
 * TODO(davidben): The small |md_len| case is one of the few places a low-level
 * hash 'final' function can fail. This should never happen.
 */
#define HASH_MAKE_STRING(c, s)						\
do {									\
	unsigned int ll;						\
	unsigned int nn;						\
	switch ((c)->md_len) {						\
	case SHA256_DIGEST_LENGTH:					\
		for (nn = 0; nn < SHA256_DIGEST_LENGTH / 4; nn++) {	\
			ll = (c)->h[nn];				\
			HOST_l2c(ll, (s));				\
		}							\
		break;							\
	default:							\
		if ((c)->md_len > SHA256_DIGEST_LENGTH)			\
			return 0;					\
		for (nn = 0; nn < (c)->md_len / 4; nn++) {		\
			ll = (c)->h[nn];				\
			HOST_l2c(ll, (s));				\
		}							\
		break;							\
	}								\
} while (0)

#define HASH_UPDATE SHA256_Update
#define HASH_TRANSFORM SHA256_Transform
#define HASH_FINAL SHA256_Final
#define HASH_BLOCK_DATA_ORDER sha256_block_data_order

static void sha256_block_data_order(unsigned int *state,
				const unsigned char *in, unsigned int num);

#ifndef HOST_c2l
#define HOST_c2l(c, l)					\
	(void)(l = (((unsigned int)(*((c)++))) << 24),	\
	l |= (((unsigned int)(*((c)++))) << 16),	\
	l |= (((unsigned int)(*((c)++))) << 8), l |= (((unsigned int)(*((c)++)))))
#endif

#ifndef HOST_l2c
#define HOST_l2c(l, c)						\
	(void)(*((c)++) = (unsigned char)(((l) >> 24) & 0xff),	\
	*((c)++) = (unsigned char)(((l) >> 16) & 0xff),		\
	*((c)++) = (unsigned char)(((l) >> 8) & 0xff),		\
	*((c)++) = (unsigned char)(((l)) & 0xff))
#endif

int HASH_UPDATE(HASH_CTX *c, const void *data_, unsigned int len)
{
	const unsigned char *data = data_;
	unsigned int l, n;

	if (len == 0)
		return 1;

	l = c->Nl + (((unsigned int)len) << 3);
	if (l < c->Nl) {
		/* Handle carries. */
		c->Nh++;
	}

	c->Nh += (unsigned int)(len >> 29);
	c->Nl = l;

	n = c->num;
	if (n != 0) {
		if (len >= HASH_CBLOCK || len + n >= HASH_CBLOCK) {
			memcpy(c->data + n, data, HASH_CBLOCK - n);
			HASH_BLOCK_DATA_ORDER(c->h, c->data, 1);
			n = HASH_CBLOCK - n;
			data += n;
			len -= n;
			c->num = 0;

			/* Keep |c->data| zeroed when unused. */
			memset(c->data, 0, HASH_CBLOCK);
		} else {
			memcpy(c->data + n, data, len);
			c->num += (unsigned int)len;
			return 1;
		}
	}

	n = len / HASH_CBLOCK;
	if (n > 0) {
		HASH_BLOCK_DATA_ORDER(c->h, data, n);
		n *= HASH_CBLOCK;
		data += n;
		len -= n;
	}

	if (len != 0) {
		c->num = (unsigned int)len;
		memcpy(c->data, data, len);
	}

	return 1;
}

void HASH_TRANSFORM(HASH_CTX *c, const unsigned char *data)
{
	HASH_BLOCK_DATA_ORDER(c->h, data, 1);
}

int HASH_FINAL(unsigned char *md, HASH_CTX *c)
{
	unsigned int n = c->num;
	unsigned char *p;

	/* |c->data| always has room for at least one byte. A full block would have
	 * been consumed.
	 */
	c->data[n] = 0x80;
	n++;

	/* Fill the block with zeros if there isn't room for a 64-bit length. */
	if (n > (HASH_CBLOCK - 8)) {
		memset(c->data + n, 0, HASH_CBLOCK - n);
		n = 0;
		HASH_BLOCK_DATA_ORDER(c->h, c->data, 1);
	}
	memset(c->data + n, 0, HASH_CBLOCK - 8 - n);

	/* Append a 64-bit length to the block and process it. */
	p = c->data + HASH_CBLOCK - 8;

	HOST_l2c(c->Nh, p);
	HOST_l2c(c->Nl, p);

	HASH_BLOCK_DATA_ORDER(c->h, c->data, 1);
	c->num = 0;
	memset(c->data, 0, HASH_CBLOCK);

	HASH_MAKE_STRING(c, md);

	return 1;
}

static const unsigned int K256[64] = {
	0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
	0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
	0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
	0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
	0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
	0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
	0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
	0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
	0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
	0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
	0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
	0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
	0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

#define ROTATE(a, n) (((a) << (n)) | ((a) >> (32 - (n))))

/* FIPS specification refers to right rotations, while our ROTATE macro
 * is left one. This is why you might notice that rotation coefficients
 * differ from those observed in FIPS document by 32-N...
 */
#define Sigma0(x) (ROTATE((x), 30) ^ ROTATE((x), 19) ^ ROTATE((x), 10))
#define Sigma1(x) (ROTATE((x), 26) ^ ROTATE((x), 21) ^ ROTATE((x), 7))
#define sigma0(x) (ROTATE((x), 25) ^ ROTATE((x), 14) ^ ((x) >> 3))
#define sigma1(x) (ROTATE((x), 15) ^ ROTATE((x), 13) ^ ((x) >> 10))

#define Ch(x, y, z) (((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define ROUND_00_15(i, a, b, c, d, e, f, g, h)		\
do {							\
	T1 += h + Sigma1(e) + Ch(e, f, g) + K256[i];	\
	h = Sigma0(a) + Maj(a, b, c);			\
	d += T1;					\
	h += T1;					\
} while (0)

#define ROUND_16_63(i, a, b, c, d, e, f, g, h, X)		\
do {								\
	s0 = X[(i + 1) & 0x0f];					\
	s0 = sigma0(s0);					\
	s1 = X[(i + 14) & 0x0f];				\
	s1 = sigma1(s1);					\
	T1 = X[(i) & 0x0f] += s0 + s1 + X[(i + 9) & 0x0f];	\
	ROUND_00_15(i, a, b, c, d, e, f, g, h);			\
} while (0)

static void sha256_block_data_order(unsigned int *state, const unsigned char *data,
				unsigned int num)
{
	unsigned int a, b, c, d, e, f, g, h, s0, s1, T1;
	unsigned int X[16];
	int i;
	unsigned int l;

	while (num--) {
		a = state[0];
		b = state[1];
		c = state[2];
		d = state[3];
		e = state[4];
		f = state[5];
		g = state[6];
		h = state[7];

		HOST_c2l(data, l);
		T1 = X[0] = l;
		ROUND_00_15(0, a, b, c, d, e, f, g, h);
		HOST_c2l(data, l);
		T1 = X[1] = l;
		ROUND_00_15(1, h, a, b, c, d, e, f, g);
		HOST_c2l(data, l);
		T1 = X[2] = l;
		ROUND_00_15(2, g, h, a, b, c, d, e, f);
		HOST_c2l(data, l);
		T1 = X[3] = l;
		ROUND_00_15(3, f, g, h, a, b, c, d, e);
		HOST_c2l(data, l);
		T1 = X[4] = l;
		ROUND_00_15(4, e, f, g, h, a, b, c, d);
		HOST_c2l(data, l);
		T1 = X[5] = l;
		ROUND_00_15(5, d, e, f, g, h, a, b, c);
		HOST_c2l(data, l);
		T1 = X[6] = l;
		ROUND_00_15(6, c, d, e, f, g, h, a, b);
		HOST_c2l(data, l);
		T1 = X[7] = l;
		ROUND_00_15(7, b, c, d, e, f, g, h, a);
		HOST_c2l(data, l);
		T1 = X[8] = l;
		ROUND_00_15(8, a, b, c, d, e, f, g, h);
		HOST_c2l(data, l);
		T1 = X[9] = l;
		ROUND_00_15(9, h, a, b, c, d, e, f, g);
		HOST_c2l(data, l);
		T1 = X[10] = l;
		ROUND_00_15(10, g, h, a, b, c, d, e, f);
		HOST_c2l(data, l);
		T1 = X[11] = l;
		ROUND_00_15(11, f, g, h, a, b, c, d, e);
		HOST_c2l(data, l);
		T1 = X[12] = l;
		ROUND_00_15(12, e, f, g, h, a, b, c, d);
		HOST_c2l(data, l);
		T1 = X[13] = l;
		ROUND_00_15(13, d, e, f, g, h, a, b, c);
		HOST_c2l(data, l);
		T1 = X[14] = l;
		ROUND_00_15(14, c, d, e, f, g, h, a, b);
		HOST_c2l(data, l);
		T1 = X[15] = l;
		ROUND_00_15(15, b, c, d, e, f, g, h, a);

		for (i = 16; i < 64; i += 8) {
			ROUND_16_63(i + 0, a, b, c, d, e, f, g, h, X);
			ROUND_16_63(i + 1, h, a, b, c, d, e, f, g, X);
			ROUND_16_63(i + 2, g, h, a, b, c, d, e, f, X);
			ROUND_16_63(i + 3, f, g, h, a, b, c, d, e, X);
			ROUND_16_63(i + 4, e, f, g, h, a, b, c, d, X);
			ROUND_16_63(i + 5, d, e, f, g, h, a, b, c, X);
			ROUND_16_63(i + 6, c, d, e, f, g, h, a, b, X);
			ROUND_16_63(i + 7, b, c, d, e, f, g, h, a, X);
		}

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}
}

int SHA256_CTX_copy(SHA256_CTX *dst, const SHA256_CTX *src)
{
	if (!dst || !src)
		return 0;

	memcpy(dst, src, sizeof(SHA256_CTX));
	return 1;
}
