/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date          Author       Notes
 * 2023-1-4      tzy          first implementation
 * 2023-6-24     tzy          modify url praser
 */
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "websocket.h"
#include "tls_client.h"

#define WEBSOCKET_TLS_BUFFER_SIZE                (2048)
#define WEBSOCKET_CACHE_BUFFER_SIZE              (512)
#define HEADER_CHECK_MIN_VALUE                   (0x000f)

#if WEBSOCKET_CACHE_BUFFER_SIZE < 512
    #error websocket cache buffer too small
#endif

struct websocket_frame_head
{
    unsigned char opcode: 4;
    unsigned char rsv: 3;
    unsigned char fin: 1;
    unsigned char payload_len: 7;
    unsigned char mask: 1;
};

struct control_frame
{
    struct websocket_frame_head head;
    unsigned char masking_key[4];
    unsigned char payload_data[1];
};

enum HEADER_CHECK
{
    HEADER_HAVE_101_SWITCH_PROTOCOL = 0,
    HEADER_HAVE_UPGRADE,
    HEADER_HAVE_CONNECTION,
    HEADER_HAVE_WEBSOCKET_ACCEPT,
    HEADER_HAVE_WEBSOCKET_PROTOCOL
};

static int websocket_send(struct websocket_session *session, const void *buf, size_t len, int flags)
{
    if (session->tls_session)
        return mbedtls_client_write(session->tls_session, buf, len);

    return send(session->socket_fd, buf, len, flags);
}

static int websocket_recv(struct websocket_session *session, void *buf, size_t len, int flags)
{
    if (session->tls_session)
        return mbedtls_client_read(session->tls_session, buf, len);

    return recv(session->socket_fd, buf, len, flags);
}

static int websocket_recv_nbytes(struct websocket_session *session, void *buf, size_t len, int flags)
{
    int read_len = 0;
    size_t pos = 0;

    while (len)
    {
        read_len = websocket_recv(session, (uint8_t *)buf + pos, len, flags);
        if (read_len <= 0)
        {
            return -WEBSOCKET_READ_ERROR;
        }
        pos += read_len;
        len -= pos;
    }

    return pos;
}

static int websocket_send_nbytes(struct websocket_session *session, void *buf, size_t len, int flags)
{
    int send_len = 0;
    size_t pos = 0;

    while (len)
    {
        send_len = websocket_send(session, (uint8_t *)buf + pos, len, flags);
        if (send_len <= 0)
        {
            return -WEBSOCKET_READ_ERROR;
        }
        pos += send_len;
        len -= pos;
    }

    return pos;
}

static void websocket_mask_data(void *encode_buf, const void *buf, uint32_t *mask_key, size_t length)
{
#define UNALIGNED(X, Y) \
    (((size_t)X & (sizeof (size_t) - 1)) | ((size_t)Y & (sizeof (size_t) - 1)))
#define BIGBLOCKSIZE    (sizeof (uint32_t) << 2)
#define LITTLEBLOCKSIZE (sizeof (uint32_t))
#define TOO_SMALL(LEN)  ((LEN) < BIGBLOCKSIZE)

    char *dst_ptr = (char *)encode_buf;
    char *src_ptr = (char *)buf;

    uint32_t *aligned_dst;
    uint32_t *aligned_src;
    int len = length;

    if (!TOO_SMALL(len) && !UNALIGNED(src_ptr, dst_ptr)) 
    {
        aligned_dst = (uint32_t *)dst_ptr;
        aligned_src = (uint32_t *)src_ptr; 

        /* Copy 4X long words at a time if possible. */
        while (len >= BIGBLOCKSIZE)
        {
            *aligned_dst++ = *aligned_src++ ^ *mask_key;
            *aligned_dst++ = *aligned_src++ ^ *mask_key;
            *aligned_dst++ = *aligned_src++ ^ *mask_key;
            *aligned_dst++ = *aligned_src++ ^ *mask_key;
            len -= BIGBLOCKSIZE;
        }

        /* Copy one long word at a time if possible. */
        while (len >= LITTLEBLOCKSIZE)
        {
            *aligned_dst++ = *aligned_src++ ^ *mask_key;
            len -= LITTLEBLOCKSIZE;
        }

        /* Pick up any residual with a byte copier. */
        dst_ptr = (char *)aligned_dst;
        src_ptr = (char *)aligned_src;
    }

    for (int i = 0; i < len; i++)
    {
        *dst_ptr++ = *src_ptr++ ^ ((char *)mask_key)[i % 4];
    }

#undef UNALIGNED
#undef BIGBLOCKSIZE
#undef LITTLEBLOCKSIZE
#undef TOO_SMALL
}

static int websocket_send_control_frame(struct websocket_session *session, websocket_frame_type_t opcode, const char *buf, char length)
{
    uint32_t mask_key = 0;
    struct control_frame *fram = (struct control_frame *)session->cache;

    if (fram == NULL)
    {
        return -WEBSOCKET_NOMEM;
    }

    ws_memset(session->cache, 0, sizeof(struct websocket_frame_head));
    fram->head.fin = 1;
    fram->head.opcode = opcode;
    fram->head.mask = 1;
    fram->head.payload_len = length;

    ws_srand_key((unsigned char *)&mask_key, sizeof(uint32_t));
    ws_memcpy(fram->masking_key, &mask_key, sizeof(uint32_t));

    if (websocket_send_nbytes(session, (void *)fram, sizeof(struct control_frame) - 1, 0) != sizeof(struct control_frame) - 1)
    {
        return -WEBSOCKET_ERROR;
    }

    if (buf != NULL)
    {
        websocket_mask_data(session->cache, buf, &mask_key, length);
        if (websocket_send(session, (void *)session->cache, length, 0) <= 0)
        {
            return -WEBSOCKET_ERROR;
        }
    }

    return WEBSOCKET_OK;
}

static int websocket_get_payload_len(struct websocket_session *session, struct websocket_frame_head *frame_head)
{
    int data_length = 0;
    int res = WEBSOCKET_OK;

    session->info.total_len = 0;

    if ((data_length = frame_head->payload_len) < 126)
    {
        session->info.total_len = data_length;
    }
    else if (data_length == 126)
    {
        uint16_t payload_len = 0;
        /* read two bytes again */
        if ((data_length = websocket_recv_nbytes(session, &payload_len, 2, 0)) <= 0)
        {
            res = -WEBSOCKET_READ_ERROR;
        }
        session->info.total_len = ntohs(payload_len);
    }
    else if (data_length == 127)
    {
        uint64_t payload_len = 0, low, hight;

        if ((data_length = websocket_recv_nbytes(session, &payload_len, 8, 0)) <= 0)
        {
            res = -WEBSOCKET_READ_ERROR;
        }
        low = ntohl((payload_len & 0xffffffff));
        hight = ntohl((payload_len & 0xffffffff00000000) >> 32);

        session->info.total_len = (uint64_t)((uint64_t)(low << 32) | hight);
    }

    session->info.remain_len = session->info.total_len;
    return res;
}

static int websocket_send_encode_package(struct websocket_session *session, const void *buf, uint64_t length, websocket_frame_type_t opcode, char fin)
{
    char head_length;
    int send_length;
    uint32_t mask_key = 0;
    size_t pos = 0;

    ws_srand_key((unsigned char *)&mask_key, 4);
    ws_memset(session->cache, 0, sizeof(struct websocket_frame_head));

    if (length < 126)
    {
        head_length = 6;
        ((struct websocket_frame_head *)session->cache)->fin = fin;
        ((struct websocket_frame_head *)session->cache)->mask = 1;
        ((struct websocket_frame_head *)session->cache)->opcode = opcode;
        ((struct websocket_frame_head *)session->cache)->payload_len = length;
        ws_memcpy(&session->cache[2], &mask_key, 4);
    }
    else if (length < 65535)
    {
        head_length = 8;
        ((struct websocket_frame_head *)session->cache)->fin = 1;
        ((struct websocket_frame_head *)session->cache)->mask = 1;
        ((struct websocket_frame_head *)session->cache)->opcode = opcode;
        ((struct websocket_frame_head *)session->cache)->payload_len = 126;
        *((uint16_t *)(session->cache + 2)) = htons(length);
        ws_memcpy(&session->cache[4], &mask_key, 4);
    }
    else
    {
        head_length = 14;
        ((struct websocket_frame_head *)session->cache)->fin = 1;
        ((struct websocket_frame_head *)session->cache)->mask = 1;
        ((struct websocket_frame_head *)session->cache)->opcode = opcode;
        ((struct websocket_frame_head *)session->cache)->payload_len = 127;
        *((uint64_t *)(session->cache + 2)) = (htonl((length & 0xFFFFFFFF) << 32) | htonl((length >> 32) & 0xFFFFFFFF));
        ws_memcpy(&session->cache[10], &mask_key, 4);
    }

    if (websocket_send_nbytes(session, session->cache, head_length, 0) != head_length)
    {
        return -WEBSOCKET_WRITE_ERROR;
    }

    websocket_mask_data(session->cache, buf, &mask_key, length);

    while (length)
    {
        send_length = length > session->cache_len ? session->cache_len : length;
        if ((send_length = websocket_send(session, session->cache + pos, send_length, 0)) < 0)
        {
            return send_length;
        }
        else if (send_length == 0)
        {
            break;
        }

        length -= send_length;
        pos += send_length;
    }

    return pos;
}

static const char *websocket_wrl_praser_host(const char *host_addr, size_t *host_len)
{
    const char *end;
    *host_len = 0;
    if(host_addr)
    {
        end = strstr(host_addr, ":");
        if(end)
        {
            *host_len = end - host_addr;
        }
    }
    return *host_len ? host_addr : NULL;
}

static const char *websocket_wrl_praser_port(const char *host_addr, size_t *post_len)
{
    const char *start, *end;
    *post_len = 0;
    if(host_addr)
    {
        start = strstr(host_addr, ":");
        if(start)
        {
            end = strstr(start, "/");
            if(end)
            {
                *post_len = end - start - 1;
            }
            else
            {
                *post_len = strlen(start) - 1;
            }
        }
    }
    return *post_len ? (start + 1) : NULL;
}

static const char *websocket_wrl_praser_path(const char *host_addr, size_t *path_len)
{
    const char *start;
    *path_len = 0;
    if(host_addr)
    {
        start = strstr(host_addr, "/");
        if(start)
        {
            *path_len = strlen(start);
        }
        else
        {
            *path_len = 1;
            start = "/";
        }
    }

    return *path_len ? (start) : "/";
}

static const char *websocket_wrl_praser_wss(const char *url, int* is_wss)
{
    const char *host_addr = NULL;
    *is_wss = 0;
    if(strncmp(url, "ws://", 5) == 0)
    {
        host_addr = url + 5;
        *is_wss = 0;
    }
    else if(strncmp(url, "wss://", 6) == 0)
    {
        host_addr = url + 6;
        *is_wss = 1;
    }
    return host_addr ? host_addr : NULL;
}

int websocket_url_praser(const char *url, char **host, char **port, char **path, int* is_wss)
{
    int ret = -WEBSOCKET_ERROR;
    const char *host_addr = NULL, *port_addr = NULL, *path_addr = NULL, *port_def = "80";
    size_t host_len = 0, port_len = 0, path_len = 0;

    host_addr = websocket_wrl_praser_host(websocket_wrl_praser_wss(url, is_wss), &host_len);
    port_addr = websocket_wrl_praser_port(host_addr, &port_len);
    path_addr = websocket_wrl_praser_path(host_addr, &path_len);

    port_def = *is_wss ? "443" : port_def;
    port_addr = port_len ? port_addr : port_def;

    if(host_addr && path_addr)
    {
        *host = ws_malloc(host_len+1);
        *path = ws_malloc(path_len+1);

        if(port_len)
        {
            *port = ws_malloc(port_len+1);
            ws_memcpy((void *)*port, port_addr, port_len);
            *((*port)+port_len) = '\0';
        }
        else
        {
            *port = ws_strdup(port_addr);
        }

        if(*host && *path && *port)
        {
            ws_memcpy((void *)*host, host_addr, host_len);
            ws_memcpy((void *)*path, path_addr, path_len);
            *((*host)+host_len) = '\0';
            *((*path)+path_len) = '\0';
            ret = WEBSOCKET_OK;
        }
        else
        {
            ws_free(*host);
            ws_free(*path);
            ws_free(*port);
            *host = *path = *port = NULL;
        }
    }

    return ret;
}

static unsigned char *websocket_generate_mask_key(struct websocket_session *session)
{
    unsigned char key[16];
    int base64_key_len = sizeof(session->key);

    ws_srand_key(key, 16);
    ws_base64_encode(session->key, &base64_key_len,  key, sizeof(key));

    return session->key;
}

int websocket_header_fields_add(struct websocket_session *session, const char *fmt, ...)
{
    int length;
    va_list args;

    if (session->cache == NULL)
    {
        session->cache = ws_malloc(WEBSOCKET_CACHE_BUFFER_SIZE);
        if (session->cache == NULL)
        {
            return -WEBSOCKET_NOMEM;
        }
        session->cache_len = WEBSOCKET_CACHE_BUFFER_SIZE;
    }

    va_start(args, fmt);
    length = vsnprintf(session->cache + session->head_len, session->cache_len - session->head_len, fmt, args);
    if (length < 0)
    {
        return -WEBSOCKET_ERROR;
    }
    va_end(args);

    session->head_len += length;

    if (session->head_len >= session->cache_len)
    {
        return -WEBSOCKET_ERROR;
    }

    return length;
}

static int websocket_snprintf(char **ptr, int *size, const char *fmt, ...)
{
    int length = 0;
    va_list args;

    va_start(args, fmt);
    if (*size > 0)
    {
        length = vsnprintf(*ptr, *size, fmt, args);
        if (length < 0)
        {
            *size = -1;
        }
        else
        {
            *size -= length;
            *ptr += length;
        }
    }
    else
    {
        *size = -1;
    }
    va_end(args);

    return length;
}

static int websocket_send_hand_frame(struct websocket_session *session, const char *subprotocol, const char *path, const char *host, const char *port)
{
    int res = WEBSOCKET_OK;
    size_t head_len = 0;
    int remain_len = 0;
    char *ptr = session->cache;
    const char http_head[] =
        "GET %s HTTP/1.1\r\n"
        "Connection: Upgrade\r\n"
        "Host: %s:%s\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n";

    /* Move user-defined header to buffer tail */
    ws_memmove(session->cache + session->cache_len - session->head_len, session->cache, session->head_len);
    remain_len = (int)(session->cache_len - session->head_len);

    head_len += websocket_snprintf(&ptr, &remain_len, http_head, path, host, port, websocket_generate_mask_key(session));
    if (subprotocol != NULL && remain_len > 0)
        head_len += websocket_snprintf(&ptr, &remain_len, "Sec-WebSocket-Protocol: %s\r\n", subprotocol);

    if (remain_len > (int)session->head_len)
        ws_memcpy(ptr, session->cache + session->cache_len - session->head_len, session->head_len);

    head_len += session->head_len;
    ptr += session->head_len;
    remain_len -= session->head_len;
    head_len += websocket_snprintf(&ptr, &remain_len, "\r\n");

    if (remain_len > 0)
    {
        if (websocket_send_nbytes(session, session->cache, head_len, 0) != head_len)
            res =  -WEBSOCKET_WRITE_ERROR;
    }
    else
    {
        res = -WEBSOCKET_ERROR;
    }
    session->head_len = 0;

    return res;
}

static int websocket_check_header_line(struct websocket_session *session, char *header_line, uint16_t *bit_map)
{
    const char *ptr = NULL;
    char *key_guid = NULL;
    const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha1[20] = { 0 }, base64_key[30] = { 0 };
    int res = WEBSOCKET_OK;

    if (strncmp(header_line, "HTTP/1.1 101 Switching Protocols", strlen("HTTP/1.1 101 Switching Protocols")) == 0)
    {
        *bit_map |= 1 << HEADER_HAVE_101_SWITCH_PROTOCOL;
    }
    else if (strncmp(header_line, "Upgrade: ", strlen("Upgrade: ")) == 0)
    {
        if (strncmp(header_line + strlen("Upgrade: "), "websocket", strlen("websocket")) == 0)
        {
            *bit_map |= 1 << HEADER_HAVE_UPGRADE;
        }
    }
    else if (strncmp(header_line, "Connection: ", strlen("Connection: ")) == 0)
    {
        if (strncmp(header_line + strlen("Connection: "), "Upgrade", strlen("Upgrade")) == 0)
        {
            *bit_map |= 1 << HEADER_HAVE_CONNECTION;
        }
    }
    else if (strncmp(header_line, "Sec-WebSocket-Accept: ", strlen("Sec-WebSocket-Accept: ")) == 0)
    {
        uint16_t total_len = strlen(guid) + strlen((const char *)session->key);
        int base64_len = 0;

        ptr = header_line + strlen("Sec-WebSocket-Accept: ");
        key_guid = (session->cache + session->cache_len - total_len - 1);

        sprintf(key_guid, "%s", (const char *)session->key);
        strcat(key_guid, guid);

        base64_len = sizeof(base64_key);
        ws_sha1((unsigned char *)key_guid, total_len, sha1);
        ws_base64_encode(base64_key, &base64_len, sha1, sizeof(sha1));

        if (strcmp((const char *)base64_key, ptr) == 0)
        {
            *bit_map |= 1 << HEADER_HAVE_WEBSOCKET_ACCEPT;
        }
    }
    else if (strncmp(header_line, "Sec-Websocket-Protocol: ", strlen("Sec-Websocket-Protocol: ")) == 0)
    {
        session->subprotocol = ws_strdup((header_line + strlen("Sec-Websocket-Protocol: ")));
        if (session->subprotocol != NULL)
        {
            *bit_map |= 1 << HEADER_HAVE_WEBSOCKET_PROTOCOL;
        }
        else
        {
            res = -WEBSOCKET_NOMEM;
        }
    }

    return res;
}

static int websocket_read_line(struct websocket_session *session, char *buffer, int size)
{
    int rc, count = 0;
    char ch = 0, last_ch = 0;

    /* Keep reading until we fill the buffer. */
    while (count < size)
    {
        rc = websocket_recv(session, (unsigned char *)&ch, 1, 0);
        if (rc <= 0)
            return rc;

        if (ch == '\n' && last_ch == '\r')
        {
            break;
        }

        buffer[count++] = ch;

        last_ch = ch;
    }

    if (count > size)
    {
        ws_log("read line failed. The line data length is out of buffer size(%d)!\n", count);
        return -WEBSOCKET_ERROR;
    }

    return count;
}

static int websocket_recv_and_check_hand_frame(struct websocket_session *session)
{
    int rc = 0, res = WEBSOCKET_OK;
    uint16_t check_value = 0;
    while (1)
    {
        rc = websocket_read_line(session, session->cache, session->cache_len);
        if (rc < 0)
            break;

        if (rc == 0)
            break;
        if ((rc == 1) && (session->cache[0] == '\r'))
        {
            session->cache[0] = '\0';
            break;
        }

        session->cache[rc - 1] = '\0';

        res = websocket_check_header_line(session, session->cache, &check_value);
        if (res != WEBSOCKET_OK)
        {
            break;
        }
    }

    if ((check_value & HEADER_CHECK_MIN_VALUE) < HEADER_CHECK_MIN_VALUE)
    {
        res = -WEBSOCKET_CONNECT_FAILED;
    }

    return res;
}

static int webscoket_tls_init(struct websocket_session *session)
{
    const char *pers = "websocket";
    int success = (
        (session) &&
        (session->tls_session = (MbedTLSSession *)ws_malloc(sizeof(MbedTLSSession))) &&
        (((MbedTLSSession *)session->tls_session)->buffer = ws_malloc(WEBSOCKET_TLS_BUFFER_SIZE)) &&
        (((MbedTLSSession *)session->tls_session)->buffer_len = WEBSOCKET_TLS_BUFFER_SIZE)
    );

    if(success)
    {
        if (mbedtls_client_init(session->tls_session, (void *)pers, strlen(pers)) < 0)
            success = -WEBSOCKET_ERROR;
    }
    else
    {
        success = -WEBSOCKET_NOMEM;
    }

    return success ? WEBSOCKET_OK : success;
}

int websocket_get_block_info_raw(struct websocket_session *session)
{
    uint16_t websocket_head = 0;
    if (session->info.remain_len != 0)
    {
        return -WEBSOCKET_NO_HEAD;
    }

    if (websocket_recv_nbytes(session, &websocket_head, 2, 0) <= 0)
    {
        return -WEBSOCKET_READ_ERROR;
    }

    session->info.frame_type = ((struct websocket_frame_head *)&websocket_head)->opcode;
    session->info.is_slice = !((struct websocket_frame_head *)&websocket_head)->fin;

    return websocket_get_payload_len(session, (struct websocket_frame_head *)&websocket_head);
}

int websocket_get_block_info(struct websocket_session *session)
{
    int res = WEBSOCKET_OK;

    if (websocket_get_block_info_raw(session) != WEBSOCKET_OK)
    {
        return -WEBSOCKET_READ_ERROR;
    }

    while (session->info.frame_type == WEBSOCKET_CLOSE_FRAME || session->info.frame_type == WEBSOCKET_PING_FRAME \
            || session->info.frame_type == WEBSOCKET_PONG_FRAME)
    {
        if (session->info.remain_len != 0)
        {
            if (websocket_recv_nbytes(session, session->cache, session->info.remain_len, 0) <= 0)
            {
                res = -WEBSOCKET_READ_ERROR;
                break;
            }
            session->info.remain_len = 0;
        }

        if (websocket_get_block_info_raw(session) != WEBSOCKET_OK)
        {
            res = -WEBSOCKET_READ_ERROR;
            break;
        }
    }

    return res;
}

int websocket_set_timeout(struct websocket_session *session, int second)
{
    struct timeval timeout;
    timeout.tv_sec = second;
    timeout.tv_usec = 0;

    setsockopt(session->socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               (void *) &timeout, sizeof(timeout));
    setsockopt(session->socket_fd, SOL_SOCKET, SO_SNDTIMEO,
               (void *) &timeout, sizeof(timeout));

    return 0;
}

int websocket_send_ping(struct websocket_session *session, const char *buf, char length)
{
    return websocket_send_control_frame(session, WEBSOCKET_PING_FRAME, buf, length);
}

int websocket_send_pong(struct websocket_session *session, const char *buf, char length)
{
    return websocket_send_control_frame(session, WEBSOCKET_PONG_FRAME, buf, length);
}

int websocket_send_close(struct websocket_session *session, websocket_status_code_t status_code, const char *buf, char length)
{
    char *send_buf;
    char send_length = 2;
    uint16_t statu_code = htons(status_code);

    if (session == NULL || session->socket_fd < 0)
        return -WEBSOCKET_ERROR;

    if (buf != NULL)
        send_length += length;

    send_buf = session->cache + session->cache_len - send_length;
    ws_memcpy(send_buf, &statu_code, sizeof(statu_code));

    if (buf != NULL)
        ws_memcpy(send_buf + sizeof(statu_code), buf, length);

    return websocket_send_control_frame(session, WEBSOCKET_CLOSE_FRAME, (void *)send_buf, send_length);
}

int websocket_read(struct websocket_session *session, void *buf, size_t length)
{
    int recv_len = 0;

    if (session->info.remain_len == 0)
    {
        if (websocket_get_block_info(session) != WEBSOCKET_OK)
        {
            return -WEBSOCKET_ERROR;
        }
    }

    if (session->info.remain_len < length)
    {
        length = session->info.remain_len;
    }

    if ((recv_len = websocket_recv(session, (void *)((char *)buf), length, 0)) <= 0)
    {
        return recv_len;
    }

    session->info.remain_len -= recv_len;

    return recv_len;
}

int websocket_write_slice(struct websocket_session *session, const void *buf, size_t length, websocket_frame_type_t opcode, websocket_slice_t slice_type)
{
    char fin = 0;

    if ((opcode != WEBSOCKET_TEXT_FRAME) && (opcode != WEBSOCKET_BIN_FRAME))
        return -WEBSOCKET_WRITE_ERROR;

    if (slice_type == WEBSOCKET_WRITE_FIRST_SLICE)
    {
        fin = 0;
    }
    else if (slice_type == WEBSOCKET_WRITE_MIDDLE_SLICE)
    {
        fin = 0;
        opcode = 0;
    }
    else if (slice_type == WEBSOCKET_WRITE_END_SLICE)
    {
        fin = 1;
        opcode = 0;
    }

    return websocket_send_encode_package(session, buf, length, opcode, fin);
}

int websocket_write(struct websocket_session *session, const void *buf, size_t length, websocket_frame_type_t opcode)
{
    if ((opcode != WEBSOCKET_TEXT_FRAME) && (opcode != WEBSOCKET_BIN_FRAME))
        return -WEBSOCKET_WRITE_ERROR;

    return websocket_send_encode_package(session, buf, length, opcode, 1);
}

int websocket_session_init(struct websocket_session *session)
{
    ws_memset(session, 0, sizeof(struct websocket_session));
    session->socket_fd = -1;

    return WEBSOCKET_OK;
}

static void websocket_recycle_resources(struct websocket_session *session)
{
    if (session->cache)
        ws_free(session->cache);

    if (session->subprotocol)
        ws_free(session->subprotocol);

    websocket_session_init(session);
}

static int websocket_using_tls(struct websocket_session *session, const char *port, const char *host)
{
    int res = -WEBSOCKET_ERROR;

    if ((res = webscoket_tls_init(session)) == WEBSOCKET_OK && session->tls_session)
    {
        session->is_tls = 1;
        ((MbedTLSSession *)session->tls_session)->port = ws_strdup(port);
        ((MbedTLSSession *)session->tls_session)->host = ws_strdup(host);
        if (((MbedTLSSession *)session->tls_session)->port == NULL || ((MbedTLSSession *)session->tls_session)->host == NULL)
        {
            res = -WEBSOCKET_NOMEM;
        }

        if (res == WEBSOCKET_OK)
            res = mbedtls_client_context(session->tls_session);

        if (res == WEBSOCKET_OK)
            res = mbedtls_client_connect(session->tls_session);

        if (res == WEBSOCKET_OK)
            session->socket_fd = ((MbedTLSSession *)session->tls_session)->server_fd.fd;
    }

    return res;
}

static int websocket_connect_server(struct websocket_session *session, const char *port, const char *host)
{
    struct sockaddr_in serv_addr;
    int res = WEBSOCKET_OK;
    struct hostent *server_ip = NULL;
    int socket_handle = -1;

    socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle < 0)
    {
        ws_log("connect failed, create socket(%d) error\n", session->socket_fd);
        return -WEBSOCKET_NOSOCKET;
    }

    server_ip = gethostbyname(host);
    if (server_ip == NULL)
    {
        res = -WEBSOCKET_CONNECT_FAILED;
    }

    if (res == WEBSOCKET_OK)
    {
        ws_memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(atoi(port));
        serv_addr.sin_addr = *((struct in_addr *)server_ip->h_addr);

        if (connect(socket_handle, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) != 0)
        {
            res = -WEBSOCKET_CONNECT_FAILED;
        }
    }
    session->socket_fd = socket_handle;

    return res;
}

int websocket_disconnect(struct websocket_session *session)
{
    if (session->tls_session)
    {
        mbedtls_client_close(session->tls_session);
        session->socket_fd = -1;
    }

    if (session->socket_fd >= 0)
    {
        close(session->socket_fd);
        session->socket_fd = -1;
    }

    websocket_recycle_resources(session);
    return WEBSOCKET_OK;
}

int websocket_connect(struct websocket_session *session, const char *url, const char *subprotocol)
{
    int res = WEBSOCKET_OK;
    char *port = NULL;
    char *path = NULL;
    char *host = NULL;
    int is_wss = 0;

    if (session->cache == NULL)
    {
        session->cache = ws_malloc(WEBSOCKET_CACHE_BUFFER_SIZE);
        if (session->cache == NULL)
        {
            websocket_session_init(session);
            return -WEBSOCKET_NOMEM;
        }
        session->cache_len = WEBSOCKET_CACHE_BUFFER_SIZE;
    }

    if (session->socket_fd > 0)
        return -WEBSOCKET_IS_CONNECT;

    res = websocket_url_praser(url, &host, &port, &path,&is_wss);
    if (res == WEBSOCKET_OK && is_wss)
        res = websocket_using_tls(session, (const char *)port, (const char *)host);

    if (res == WEBSOCKET_OK && session->tls_session == NULL)
        res = websocket_connect_server(session, (const char *)port, (const char *)host);

    if (res == WEBSOCKET_OK)
        res = websocket_send_hand_frame(session, subprotocol, path, host, port);

    if (res == WEBSOCKET_OK)
        res = websocket_recv_and_check_hand_frame(session);

    ws_free(path);
    ws_free(host);
    ws_free(port);

    if (res != WEBSOCKET_OK)
    {
        websocket_disconnect(session);
    }

    return res;
}