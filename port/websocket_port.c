/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-1-4      tzy          first implementation
 */

#include "websocket.h"
#include <stdlib.h>
#include <string.h>
#include "tiny_base64.h"
#include "tiny_sha1.h"
#include <time.h>

void *ws_malloc(size_t size)
{
    return malloc(size);
}

void ws_free(void *ptr)
{
    free(ptr);
}

char *ws_strdup(const char *s)
{
    return strdup(s);
}

void *ws_memset(void *s, int c, size_t count)
{
    return memset(s, c, count);
}

void *ws_memcpy(void *dst, const void *src, size_t count)
{
    return memcpy(dst, src, count);
}

int ws_base64_encode(unsigned char *dst, int *dlen, unsigned char *src, int slen)
{
    return tiny_base64_encode(dst, dlen, src, slen);
}

void ws_sha1(unsigned char *input,
             size_t ilen,
             unsigned char output[20])
{
    tiny_sha1(input, ilen, output);
}

void ws_srand_key(unsigned char *buf, int len)
{
    char key_value;
    srand(time(0));
    for (int i = 0; i < len; i++)
    {
        if ((key_value = rand() % 128) > 32)
        {
            buf[i] = key_value;
        }
        else
        {
            buf[i] = 66;
        }
    }
}

void *ws_memmove(void *dest, const void *src, size_t n)
{
    return memmove(dest, src, n);
}
