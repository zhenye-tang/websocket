/*
 *	RFC 1521 bignum encoding/decoding
 *
 *  Based on TropicSSL: Copyright (C) 2017 Shanghai Real-Thread Technology Co., Ltd
 *
 *	Based on XySSL: Copyright (C) 2006-2008	 Christophe Devine
 *
 *	Copyright (C) 2009	Paul Bakker <polarssl_maintainer at polarssl dot org>
 *
 *	All rights reserved.
 *
 *	Redistribution and use in source and binary forms, with or without
 *	modification, are permitted provided that the following conditions
 *	are met:
 *
 *	  * Redistributions of source code must retain the above copyright
 *		notice, this list of conditions and the following disclaimer.
 *	  * Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
 *	  * Neither the names of PolarSSL or XySSL nor the names of its contributors
 *		may be used to endorse or promote products derived from this software
 *		without specific prior written permission.
 *
 *	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *	FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *	OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *	TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tinycrypt_config.h"

#if defined(TINY_CRYPT_BIGNUM)

#include "tinycrypt.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define TINY_MPI_CHK(f) do { if( ( ret = f ) != 0 ) goto cleanup; } while( 0 )

/* Implementation that should never be optimized out by the compiler */
static void tiny_mpi_zeroize(tiny_mpi_uint *v, int n)
{
    volatile tiny_mpi_uint *p = v;
    while (n--) *p++ = 0;
}

#define ciL    (sizeof(tiny_mpi_uint))          /* chars in limb  */
#define biL    ((int)(ciL << 3))                /* bits  in limb  */
#define biH    ((int)(ciL << 2))                /* half limb size */

#define MPI_SIZE_T_MAX  ( (int) -1 )            /* SIZE_T_MAX is not standard */

#define TINY_MPI_WINDOW_SIZE (6)                /* Maximum windows size used. */

/*
 * Convert between bits/chars and number of limbs
 * Divide first in order to avoid potential overflows
 */
#define BITS_TO_LIMBS(i)  ( (i) / biL + ( (i) % biL != 0 ) )
#define CHARS_TO_LIMBS(i) ( (i) / ciL + ( (i) % ciL != 0 ) )

#define MULADDC_INIT                    \
{                                       \
    tiny_mpi_uint s0, s1, b0, b1;              \
    tiny_mpi_uint r0, r1, rx, ry;              \
    b0 = ( b << biH ) >> biH;           \
    b1 = ( b >> biH );

#define MULADDC_CORE                    \
    s0 = ( *s << biH ) >> biH;          \
    s1 = ( *s >> biH ); s++;            \
    rx = s0 * b1; r0 = s0 * b0;         \
    ry = s1 * b0; r1 = s1 * b1;         \
    r1 += ( rx >> biH );                \
    r1 += ( ry >> biH );                \
    rx <<= biH; ry <<= biH;             \
    r0 += rx; r1 += (r0 < rx);          \
    r0 += ry; r1 += (r0 < ry);          \
    r0 +=  c; r1 += (r0 <  c);          \
    r0 += *d; r1 += (r0 < *d);          \
    c = r1; *(d++) = r0;

#define MULADDC_STOP                    \
}

/*
 * Initialize one MPI
 */
void tiny_mpi_init(tiny_mpi *X)
{
    if (X == NULL)
        return;

    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

/*
 * Unallocate one MPI
 */
void tiny_mpi_free(tiny_mpi *X)
{
    if (X == NULL)
        return;

    if (X->p != NULL)
    {
        tiny_mpi_zeroize(X->p, X->n);
        tiny_free(X->p);
    }

    X->s = 1;
    X->n = 0;
    X->p = NULL;
}

/*
 * Enlarge to the specified number of limbs
 */
int tiny_mpi_grow(tiny_mpi *X, int nblimbs)
{
    tiny_mpi_uint *p;

    if (X->n < nblimbs)
    {
        if ((p = (tiny_mpi_uint *)tiny_calloc(nblimbs, ciL)) == NULL)
            return (-1);

        if (X->p != NULL)
        {
            memcpy(p, X->p, X->n * ciL);
            tiny_mpi_zeroize(X->p, X->n);
            tiny_free(X->p);
        }

        X->n = nblimbs;
        X->p = p;
    }

    return (0);
}

/*
 * Resize down as much as possible,
 * while keeping at least the specified number of limbs
 */
int tiny_mpi_shrink(tiny_mpi *X, int nblimbs)
{
    tiny_mpi_uint *p;
    int i;

    /* Actually resize up in this case */
    if (X->n <= nblimbs)
        return (tiny_mpi_grow(X, nblimbs));

    for (i = X->n - 1; i > 0; i--)
        if (X->p[i] != 0)
            break;
    i++;

    if (i < nblimbs)
        i = nblimbs;

    if ((p = (tiny_mpi_uint *)tiny_calloc(i, ciL)) == NULL)
        return (-1);

    if (X->p != NULL)
    {
        memcpy(p, X->p, i * ciL);
        tiny_mpi_zeroize(X->p, X->n);
        tiny_free(X->p);
    }

    X->n = i;
    X->p = p;

    return (0);
}

/*
 * Copy the contents of Y into X
 */
int tiny_mpi_copy(tiny_mpi *X, const tiny_mpi *Y)
{
    int ret;
    int i;

    if (X == Y)
        return (0);

    if (Y->p == NULL)
    {
        tiny_mpi_free(X);
        return (0);
    }

    for (i = Y->n - 1; i > 0; i--)
        if (Y->p[i] != 0)
            break;
    i++;

    X->s = Y->s;

    TINY_MPI_CHK(tiny_mpi_grow(X, i));

    memset(X->p, 0, X->n * ciL);
    memcpy(X->p, Y->p, i * ciL);

cleanup:

    return (ret);
}

/*
 * Swap the contents of X and Y
 */
void tiny_mpi_swap(tiny_mpi *X, tiny_mpi *Y)
{
    tiny_mpi T;

    memcpy(&T,  X, sizeof(tiny_mpi));
    memcpy(X,  Y, sizeof(tiny_mpi));
    memcpy(Y, &T, sizeof(tiny_mpi));
}

/*
 * Conditionally assign X = Y, without leaking information
 * about whether the assignment was made or not.
 * (Leaking information about the respective sizes of X and Y is ok however.)
 */
int tiny_mpi_safe_cond_assign(tiny_mpi *X, const tiny_mpi *Y, unsigned char assign)
{
    int ret = 0;
    int i;

    /* make sure assign is 0 or 1 in a time-constant manner */
    assign = (assign | (unsigned char) - assign) >> 7;

    TINY_MPI_CHK(tiny_mpi_grow(X, Y->n));

    X->s = X->s * (1 - assign) + Y->s * assign;

    for (i = 0; i < Y->n; i++)
        X->p[i] = X->p[i] * (1 - assign) + Y->p[i] * assign;

    for (; i < X->n; i++)
        X->p[i] *= (1 - assign);

cleanup:
    return (ret);
}

/*
 * Conditionally swap X and Y, without leaking information
 * about whether the swap was made or not.
 * Here it is not ok to simply swap the pointers, which whould lead to
 * different memory access patterns when X and Y are used afterwards.
 */
int tiny_mpi_safe_cond_swap(tiny_mpi *X, tiny_mpi *Y, unsigned char swap)
{
    int ret, s;
    int i;
    tiny_mpi_uint tmp;

    if (X == Y)
        return (0);

    /* make sure swap is 0 or 1 in a time-constant manner */
    swap = (swap | (unsigned char) - swap) >> 7;

    TINY_MPI_CHK(tiny_mpi_grow(X, Y->n));
    TINY_MPI_CHK(tiny_mpi_grow(Y, X->n));

    s = X->s;
    X->s = X->s * (1 - swap) + Y->s * swap;
    Y->s = Y->s * (1 - swap) +    s * swap;


    for (i = 0; i < X->n; i++)
    {
        tmp = X->p[i];
        X->p[i] = X->p[i] * (1 - swap) + Y->p[i] * swap;
        Y->p[i] = Y->p[i] * (1 - swap) +     tmp * swap;
    }

cleanup:
    return (ret);
}

/*
 * Set value from integer
 */
int tiny_mpi_lset(tiny_mpi *X, tiny_mpi_sint z)
{
    int ret;

    TINY_MPI_CHK(tiny_mpi_grow(X, 1));
    memset(X->p, 0, X->n * ciL);

    X->p[0] = (z < 0) ? -z : z;
    X->s    = (z < 0) ? -1 : 1;

cleanup:

    return (ret);
}

/*
 * Get a specific bit
 */
int tiny_mpi_get_bit(const tiny_mpi *X, int pos)
{
    if (X->n * biL <= pos)
        return (0);

    return ((X->p[pos / biL] >> (pos % biL)) & 0x01);
}

/* Get a specific byte, without range checks. */
#define GET_BYTE( X, i )                                \
    ( ( ( X )->p[( i ) / ciL] >> ( ( ( i ) % ciL ) * 8 ) ) & 0xff )

/*
 * Set a bit to a specific value of 0 or 1
 */
int tiny_mpi_set_bit(tiny_mpi *X, int pos, unsigned char val)
{
    int ret = 0;
    int off = pos / biL;
    int idx = pos % biL;

    if (val != 0 && val != 1)
        return (-1);

    if (X->n * biL <= pos)
    {
        if (val == 0)
            return (0);

        TINY_MPI_CHK(tiny_mpi_grow(X, off + 1));
    }

    X->p[off] &= ~((tiny_mpi_uint) 0x01 << idx);
    X->p[off] |= (tiny_mpi_uint) val << idx;

cleanup:

    return (ret);
}

/*
 * Return the number of less significant zero-bits
 */
int tiny_mpi_lsb(const tiny_mpi *X)
{
    int i, j, count = 0;

    for (i = 0; i < X->n; i++)
        for (j = 0; j < biL; j++, count++)
            if (((X->p[i] >> j) & 1) != 0)
                return (count);

    return (0);
}

/*
 * Count leading zero bits in a given integer
 */
static int tiny_clz(const tiny_mpi_uint x)
{
    int j;
    tiny_mpi_uint mask = (tiny_mpi_uint) 1 << (biL - 1);

    for (j = 0; j < biL; j++)
    {
        if (x & mask) break;

        mask >>= 1;
    }

    return j;
}

/*
 * Return the number of bits
 */
int tiny_mpi_bitlen(const tiny_mpi *X)
{
    int i, j;

    if (X->n == 0)
        return (0);

    for (i = X->n - 1; i > 0; i--)
        if (X->p[i] != 0)
            break;

    j = biL - tiny_clz(X->p[i]);

    return ((i * biL) + j);
}

/*
 * Return the total size in bytes
 */
int tiny_mpi_size(const tiny_mpi *X)
{
    return ((tiny_mpi_bitlen(X) + 7) >> 3);
}

/*
 * Helper to write the digits high-order first.
 */
static int mpi_write_hlp(tiny_mpi *X, int radix,
                         char **p, const int buflen)
{
    int ret;
    tiny_mpi_uint r;
    int length = 0;
    char *p_end = *p + buflen;

    do
    {
        if (length >= buflen)
        {
            return (-1);
        }

        TINY_MPI_CHK(tiny_mpi_mod_int(&r, X, radix));
        TINY_MPI_CHK(tiny_mpi_div_int(X, NULL, X, radix));
        /*
         * Write the residue in the current position, as an ASCII character.
         */
        if (r < 0xA)
            *(--p_end) = (char)('0' + r);
        else
            *(--p_end) = (char)('A' + (r - 0xA));

        length++;
    }
    while (tiny_mpi_cmp_int(X, 0) != 0);

    memmove(*p, p_end, length);
    *p += length;

cleanup:

    return (ret);
}

/*
 * Export into an ASCII string
 */
int tiny_mpi_write_string(const tiny_mpi *X, int radix,
                          char *buf, int buflen, int *olen)
{
    int ret = 0;
    int n;
    char *p;
    tiny_mpi T;

    if (radix < 2 || radix > 16)
        return (-1);

    n = tiny_mpi_bitlen(X);
    if (radix >=  4) n >>= 1;
    if (radix >= 16) n >>= 1;
    /*
     * Round up the buffer length to an even value to ensure that there is
     * enough room for hexadecimal values that can be represented in an odd
     * number of digits.
     */
    n += 3 + ((n + 1) & 1);

    if (buflen < n)
    {
        *olen = n;
        return (-1);
    }

    p = buf;
    tiny_mpi_init(&T);

    if (X->s == -1)
        *p++ = '-';

    if (radix == 16)
    {
        int c;
        int i, j, k;

        for (i = X->n, k = 0; i > 0; i--)
        {
            for (j = ciL; j > 0; j--)
            {
                c = (X->p[i - 1] >> ((j - 1) << 3)) & 0xFF;

                if (c == 0 && k == 0 && (i + j) != 2)
                    continue;

                *(p++) = "0123456789ABCDEF" [c / 16];
                *(p++) = "0123456789ABCDEF" [c % 16];
                k = 1;
            }
        }
    }
    else
    {
        TINY_MPI_CHK(tiny_mpi_copy(&T, X));

        if (T.s == -1)
            T.s = 1;

        TINY_MPI_CHK(mpi_write_hlp(&T, radix, &p, buflen));
    }

    *p++ = '\0';
    *olen = p - buf;

cleanup:

    tiny_mpi_free(&T);

    return (ret);
}

/*
 * Import X from unsigned binary data, big endian
 */
int tiny_mpi_read_binary(tiny_mpi *X, const unsigned char *buf, int buflen)
{
    int ret;
    int i, j;
    int const limbs = CHARS_TO_LIMBS(buflen);

    /* Ensure that target MPI has exactly the necessary number of limbs */
    if (X->n != limbs)
    {
        tiny_mpi_free(X);
        tiny_mpi_init(X);
        TINY_MPI_CHK(tiny_mpi_grow(X, limbs));
    }

    TINY_MPI_CHK(tiny_mpi_lset(X, 0));

    for (i = buflen, j = 0; i > 0; i--, j++)
        X->p[j / ciL] |= ((tiny_mpi_uint) buf[i - 1]) << ((j % ciL) << 3);

cleanup:

    return (ret);
}

/*
 * Export X into unsigned binary data, big endian
 */
int tiny_mpi_write_binary(const tiny_mpi *X,
                          unsigned char *buf, int buflen)
{
    int stored_bytes = X->n * ciL;
    int bytes_to_copy;
    unsigned char *p;
    int i;

    if (stored_bytes < buflen)
    {
        /* There is enough space in the output buffer. Write initial
         * null bytes and record the position at which to start
         * writing the significant bytes. In this case, the execution
         * trace of this function does not depend on the value of the
         * number. */
        bytes_to_copy = stored_bytes;
        p = buf + buflen - stored_bytes;
        memset(buf, 0, buflen - stored_bytes);
    }
    else
    {
        /* The output buffer is smaller than the allocated size of X.
         * However X may fit if its leading bytes are zero. */
        bytes_to_copy = buflen;
        p = buf;
        for (i = bytes_to_copy; i < stored_bytes; i++)
        {
            if (GET_BYTE(X, i) != 0)
                return (-1);
        }
    }

    for (i = 0; i < bytes_to_copy; i++)
        p[bytes_to_copy - i - 1] = GET_BYTE(X, i);

    return (0);
}

/*
 * Left-shift: X <<= count
 */
int tiny_mpi_shift_l(tiny_mpi *X, int count)
{
    int ret;
    int i, v0, t1;
    tiny_mpi_uint r0 = 0, r1;

    v0 = count / (biL);
    t1 = count & (biL - 1);

    i = tiny_mpi_bitlen(X) + count;

    if (X->n * biL < i)
        TINY_MPI_CHK(tiny_mpi_grow(X, BITS_TO_LIMBS(i)));

    ret = 0;

    /*
     * shift by count / limb_size
     */
    if (v0 > 0)
    {
        for (i = X->n; i > v0; i--)
            X->p[i - 1] = X->p[i - v0 - 1];

        for (; i > 0; i--)
            X->p[i - 1] = 0;
    }

    /*
     * shift by count % limb_size
     */
    if (t1 > 0)
    {
        for (i = v0; i < X->n; i++)
        {
            r1 = X->p[i] >> (biL - t1);
            X->p[i] <<= t1;
            X->p[i] |= r0;
            r0 = r1;
        }
    }

cleanup:

    return (ret);
}

/*
 * Right-shift: X >>= count
 */
int tiny_mpi_shift_r(tiny_mpi *X, int count)
{
    int i, v0, v1;
    tiny_mpi_uint r0 = 0, r1;

    v0 = count /  biL;
    v1 = count & (biL - 1);

    if (v0 > X->n || (v0 == X->n && v1 > 0))
        return tiny_mpi_lset(X, 0);

    /*
     * shift by count / limb_size
     */
    if (v0 > 0)
    {
        for (i = 0; i < X->n - v0; i++)
            X->p[i] = X->p[i + v0];

        for (; i < X->n; i++)
            X->p[i] = 0;
    }

    /*
     * shift by count % limb_size
     */
    if (v1 > 0)
    {
        for (i = X->n; i > 0; i--)
        {
            r1 = X->p[i - 1] << (biL - v1);
            X->p[i - 1] >>= v1;
            X->p[i - 1] |= r0;
            r0 = r1;
        }
    }

    return (0);
}

/*
 * Compare unsigned values
 */
int tiny_mpi_cmp_abs(const tiny_mpi *X, const tiny_mpi *Y)
{
    int i, j;

    for (i = X->n; i > 0; i--)
        if (X->p[i - 1] != 0)
            break;

    for (j = Y->n; j > 0; j--)
        if (Y->p[j - 1] != 0)
            break;

    if (i == 0 && j == 0)
        return (0);

    if (i > j) return (1);
    if (j > i) return (-1);

    for (; i > 0; i--)
    {
        if (X->p[i - 1] > Y->p[i - 1]) return (1);
        if (X->p[i - 1] < Y->p[i - 1]) return (-1);
    }

    return (0);
}

/*
 * Compare signed values
 */
int tiny_mpi_cmp_mpi(const tiny_mpi *X, const tiny_mpi *Y)
{
    int i, j;

    for (i = X->n; i > 0; i--)
        if (X->p[i - 1] != 0)
            break;

    for (j = Y->n; j > 0; j--)
        if (Y->p[j - 1] != 0)
            break;

    if (i == 0 && j == 0)
        return (0);

    if (i > j) return (X->s);
    if (j > i) return (-Y->s);

    if (X->s > 0 && Y->s < 0) return (1);
    if (Y->s > 0 && X->s < 0) return (-1);

    for (; i > 0; i--)
    {
        if (X->p[i - 1] > Y->p[i - 1]) return (X->s);
        if (X->p[i - 1] < Y->p[i - 1]) return (-X->s);
    }

    return (0);
}

/*
 * Compare signed values
 */
int tiny_mpi_cmp_int(const tiny_mpi *X, tiny_mpi_sint z)
{
    tiny_mpi Y;
    tiny_mpi_uint p[1];

    *p  = (z < 0) ? -z : z;
    Y.s = (z < 0) ? -1 : 1;
    Y.n = 1;
    Y.p = p;

    return (tiny_mpi_cmp_mpi(X, &Y));
}

/*
 * Unsigned addition: X = |A| + |B|  (HAC 14.7)
 */
int tiny_mpi_add_abs(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret;
    int i, j;
    tiny_mpi_uint *o, *p, c, tmp;

    if (X == B)
    {
        const tiny_mpi *T = A;
        A = X;
        B = T;
    }

    if (X != A)
        TINY_MPI_CHK(tiny_mpi_copy(X, A));

    /*
     * X should always be positive as a result of unsigned additions.
     */
    X->s = 1;

    for (j = B->n; j > 0; j--)
        if (B->p[j - 1] != 0)
            break;

    TINY_MPI_CHK(tiny_mpi_grow(X, j));

    o = B->p;
    p = X->p;
    c = 0;

    /*
     * tmp is used because it might happen that p == o
     */
    for (i = 0; i < j; i++, o++, p++)
    {
        tmp = *o;
        *p +=  c;
        c  = (*p <  c);
        *p += tmp;
        c += (*p < tmp);
    }

    while (c != 0)
    {
        if (i >= X->n)
        {
            TINY_MPI_CHK(tiny_mpi_grow(X, i + 1));
            p = X->p + i;
        }

        *p += c;
        c = (*p < c);
        i++;
        p++;
    }

cleanup:

    return (ret);
}

/*
 * Helper for tiny_mpi subtraction
 */
static void mpi_sub_hlp(int n, tiny_mpi_uint *s, tiny_mpi_uint *d)
{
    int i;
    tiny_mpi_uint c, z;

    for (i = c = 0; i < n; i++, s++, d++)
    {
        z = (*d <  c);
        *d -=  c;
        c = (*d < *s) + z;
        *d -= *s;
    }

    while (c != 0)
    {
        z = (*d < c);
        *d -= c;
        c = z;
        i++;
        d++;
    }
}

/*
 * Unsigned subtraction: X = |A| - |B|  (HAC 14.9)
 */
int tiny_mpi_sub_abs(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *B)
{
    tiny_mpi TB;
    int ret;
    int n;

    if (tiny_mpi_cmp_abs(A, B) < 0)
        return (-1);

    tiny_mpi_init(&TB);

    if (X == B)
    {
        TINY_MPI_CHK(tiny_mpi_copy(&TB, B));
        B = &TB;
    }

    if (X != A)
        TINY_MPI_CHK(tiny_mpi_copy(X, A));

    /*
     * X should always be positive as a result of unsigned subtractions.
     */
    X->s = 1;

    ret = 0;

    for (n = B->n; n > 0; n--)
        if (B->p[n - 1] != 0)
            break;

    mpi_sub_hlp(n, B->p, X->p);

cleanup:

    tiny_mpi_free(&TB);

    return (ret);
}

/*
 * Signed addition: X = A + B
 */
int tiny_mpi_add_mpi(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret, s = A->s;

    if (A->s * B->s < 0)
    {
        if (tiny_mpi_cmp_abs(A, B) >= 0)
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(X, A, B));
            X->s =  s;
        }
        else
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(X, B, A));
            X->s = -s;
        }
    }
    else
    {
        TINY_MPI_CHK(tiny_mpi_add_abs(X, A, B));
        X->s = s;
    }

cleanup:

    return (ret);
}

/*
 * Signed subtraction: X = A - B
 */
int tiny_mpi_sub_mpi(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret, s = A->s;

    if (A->s * B->s > 0)
    {
        if (tiny_mpi_cmp_abs(A, B) >= 0)
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(X, A, B));
            X->s =  s;
        }
        else
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(X, B, A));
            X->s = -s;
        }
    }
    else
    {
        TINY_MPI_CHK(tiny_mpi_add_abs(X, A, B));
        X->s = s;
    }

cleanup:

    return (ret);
}

/*
 * Signed addition: X = A + b
 */
int tiny_mpi_add_int(tiny_mpi *X, const tiny_mpi *A, tiny_mpi_sint b)
{
    tiny_mpi _B;
    tiny_mpi_uint p[1];

    p[0] = (b < 0) ? -b : b;
    _B.s = (b < 0) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return (tiny_mpi_add_mpi(X, A, &_B));
}

/*
 * Signed subtraction: X = A - b
 */
int tiny_mpi_sub_int(tiny_mpi *X, const tiny_mpi *A, tiny_mpi_sint b)
{
    tiny_mpi _B;
    tiny_mpi_uint p[1];

    p[0] = (b < 0) ? -b : b;
    _B.s = (b < 0) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return (tiny_mpi_sub_mpi(X, A, &_B));
}

/*
 * Helper for tiny_mpi multiplication
 */
void mpi_mul_hlp(int i, tiny_mpi_uint *s, tiny_mpi_uint *d, tiny_mpi_uint b)
{
    tiny_mpi_uint c = 0, t = 0;

    for (; i >= 16; i -= 16)
    {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE

        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }

    for (; i >= 8; i -= 8)
    {
        MULADDC_INIT
        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE

        MULADDC_CORE   MULADDC_CORE
        MULADDC_CORE   MULADDC_CORE
        MULADDC_STOP
    }

    for (; i > 0; i--)
    {
        MULADDC_INIT
        MULADDC_CORE
        MULADDC_STOP
    }

    t++;

    do
    {
        *d += c;
        c = (*d < c);
        d++;
    }
    while (c != 0);
}

/*
 * Baseline multiplication: X = A * B  (HAC 14.12)
 */
int tiny_mpi_mul_mpi(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret;
    int i, j;
    tiny_mpi TA, TB;

    tiny_mpi_init(&TA);
    tiny_mpi_init(&TB);

    if (X == A)
    {
        TINY_MPI_CHK(tiny_mpi_copy(&TA, A));
        A = &TA;
    }
    if (X == B)
    {
        TINY_MPI_CHK(tiny_mpi_copy(&TB, B));
        B = &TB;
    }

    for (i = A->n; i > 0; i--)
        if (A->p[i - 1] != 0)
            break;

    for (j = B->n; j > 0; j--)
        if (B->p[j - 1] != 0)
            break;

    TINY_MPI_CHK(tiny_mpi_grow(X, i + j));
    TINY_MPI_CHK(tiny_mpi_lset(X, 0));

    for (i++; j > 0; j--)
        mpi_mul_hlp(i - 1, A->p, X->p + j - 1, B->p[j - 1]);

    X->s = A->s * B->s;

cleanup:

    tiny_mpi_free(&TB);
    tiny_mpi_free(&TA);

    return (ret);
}

/*
 * Baseline multiplication: X = A * b
 */
int tiny_mpi_mul_int(tiny_mpi *X, const tiny_mpi *A, tiny_mpi_uint b)
{
    tiny_mpi _B;
    tiny_mpi_uint p[1];

    _B.s = 1;
    _B.n = 1;
    _B.p = p;
    p[0] = b;

    return (tiny_mpi_mul_mpi(X, A, &_B));
}

/*
 * Unsigned integer divide - double tiny_mpi_uint dividend, u1/u0, and
 * tiny_mpi_uint divisor, d
 */
static tiny_mpi_uint tiny_int_div_int(tiny_mpi_uint u1,
                                      tiny_mpi_uint u0, tiny_mpi_uint d, tiny_mpi_uint *r)
{
#if defined(TINY_HAVE_UDBL)
    tiny_t_udbl dividend, quotient;
#else
    const tiny_mpi_uint radix = (tiny_mpi_uint) 1 << biH;
    const tiny_mpi_uint uint_halfword_mask = ((tiny_mpi_uint) 1 << biH) - 1;
    tiny_mpi_uint d0, d1, q0, q1, rAX, r0, quotient;
    tiny_mpi_uint u0_msw, u0_lsw;
    int s;
#endif

    /*
     * Check for overflow
     */
    if (0 == d || u1 >= d)
    {
        if (r != NULL) *r = ~0;

        return (~0);
    }

#if defined(TINY_HAVE_UDBL)
    dividend  = (tiny_t_udbl) u1 << biL;
    dividend |= (tiny_t_udbl) u0;
    quotient = dividend / d;
    if (quotient > ((tiny_t_udbl) 1 << biL) - 1)
        quotient = ((tiny_t_udbl) 1 << biL) - 1;

    if (r != NULL)
        *r = (tiny_mpi_uint)(dividend - (quotient * d));

    return (tiny_mpi_uint) quotient;
#else

    /*
     * Algorithm D, Section 4.3.1 - The Art of Computer Programming
     *   Vol. 2 - Seminumerical Algorithms, Knuth
     */

    /*
     * Normalize the divisor, d, and dividend, u0, u1
     */
    s = tiny_clz(d);
    d = d << s;

    u1 = u1 << s;
    u1 |= (u0 >> (biL - s)) & (-(tiny_mpi_sint)s >> (biL - 1));
    u0 =  u0 << s;

    d1 = d >> biH;
    d0 = d & uint_halfword_mask;

    u0_msw = u0 >> biH;
    u0_lsw = u0 & uint_halfword_mask;

    /*
     * Find the first quotient and remainder
     */
    q1 = u1 / d1;
    r0 = u1 - d1 * q1;

    while (q1 >= radix || (q1 * d0 > radix * r0 + u0_msw))
    {
        q1 -= 1;
        r0 += d1;

        if (r0 >= radix) break;
    }

    rAX = (u1 * radix) + (u0_msw - q1 * d);
    q0 = rAX / d1;
    r0 = rAX - q0 * d1;

    while (q0 >= radix || (q0 * d0 > radix * r0 + u0_lsw))
    {
        q0 -= 1;
        r0 += d1;

        if (r0 >= radix) break;
    }

    if (r != NULL)
        *r = (rAX * radix + u0_lsw - q0 * d) >> s;

    quotient = q1 * radix + q0;

    return quotient;
#endif
}

/*
 * Division by tiny_mpi: A = Q * B + R  (HAC 14.20)
 */
int tiny_mpi_div_mpi(tiny_mpi *Q, tiny_mpi *R, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret;
    int i, n, t, k;
    tiny_mpi X, Y, Z, T1, T2;

    if (tiny_mpi_cmp_int(B, 0) == 0)
        return (-1);

    tiny_mpi_init(&X);
    tiny_mpi_init(&Y);
    tiny_mpi_init(&Z);
    tiny_mpi_init(&T1);
    tiny_mpi_init(&T2);

    if (tiny_mpi_cmp_abs(A, B) < 0)
    {
        if (Q != NULL) TINY_MPI_CHK(tiny_mpi_lset(Q, 0));
        if (R != NULL) TINY_MPI_CHK(tiny_mpi_copy(R, A));
        return (0);
    }

    TINY_MPI_CHK(tiny_mpi_copy(&X, A));
    TINY_MPI_CHK(tiny_mpi_copy(&Y, B));
    X.s = Y.s = 1;

    TINY_MPI_CHK(tiny_mpi_grow(&Z, A->n + 2));
    TINY_MPI_CHK(tiny_mpi_lset(&Z,  0));
    TINY_MPI_CHK(tiny_mpi_grow(&T1, 2));
    TINY_MPI_CHK(tiny_mpi_grow(&T2, 3));

    k = tiny_mpi_bitlen(&Y) % biL;
    if (k < biL - 1)
    {
        k = biL - 1 - k;
        TINY_MPI_CHK(tiny_mpi_shift_l(&X, k));
        TINY_MPI_CHK(tiny_mpi_shift_l(&Y, k));
    }
    else k = 0;

    n = X.n - 1;
    t = Y.n - 1;
    TINY_MPI_CHK(tiny_mpi_shift_l(&Y, biL * (n - t)));

    while (tiny_mpi_cmp_mpi(&X, &Y) >= 0)
    {
        Z.p[n - t]++;
        TINY_MPI_CHK(tiny_mpi_sub_mpi(&X, &X, &Y));
    }
    TINY_MPI_CHK(tiny_mpi_shift_r(&Y, biL * (n - t)));

    for (i = n; i > t ; i--)
    {
        if (X.p[i] >= Y.p[t])
            Z.p[i - t - 1] = ~0;
        else
        {
            Z.p[i - t - 1] = tiny_int_div_int(X.p[i], X.p[i - 1],
                                              Y.p[t], NULL);
        }

        Z.p[i - t - 1]++;
        do
        {
            Z.p[i - t - 1]--;

            TINY_MPI_CHK(tiny_mpi_lset(&T1, 0));
            T1.p[0] = (t < 1) ? 0 : Y.p[t - 1];
            T1.p[1] = Y.p[t];
            TINY_MPI_CHK(tiny_mpi_mul_int(&T1, &T1, Z.p[i - t - 1]));

            TINY_MPI_CHK(tiny_mpi_lset(&T2, 0));
            T2.p[0] = (i < 2) ? 0 : X.p[i - 2];
            T2.p[1] = (i < 1) ? 0 : X.p[i - 1];
            T2.p[2] = X.p[i];
        }
        while (tiny_mpi_cmp_mpi(&T1, &T2) > 0);

        TINY_MPI_CHK(tiny_mpi_mul_int(&T1, &Y, Z.p[i - t - 1]));
        TINY_MPI_CHK(tiny_mpi_shift_l(&T1,  biL * (i - t - 1)));
        TINY_MPI_CHK(tiny_mpi_sub_mpi(&X, &X, &T1));

        if (tiny_mpi_cmp_int(&X, 0) < 0)
        {
            TINY_MPI_CHK(tiny_mpi_copy(&T1, &Y));
            TINY_MPI_CHK(tiny_mpi_shift_l(&T1, biL * (i - t - 1)));
            TINY_MPI_CHK(tiny_mpi_add_mpi(&X, &X, &T1));
            Z.p[i - t - 1]--;
        }
    }

    if (Q != NULL)
    {
        TINY_MPI_CHK(tiny_mpi_copy(Q, &Z));
        Q->s = A->s * B->s;
    }

    if (R != NULL)
    {
        TINY_MPI_CHK(tiny_mpi_shift_r(&X, k));
        X.s = A->s;
        TINY_MPI_CHK(tiny_mpi_copy(R, &X));

        if (tiny_mpi_cmp_int(R, 0) == 0)
            R->s = 1;
    }

cleanup:

    tiny_mpi_free(&X);
    tiny_mpi_free(&Y);
    tiny_mpi_free(&Z);
    tiny_mpi_free(&T1);
    tiny_mpi_free(&T2);

    return (ret);
}

/*
 * Division by int: A = Q * b + R
 */
int tiny_mpi_div_int(tiny_mpi *Q, tiny_mpi *R, const tiny_mpi *A, tiny_mpi_sint b)
{
    tiny_mpi _B;
    tiny_mpi_uint p[1];

    p[0] = (b < 0) ? -b : b;
    _B.s = (b < 0) ? -1 : 1;
    _B.n = 1;
    _B.p = p;

    return (tiny_mpi_div_mpi(Q, R, A, &_B));
}

/*
 * Modulo: R = A mod B
 */
int tiny_mpi_mod_mpi(tiny_mpi *R, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret;

    if (tiny_mpi_cmp_int(B, 0) < 0)
        return (-1);

    TINY_MPI_CHK(tiny_mpi_div_mpi(NULL, R, A, B));

    while (tiny_mpi_cmp_int(R, 0) < 0)
        TINY_MPI_CHK(tiny_mpi_add_mpi(R, R, B));

    while (tiny_mpi_cmp_mpi(R, B) >= 0)
        TINY_MPI_CHK(tiny_mpi_sub_mpi(R, R, B));

cleanup:

    return (ret);
}

/*
 * Modulo: r = A mod b
 */
int tiny_mpi_mod_int(tiny_mpi_uint *r, const tiny_mpi *A, tiny_mpi_sint b)
{
    int i;
    tiny_mpi_uint x, y, z;

    if (b == 0)
        return (-1);

    if (b < 0)
        return (-1);

    /*
     * handle trivial cases
     */
    if (b == 1)
    {
        *r = 0;
        return (0);
    }

    if (b == 2)
    {
        *r = A->p[0] & 1;
        return (0);
    }

    /*
     * general case
     */
    for (i = A->n, y = 0; i > 0; i--)
    {
        x  = A->p[i - 1];
        y  = (y << biH) | (x >> biH);
        z  = y / b;
        y -= z * b;

        x <<= biH;
        y  = (y << biH) | (x >> biH);
        z  = y / b;
        y -= z * b;
    }

    /*
     * If A is negative, then the current y represents a negative value.
     * Flipping it to the positive side.
     */
    if (A->s < 0 && y != 0)
        y = b - y;

    *r = y;

    return (0);
}

/*
 * Fast Montgomery initialization (thanks to Tom St Denis)
 */
static void mpi_montg_init(tiny_mpi_uint *mm, const tiny_mpi *N)
{
    tiny_mpi_uint x, m0 = N->p[0];
    unsigned int i;

    x  = m0;
    x += ((m0 + 2) & 4) << 1;

    for (i = biL; i >= 8; i /= 2)
        x *= (2 - (m0 * x));

    *mm = ~x + 1;
}

/*
 * Montgomery multiplication: A = A * B * R^-1 mod N  (HAC 14.36)
 */
static int mpi_montmul(tiny_mpi *A, const tiny_mpi *B, const tiny_mpi *N, tiny_mpi_uint mm,
                       const tiny_mpi *T)
{
    int i, n, m;
    tiny_mpi_uint u0, u1, *d;

    if (T->n < N->n + 1 || T->p == NULL)
        return (-1);

    memset(T->p, 0, T->n * ciL);

    d = T->p;
    n = N->n;
    m = (B->n < n) ? B->n : n;

    for (i = 0; i < n; i++)
    {
        /*
         * T = (T + u0*B + u1*N) / 2^biL
         */
        u0 = A->p[i];
        u1 = (d[0] + u0 * B->p[0]) * mm;

        mpi_mul_hlp(m, B->p, d, u0);
        mpi_mul_hlp(n, N->p, d, u1);

        *d++ = u0;
        d[n + 1] = 0;
    }

    memcpy(A->p, d, (n + 1) * ciL);

    if (tiny_mpi_cmp_abs(A, N) >= 0)
        mpi_sub_hlp(n, N->p, A->p);
    else
        /* prevent timing attacks */
        mpi_sub_hlp(n, A->p, T->p);

    return (0);
}

/*
 * Montgomery reduction: A = A * R^-1 mod N
 */
static int mpi_montred(tiny_mpi *A, const tiny_mpi *N, tiny_mpi_uint mm, const tiny_mpi *T)
{
    tiny_mpi_uint z = 1;
    tiny_mpi U;

    U.n = U.s = (int) z;
    U.p = &z;

    return (mpi_montmul(A, &U, N, mm, T));
}

/*
 * Sliding-window exponentiation: X = A^E mod N  (HAC 14.85)
 */
int tiny_mpi_exp_mod(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *E, const tiny_mpi *N, tiny_mpi *_RR)
{
    int ret;
    int wbits, wsize, one = 1;
    int i, j, nblimbs;
    int bufsize, nbits;
    tiny_mpi_uint ei, mm, state;
    tiny_mpi RR, T, W[ 2 << TINY_MPI_WINDOW_SIZE ], Apos;
    int neg;

    if (tiny_mpi_cmp_int(N, 0) <= 0 || (N->p[0] & 1) == 0)
        return (-1);

    if (tiny_mpi_cmp_int(E, 0) < 0)
        return (-1);

    /*
     * Init temps and window size
     */
    mpi_montg_init(&mm, N);
    tiny_mpi_init(&RR);
    tiny_mpi_init(&T);
    tiny_mpi_init(&Apos);
    memset(W, 0, sizeof(W));

    i = tiny_mpi_bitlen(E);

    wsize = (i > 671) ? 6 : (i > 239) ? 5 :
            (i >  79) ? 4 : (i >  23) ? 3 : 1;

    if (wsize > TINY_MPI_WINDOW_SIZE)
        wsize = TINY_MPI_WINDOW_SIZE;

    j = N->n + 1;
    TINY_MPI_CHK(tiny_mpi_grow(X, j));
    TINY_MPI_CHK(tiny_mpi_grow(&W[1],  j));
    TINY_MPI_CHK(tiny_mpi_grow(&T, j * 2));

    /*
     * Compensate for negative A (and correct at the end)
     */
    neg = (A->s == -1);
    if (neg)
    {
        TINY_MPI_CHK(tiny_mpi_copy(&Apos, A));
        Apos.s = 1;
        A = &Apos;
    }

    /*
     * If 1st call, pre-compute R^2 mod N
     */
    if (_RR == NULL || _RR->p == NULL)
    {
        TINY_MPI_CHK(tiny_mpi_lset(&RR, 1));
        TINY_MPI_CHK(tiny_mpi_shift_l(&RR, N->n * 2 * biL));
        TINY_MPI_CHK(tiny_mpi_mod_mpi(&RR, &RR, N));

        if (_RR != NULL)
            memcpy(_RR, &RR, sizeof(tiny_mpi));
    }
    else
        memcpy(&RR, _RR, sizeof(tiny_mpi));

    /*
     * W[1] = A * R^2 * R^-1 mod N = A * R mod N
     */
    if (tiny_mpi_cmp_mpi(A, N) >= 0)
        TINY_MPI_CHK(tiny_mpi_mod_mpi(&W[1], A, N));
    else
        TINY_MPI_CHK(tiny_mpi_copy(&W[1], A));

    TINY_MPI_CHK(mpi_montmul(&W[1], &RR, N, mm, &T));

    /*
     * X = R^2 * R^-1 mod N = R mod N
     */
    TINY_MPI_CHK(tiny_mpi_copy(X, &RR));
    TINY_MPI_CHK(mpi_montred(X, N, mm, &T));

    if (wsize > 1)
    {
        /*
         * W[1 << (wsize - 1)] = W[1] ^ (wsize - 1)
         */
        j =  one << (wsize - 1);

        TINY_MPI_CHK(tiny_mpi_grow(&W[j], N->n + 1));
        TINY_MPI_CHK(tiny_mpi_copy(&W[j], &W[1]));

        for (i = 0; i < wsize - 1; i++)
            TINY_MPI_CHK(mpi_montmul(&W[j], &W[j], N, mm, &T));

        /*
         * W[i] = W[i - 1] * W[1]
         */
        for (i = j + 1; i < (one << wsize); i++)
        {
            TINY_MPI_CHK(tiny_mpi_grow(&W[i], N->n + 1));
            TINY_MPI_CHK(tiny_mpi_copy(&W[i], &W[i - 1]));

            TINY_MPI_CHK(mpi_montmul(&W[i], &W[1], N, mm, &T));
        }
    }

    nblimbs = E->n;
    bufsize = 0;
    nbits   = 0;
    wbits   = 0;
    state   = 0;

    while (1)
    {
        if (bufsize == 0)
        {
            if (nblimbs == 0)
                break;

            nblimbs--;

            bufsize = sizeof(tiny_mpi_uint) << 3;
        }

        bufsize--;

        ei = (E->p[nblimbs] >> bufsize) & 1;

        /*
         * skip leading 0s
         */
        if (ei == 0 && state == 0)
            continue;

        if (ei == 0 && state == 1)
        {
            /*
             * out of window, square X
             */
            TINY_MPI_CHK(mpi_montmul(X, X, N, mm, &T));
            continue;
        }

        /*
         * add ei to current window
         */
        state = 2;

        nbits++;
        wbits |= (ei << (wsize - nbits));

        if (nbits == wsize)
        {
            /*
             * X = X^wsize R^-1 mod N
             */
            for (i = 0; i < wsize; i++)
                TINY_MPI_CHK(mpi_montmul(X, X, N, mm, &T));

            /*
             * X = X * W[wbits] R^-1 mod N
             */
            TINY_MPI_CHK(mpi_montmul(X, &W[wbits], N, mm, &T));

            state--;
            nbits = 0;
            wbits = 0;
        }
    }

    /*
     * process the remaining bits
     */
    for (i = 0; i < nbits; i++)
    {
        TINY_MPI_CHK(mpi_montmul(X, X, N, mm, &T));

        wbits <<= 1;

        if ((wbits & (one << wsize)) != 0)
            TINY_MPI_CHK(mpi_montmul(X, &W[1], N, mm, &T));
    }

    /*
     * X = A^E * R * R^-1 mod N = A^E mod N
     */
    TINY_MPI_CHK(mpi_montred(X, N, mm, &T));

    if (neg && E->n != 0 && (E->p[0] & 1) != 0)
    {
        X->s = -1;
        TINY_MPI_CHK(tiny_mpi_add_mpi(X, N, X));
    }

cleanup:

    for (i = (one << (wsize - 1)); i < (one << wsize); i++)
        tiny_mpi_free(&W[i]);

    tiny_mpi_free(&W[1]);
    tiny_mpi_free(&T);
    tiny_mpi_free(&Apos);

    if (_RR == NULL || _RR->p == NULL)
        tiny_mpi_free(&RR);

    return (ret);
}

/*
 * Greatest common divisor: G = gcd(A, B)  (HAC 14.54)
 */
int tiny_mpi_gcd(tiny_mpi *G, const tiny_mpi *A, const tiny_mpi *B)
{
    int ret;
    int lz, lzt;
    tiny_mpi TG, TA, TB;

    tiny_mpi_init(&TG);
    tiny_mpi_init(&TA);
    tiny_mpi_init(&TB);

    TINY_MPI_CHK(tiny_mpi_copy(&TA, A));
    TINY_MPI_CHK(tiny_mpi_copy(&TB, B));

    lz = tiny_mpi_lsb(&TA);
    lzt = tiny_mpi_lsb(&TB);

    if (lzt < lz)
        lz = lzt;

    TINY_MPI_CHK(tiny_mpi_shift_r(&TA, lz));
    TINY_MPI_CHK(tiny_mpi_shift_r(&TB, lz));

    TA.s = TB.s = 1;

    while (tiny_mpi_cmp_int(&TA, 0) != 0)
    {
        TINY_MPI_CHK(tiny_mpi_shift_r(&TA, tiny_mpi_lsb(&TA)));
        TINY_MPI_CHK(tiny_mpi_shift_r(&TB, tiny_mpi_lsb(&TB)));

        if (tiny_mpi_cmp_mpi(&TA, &TB) >= 0)
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(&TA, &TA, &TB));
            TINY_MPI_CHK(tiny_mpi_shift_r(&TA, 1));
        }
        else
        {
            TINY_MPI_CHK(tiny_mpi_sub_abs(&TB, &TB, &TA));
            TINY_MPI_CHK(tiny_mpi_shift_r(&TB, 1));
        }
    }

    TINY_MPI_CHK(tiny_mpi_shift_l(&TB, lz));
    TINY_MPI_CHK(tiny_mpi_copy(G, &TB));

cleanup:

    tiny_mpi_free(&TG);
    tiny_mpi_free(&TA);
    tiny_mpi_free(&TB);

    return (ret);
}

/*
 * Modular inverse: X = A^-1 mod N  (HAC 14.61 / 14.64)
 */
int tiny_mpi_inv_mod(tiny_mpi *X, const tiny_mpi *A, const tiny_mpi *N)
{
    int ret;
    tiny_mpi G, TA, TU, U1, U2, TB, TV, V1, V2;

    if (tiny_mpi_cmp_int(N, 1) <= 0)
        return (-1);

    tiny_mpi_init(&TA);
    tiny_mpi_init(&TU);
    tiny_mpi_init(&U1);
    tiny_mpi_init(&U2);
    tiny_mpi_init(&G);
    tiny_mpi_init(&TB);
    tiny_mpi_init(&TV);
    tiny_mpi_init(&V1);
    tiny_mpi_init(&V2);

    TINY_MPI_CHK(tiny_mpi_gcd(&G, A, N));

    if (tiny_mpi_cmp_int(&G, 1) != 0)
    {
        ret = -1;
        goto cleanup;
    }

    TINY_MPI_CHK(tiny_mpi_mod_mpi(&TA, A, N));
    TINY_MPI_CHK(tiny_mpi_copy(&TU, &TA));
    TINY_MPI_CHK(tiny_mpi_copy(&TB, N));
    TINY_MPI_CHK(tiny_mpi_copy(&TV, N));

    TINY_MPI_CHK(tiny_mpi_lset(&U1, 1));
    TINY_MPI_CHK(tiny_mpi_lset(&U2, 0));
    TINY_MPI_CHK(tiny_mpi_lset(&V1, 0));
    TINY_MPI_CHK(tiny_mpi_lset(&V2, 1));

    do
    {
        while ((TU.p[0] & 1) == 0)
        {
            TINY_MPI_CHK(tiny_mpi_shift_r(&TU, 1));

            if ((U1.p[0] & 1) != 0 || (U2.p[0] & 1) != 0)
            {
                TINY_MPI_CHK(tiny_mpi_add_mpi(&U1, &U1, &TB));
                TINY_MPI_CHK(tiny_mpi_sub_mpi(&U2, &U2, &TA));
            }

            TINY_MPI_CHK(tiny_mpi_shift_r(&U1, 1));
            TINY_MPI_CHK(tiny_mpi_shift_r(&U2, 1));
        }

        while ((TV.p[0] & 1) == 0)
        {
            TINY_MPI_CHK(tiny_mpi_shift_r(&TV, 1));

            if ((V1.p[0] & 1) != 0 || (V2.p[0] & 1) != 0)
            {
                TINY_MPI_CHK(tiny_mpi_add_mpi(&V1, &V1, &TB));
                TINY_MPI_CHK(tiny_mpi_sub_mpi(&V2, &V2, &TA));
            }

            TINY_MPI_CHK(tiny_mpi_shift_r(&V1, 1));
            TINY_MPI_CHK(tiny_mpi_shift_r(&V2, 1));
        }

        if (tiny_mpi_cmp_mpi(&TU, &TV) >= 0)
        {
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&TU, &TU, &TV));
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&U1, &U1, &V1));
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&U2, &U2, &V2));
        }
        else
        {
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&TV, &TV, &TU));
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&V1, &V1, &U1));
            TINY_MPI_CHK(tiny_mpi_sub_mpi(&V2, &V2, &U2));
        }
    }
    while (tiny_mpi_cmp_int(&TU, 0) != 0);

    while (tiny_mpi_cmp_int(&V1, 0) < 0)
        TINY_MPI_CHK(tiny_mpi_add_mpi(&V1, &V1, N));

    while (tiny_mpi_cmp_mpi(&V1, N) >= 0)
        TINY_MPI_CHK(tiny_mpi_sub_mpi(&V1, &V1, N));

    TINY_MPI_CHK(tiny_mpi_copy(X, &V1));

cleanup:

    tiny_mpi_free(&TA);
    tiny_mpi_free(&TU);
    tiny_mpi_free(&U1);
    tiny_mpi_free(&U2);
    tiny_mpi_free(&G);
    tiny_mpi_free(&TB);
    tiny_mpi_free(&TV);
    tiny_mpi_free(&V1);
    tiny_mpi_free(&V2);

    return (ret);
}

#endif /* TINY_BIGNUM_C */
